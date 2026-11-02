#include "importer/src/writers/png_write.h"

// Private copy of the writer: obl_common compiles its own (renderer_jpeg.cpp) and the Linux unit tests link both
// libraries into one executable.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505) // the BMP/TGA/JPEG writers stay unused
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../../third_party/stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace obl::convert {

namespace {
void Big32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int shift : {24, 16, 8, 0}) out.push_back(static_cast<uint8_t>(value >> shift));
}
void Chunk(std::vector<uint8_t>& out, const char* type, std::span<const uint8_t> bytes)
{
    Big32(out, static_cast<uint32_t>(bytes.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
    Big32(out, stbiw__crc32(out.data() + start, static_cast<int>(bytes.size() + 4)));
}
std::vector<uint8_t> PlanePng(
        int width, int height, int bits, int type, std::span<const uint8_t> rows, std::span<const uint8_t> palette = {})
{
    int size = 0;
    unsigned char* compressed = stbi_zlib_compress(const_cast<unsigned char*>(rows.data()), static_cast<int>(rows.size()), &size, 8);
    if (!compressed) return {};
    std::vector<uint8_t> out{137, 80, 78, 71, 13, 10, 26, 10}, header;
    Big32(header, width);
    Big32(header, height);
    header.insert(header.end(), {static_cast<uint8_t>(bits), static_cast<uint8_t>(type), 0, 0, 0});
    Chunk(out, "IHDR", header);
    if (!palette.empty()) Chunk(out, "PLTE", palette);
    Chunk(out, "IDAT", std::span(compressed, size));
    STBIW_FREE(compressed);
    Chunk(out, "IEND", {});
    return out;
}
} // namespace

std::vector<uint8_t> EncodeIndexedPng(int width, int height, std::span<const uint8_t> pixels, std::span<const uint8_t> palette)
{
    if (width <= 0 || height <= 0 || pixels.size() != size_t(width) * height || palette.empty() || palette.size() % 3 ||
            palette.size() > 768)
        return {};
    std::vector<uint8_t> rows;
    for (int y = 0; y < height; ++y) {
        rows.push_back(0);
        for (int x = 0; x < width; ++x) {
            const auto index = pixels[size_t(y) * width + x];
            if (index >= palette.size() / 3) return {};
            rows.push_back(index);
        }
    }
    return PlanePng(width, height, 8, 3, rows, palette);
}

std::vector<uint8_t> EncodeGray16Png(int width, int height, std::span<const uint16_t> pixels)
{
    if (width <= 0 || height <= 0 || pixels.size() != size_t(width) * height) return {};
    std::vector<uint8_t> rows;
    rows.reserve((size_t(width) * 2 + 1) * height);
    for (int y = 0; y < height; ++y) {
        rows.push_back(0);
        for (int x = 0; x < width; ++x) {
            const auto value = pixels[size_t(y) * width + x];
            rows.push_back(static_cast<uint8_t>(value >> 8));
            rows.push_back(static_cast<uint8_t>(value));
        }
    }
    return PlanePng(width, height, 16, 0, rows);
}

std::vector<uint8_t> EncodePng(int width, int height, int channels, std::span<const uint8_t> pixels)
{
    std::vector<uint8_t> out;
    if (width <= 0 || height <= 0 || channels < 1 || channels > 4 ||
            pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels))
        return out;
    auto append = [](void* context, void* data, int size) {
        auto* bytes = static_cast<const uint8_t*>(data);
        static_cast<std::vector<uint8_t>*>(context)->insert(static_cast<std::vector<uint8_t>*>(context)->end(), bytes, bytes + size);
    };
    if (!stbi_write_png_to_func(append, &out, width, height, channels, pixels.data(), width * channels)) out.clear();
    return out;
}

} // namespace obl::convert
