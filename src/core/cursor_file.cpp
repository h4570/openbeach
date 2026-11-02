#include "core/cursor_file.h"

namespace obl::cursor_file {
namespace {

uint32_t Read16(const uint8_t* p)
{
    return p[0] | (p[1] << 8);
}

uint32_t Read32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Rows of a DIB are padded to 4 bytes.
size_t Stride(int width, int bpp)
{
    return ((static_cast<size_t>(width) * bpp + 31) / 32) * 4;
}

bool DecodeEntry(const uint8_t* data, size_t size, int index, bool cursor, Image& out)
{
    const uint8_t* entry = data + 6 + static_cast<size_t>(index) * 16;
    const uint32_t bytes = Read32(entry + 8), offset = Read32(entry + 12);
    if (offset > size || bytes > size - offset || bytes < 40) return false;
    const uint8_t* dib = data + offset;
    const uint32_t headerSize = Read32(dib);
    const auto width = static_cast<int32_t>(Read32(dib + 4));
    const auto doubleHeight = static_cast<int32_t>(Read32(dib + 8));
    const uint32_t bpp = Read16(dib + 14), compression = Read32(dib + 16);
    uint32_t colors = Read32(dib + 32);
    if (headerSize < 40 || headerSize > bytes || width <= 0 || width > 256 || doubleHeight <= 0 || doubleHeight % 2 || doubleHeight > 512)
        return false;
    if (compression != 0 || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) return false;
    const int height = doubleHeight / 2;
    if (bpp <= 8 && colors == 0) colors = 1u << bpp;
    if (bpp > 8) colors = 0;
    if (colors > 256) return false;
    const size_t paletteAt = headerSize, xorAt = paletteAt + colors * 4;
    const size_t xorStride = Stride(width, static_cast<int>(bpp)), andStride = Stride(width, 1);
    const size_t andAt = xorAt + xorStride * height;
    if (andAt + andStride * height > bytes) return false;

    out.width = width;
    out.height = height;
    // An icon entry keeps planes and bit count where a cursor entry keeps its hot spot.
    out.hotX = cursor ? static_cast<int>(Read16(entry + 4)) : 0;
    out.hotY = cursor ? static_cast<int>(Read16(entry + 6)) : 0;
    out.argb.assign(static_cast<size_t>(width) * height, 0);
    bool alpha = false;
    if (bpp == 32)
        for (int y = 0; y < height && !alpha; ++y)
            for (int x = 0; x < width && !alpha; ++x) alpha = dib[xorAt + y * xorStride + x * 4 + 3] != 0;
    for (int y = 0; y < height; ++y) {
        // DIB rows are stored bottom-up.
        const uint8_t* xorRow = dib + xorAt + (height - 1 - y) * xorStride;
        const uint8_t* andRow = dib + andAt + (height - 1 - y) * andStride;
        for (int x = 0; x < width; ++x) {
            const bool transparent = (andRow[x / 8] >> (7 - x % 8)) & 1;
            uint32_t rgb = 0, a = transparent ? 0 : 255;
            if (bpp <= 8) {
                const unsigned bit = static_cast<unsigned>(x) * bpp;
                const unsigned index = (xorRow[bit / 8] >> (8 - bpp - bit % 8)) & ((1u << bpp) - 1);
                if (index >= colors) return false;
                const uint8_t* entryColor = dib + paletteAt + index * 4;
                rgb = (entryColor[2] << 16) | (entryColor[1] << 8) | entryColor[0];
                // Monochrome "invert screen" (mask and colour set) cannot be expressed in ARGB: opaque black.
                if (transparent && index != 0 && bpp == 1) {
                    rgb = 0;
                    a = 255;
                }
            } else {
                const uint8_t* p = xorRow + x * (bpp / 8);
                rgb = (p[2] << 16) | (p[1] << 8) | p[0];
                if (alpha) a = p[3];
            }
            out.argb[static_cast<size_t>(y) * width + x] = a ? (a << 24) | rgb : 0;
        }
    }
    return true;
}

// The directory header: reserved 0, the type (1 icon, 2 cursor) and `count` 16-byte entries inside the file.
bool CheckDirectory(const uint8_t* data, size_t size, uint32_t type)
{
    if (size < 6 || Read16(data) != 0 || Read16(data + 2) != type) return false;
    const uint32_t count = Read16(data + 4);
    return count != 0 && 6 + static_cast<size_t>(count) * 16 <= size;
}

} // namespace

bool Decode(const uint8_t* data, size_t size, Image& out)
{
    return CheckDirectory(data, size, 2) && DecodeEntry(data, size, 0, true, out);
}

bool DecodeIcon(const uint8_t* data, size_t size, Image& out)
{
    if (!CheckDirectory(data, size, 1)) return false;
    // The richest entry: largest size (0 means 256), then most bits per pixel.
    const int count = static_cast<int>(Read16(data + 4));
    int best = 0;
    uint32_t bestScore = 0;
    for (int i = 0; i < count; ++i) {
        const uint8_t* entry = data + 6 + static_cast<size_t>(i) * 16;
        const uint32_t width = entry[0] ? entry[0] : 256;
        const uint32_t score = width * 64 + Read16(entry + 6);
        if (score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return DecodeEntry(data, size, best, false, out);
}

} // namespace obl::cursor_file
