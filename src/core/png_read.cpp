#include "core/png_read.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

// stb_image's zlib inflater is the only part used; the PNG chunks are read here, because stb expands palette images to RGB and the
// converter's indices must come back as they were written. Static, so this translation unit owns the copy.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../../third_party/stb/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace obl::png_read {
namespace {

constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
constexpr uint32_t kMaxSide = 16384;
constexpr uint8_t kColorGray = 0;
constexpr uint8_t kColorRgb = 2;
constexpr uint8_t kColorIndexed = 3;
constexpr uint8_t kColorGrayAlpha = 4;
constexpr uint8_t kColorRgba = 6;

uint32_t BigEndian32(const uint8_t* at)
{
    return (uint32_t(at[0]) << 24) | (uint32_t(at[1]) << 16) | (uint32_t(at[2]) << 8) | at[3];
}

bool Fail(std::string& error, std::string_view message)
{
    error.assign(message);
    return false;
}

// The predictor of PNG's Paeth filter: the neighbour nearest to a + b - c, ties taken in the order a, b, c.
uint8_t Paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

} // namespace

bool Decode(std::span<const uint8_t> bytes, Image& image, std::string& error)
{
    image = {};
    int& width = image.width;
    int& height = image.height;
    std::vector<uint8_t>& samples = image.bytes;
    if (bytes.size() < sizeof(kSignature) || std::memcmp(bytes.data(), kSignature, sizeof(kSignature)) != 0)
        return Fail(error, "not a PNG file");

    // Chunks: length, type, data, CRC. The CRC is not checked; a damaged image fails in the inflater or the filters.
    size_t position = sizeof(kSignature);
    bool haveHeader = false, haveEnd = false;
    int channels = 0, bitDepth = 0;
    std::vector<uint8_t> compressed;
    while (!haveEnd) {
        if (bytes.size() - position < 12) return Fail(error, "truncated PNG chunk");
        const uint32_t length = BigEndian32(bytes.data() + position);
        if (length > bytes.size() - position - 12) return Fail(error, "truncated PNG chunk");
        const std::string_view type(reinterpret_cast<const char*>(bytes.data() + position + 4), 4);
        const uint8_t* data = bytes.data() + position + 8;
        position += 12 + length;
        if (type == "IHDR") {
            if (haveHeader || length != 13) return Fail(error, "bad PNG header chunk");
            const uint32_t w = BigEndian32(data), h = BigEndian32(data + 4);
            if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide) return Fail(error, "PNG size out of range");
            bitDepth = data[8];
            const uint8_t colourType = data[9];
            if (colourType == kColorGray && (bitDepth == 8 || bitDepth == 16))
                channels = 1;
            else if (colourType == kColorRgb && bitDepth == 8)
                channels = 3;
            else if (colourType == kColorIndexed && bitDepth == 8)
                channels = 1;
            else if (colourType == kColorRgba && bitDepth == 8)
                channels = 4;
            else if (colourType == kColorGrayAlpha && bitDepth == 8)
                channels = 2;
            else
                return Fail(error, "unsupported PNG bit depth or colour type");
            if (data[10] != 0 || data[11] != 0 || data[12] != 0)
                return Fail(error, "unsupported PNG compression, filter method or interlace");
            width = static_cast<int>(w);
            height = static_cast<int>(h);
            image.channels = channels;
            image.bitDepth = bitDepth;
            image.indexed = colourType == kColorIndexed;
            haveHeader = true;
        } else if (type == "PLTE") {
            if (!haveHeader || length == 0 || length > 768 || length % 3) return Fail(error, "bad PNG palette");
            image.palette.assign(data, data + length);
            image.palette.resize(768, 0);
        } else if (type == "IDAT") {
            if (!haveHeader) return Fail(error, "PNG image data before the header");
            compressed.insert(compressed.end(), data, data + length);
        } else if (type == "IEND") {
            haveEnd = true;
        }
    }
    if (!haveHeader || compressed.empty()) return Fail(error, "PNG without image data");

    // One filter byte, then one row of `bpp` bytes per pixel. The filters refer to the byte `bpp` back, the pixel to the left, so for
    // 16-bit or RGB samples the neighbour is not the previous byte.
    const size_t bpp = static_cast<size_t>(channels) * static_cast<size_t>(bitDepth) / 8;
    const size_t stride = static_cast<size_t>(width) * bpp;
    const size_t expected = static_cast<size_t>(height) * (stride + 1);
    if (compressed.size() > 0x7fffffff) return Fail(error, "PNG image data too large");
    int inflated = 0;
    std::unique_ptr<char, void (*)(void*)> raw(
            stbi_zlib_decode_malloc_guesssize_headerflag(reinterpret_cast<const char*>(compressed.data()),
                    static_cast<int>(compressed.size()), static_cast<int>(expected), &inflated, 1),
            std::free);
    if (!raw || static_cast<size_t>(inflated) != expected) return Fail(error, "PNG image data does not inflate to the image size");

    const auto* in = reinterpret_cast<const uint8_t*>(raw.get());
    samples.assign(stride * static_cast<size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        const uint8_t filter = in[static_cast<size_t>(y) * (stride + 1)];
        if (filter > 4) return Fail(error, "unrecognized PNG filter");
        const uint8_t* line = in + static_cast<size_t>(y) * (stride + 1) + 1;
        uint8_t* out = samples.data() + static_cast<size_t>(y) * stride;
        const uint8_t* up = y ? out - stride : nullptr;
        for (size_t x = 0; x < stride; ++x) {
            const int left = x >= bpp ? out[x - bpp] : 0;
            const int above = up ? up[x] : 0;
            const int upLeft = (up && x >= bpp) ? up[x - bpp] : 0;
            int predictor = 0;
            switch (filter) {
            case 1: predictor = left; break;
            case 2: predictor = above; break;
            case 3: predictor = (left + above) / 2; break;
            case 4: predictor = Paeth(left, above, upLeft); break;
            default: break;
            }
            out[x] = static_cast<uint8_t>(line[x] + predictor);
        }
    }
    return true;
}

bool DecodeSamples(std::span<const uint8_t> bytes, int& width, int& height, std::vector<uint8_t>& samples, std::string& error)
{
    Image image;
    if (!Decode(bytes, image, error)) return false;
    if (image.channels != 1 || image.bitDepth != 8) return Fail(error, "PNG is not an 8-bit gray or palette image");
    width = image.width;
    height = image.height;
    samples = std::move(image.bytes);
    return true;
}

} // namespace obl::png_read
