#include "core/image_decode.h"
#include "core/ascii_case.h"
#include "core/png_read.h"
#include <fstream>
#include <algorithm>

#include <cstdio>
#include <cstring>
#include <new>

namespace obl::image {
namespace {

// The game's loaders have no size limit; refuse images that could not be allocated in the 32-bit process anyway.
constexpr uint64_t kMaxPixelBytes = 1u << 28;

struct Reader {
    const uint8_t* data;
    size_t size;

    bool Has(uint64_t offset, uint64_t length) const { return offset <= size && length <= size - offset; }
    uint16_t U16(uint64_t at) const { return static_cast<uint16_t>(data[at] | (data[at + 1] << 8)); }
    uint32_t U32(uint64_t at) const { return U16(at) | (static_cast<uint32_t>(U16(at + 2)) << 16); }
};

bool Allocate(Image& image, int32_t width, int32_t height, int32_t bpp, bool palette)
{
    if (width < 0 || height < 0 || bpp < 0) return false;
    uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height); // < 2^62
    uint64_t bytesPerPixel = static_cast<uint64_t>(bpp / 8);
    if (bytesPerPixel != 0 && pixelCount > kMaxPixelBytes / bytesPerPixel) return false;
    uint64_t bytes = pixelCount * bytesPerPixel;
    image.width = width;
    image.height = height;
    image.bpp = bpp;
    image.pixels.assign(static_cast<size_t>(bytes), 0);
    image.palette.clear();
    if (palette) image.palette.assign(768, 0);
    return true;
}

// ---- TGA -------------------------------------------------------------------------------------------------------

// BGR colour map entries → RGB palette.
bool TgaReadPalette(const Reader& file, uint64_t at, uint32_t count, Image& out)
{
    if (count > 256 || !file.Has(at, count * 3ull)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        out.palette[i * 3 + 0] = file.data[at + i * 3 + 2];
        out.palette[i * 3 + 1] = file.data[at + i * 3 + 1];
        out.palette[i * 3 + 2] = file.data[at + i * 3 + 0];
    }
    return true;
}

// Bottom-up rows → top-down, BGR(A) → RGB(A). `src` holds width*height pixels.
void TgaCopyPixelsFlipped(const uint8_t* src, Image& out)
{
    size_t bytes = static_cast<size_t>(out.bpp / 8);
    size_t rowBytes = static_cast<size_t>(out.width) * bytes;
    for (int32_t row = 0; row < out.height; ++row) {
        const uint8_t* from = src + static_cast<size_t>(row) * rowBytes;
        uint8_t* to = out.pixels.data() + static_cast<size_t>(out.height - 1 - row) * rowBytes;
        if (out.bpp == 8) {
            std::memcpy(to, from, rowBytes);
            continue;
        }
        for (int32_t x = 0; x < out.width; ++x, from += bytes, to += bytes) {
            to[0] = from[2];
            to[1] = from[1];
            to[2] = from[0];
            if (bytes == 4) to[3] = from[3];
        }
    }
}

// RLE decoding, then the same flip as CopyPixelsFlipped: packets of up to 128 pixels, high bit = repeat.
// A packet that runs past the pixel count is not rejected by a naive decoder, which never ends (the remaining count goes negative).
bool TgaDecodeRle(const Reader& file, uint64_t at, Image& out)
{
    size_t bytes = static_cast<size_t>(out.bpp / 8);
    uint64_t remaining = static_cast<uint64_t>(out.width) * static_cast<uint64_t>(out.height);
    std::vector<uint8_t> decoded(out.pixels.size());
    size_t write = 0;
    while (remaining != 0) {
        if (!file.Has(at, 1)) return false;
        uint8_t header = file.data[at++];
        uint64_t count = (header & 0x7fu) + 1;
        if (count > remaining) return false;
        if (header & 0x80) {
            if (!file.Has(at, bytes)) return false;
            for (uint64_t i = 0; i < count; ++i, write += bytes) std::memcpy(decoded.data() + write, file.data + at, bytes);
            at += bytes;
        } else {
            if (!file.Has(at, count * bytes)) return false;
            std::memcpy(decoded.data() + write, file.data + at, count * bytes);
            write += count * bytes;
            at += count * bytes;
        }
        remaining -= count;
    }
    TgaCopyPixelsFlipped(decoded.data(), out);
    return true;
}

// ---- TIFF ------------------------------------------------------------------------------------------------------

// The fields the game keeps from the IFD (its 0xc0 byte Tiff context); a tag is stored as the raw 32-bit value field.
struct TiffInfo {
    uint32_t width = 0; // 0x100
    uint32_t length = 0; // 0x101
    uint16_t bitsPerSample = 0; // 0x102 (low 16 bits of the value)
    uint32_t compression = 0; // 0x103
    uint32_t stripCount = 0; // 0x111 count
    uint32_t stripOffsetsValue = 0;
    uint16_t samplesPerPixel = 0; // 0x115
    uint32_t rowsPerStrip = 0; // 0x116
    uint32_t stripCountsValue = 0; // 0x117
    uint32_t colorMap = 0; // 0x140
    uint32_t tileWidth = 0; // 0x142
    uint32_t tileLength = 0; // 0x143
    uint32_t tileOffsetsValue = 0; // 0x144
    uint32_t tileCountsValue = 0; // 0x145
    std::vector<uint32_t> stripOffsets, stripCounts, tileOffsets, tileCounts;
};

// Offsets/byte counts of an IFD: inline in the value field for one entry, otherwise an array at that offset.
bool ReadArray(const Reader& file, uint32_t pointerOrValue, uint64_t count, std::vector<uint32_t>& out)
{
    if (count > 1) {
        if (!file.Has(pointerOrValue, count * 4)) return false;
        out.resize(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i) out[static_cast<size_t>(i)] = file.U32(pointerOrValue + i * 4);
    } else if (count == 1) {
        out.assign(1, pointerOrValue);
    }
    return true;
}

// Reads the image file directory: the tags the decoder uses.
bool TiffReadIfd(const Reader& file, uint32_t ifd, TiffInfo& info)
{
    if (!file.Has(ifd, 2)) return false;
    uint32_t entries = file.U16(ifd);
    uint64_t at = ifd + 2ull;
    for (uint32_t i = 0; i < entries; ++i, at += 12) {
        if (!file.Has(at, 12)) return false;
        uint32_t tag = file.U16(at);
        uint32_t count = file.U32(at + 4);
        uint32_t value = file.U32(at + 8);
        switch (tag) {
        case 0x100: info.width = value; break;
        case 0x101: info.length = value; break;
        case 0x102: info.bitsPerSample = static_cast<uint16_t>(value); break;
        case 0x103: info.compression = value; break;
        case 0x111:
            info.stripCount = count;
            info.stripOffsetsValue = value;
            break;
        case 0x115: info.samplesPerPixel = static_cast<uint16_t>(value); break;
        case 0x116: info.rowsPerStrip = value; break;
        case 0x117: info.stripCountsValue = value; break;
        case 0x140: info.colorMap = value; break;
        case 0x142: info.tileWidth = value; break;
        case 0x143: info.tileLength = value; break;
        case 0x144: info.tileOffsetsValue = value; break;
        case 0x145: info.tileCountsValue = value; break;
        default: break; // ImageDescription (title) and the rest are not used by the loader
        }
    }
    if (info.stripOffsetsValue == 0) {
        // No strips: tiles are assumed and the tile size divides.
        if (info.tileWidth == 0 || info.tileLength == 0) return false;
        uint64_t count = static_cast<uint64_t>(info.length / info.tileLength) * (info.width / info.tileWidth);
        return ReadArray(file, info.tileCountsValue, count, info.tileCounts) &&
                ReadArray(file, info.tileOffsetsValue, count, info.tileOffsets);
    }
    return ReadArray(file, info.stripCountsValue, info.stripCount, info.stripCounts) &&
            ReadArray(file, info.stripOffsetsValue, info.stripCount, info.stripOffsets);
}

// Uncompressed strips are stored back to back.
bool TiffReadStrips(const Reader& file, const TiffInfo& info, Image& out)
{
    uint64_t write = 0;
    for (uint32_t i = 0; i < info.stripCount; ++i) {
        if (i >= info.stripOffsets.size() || i >= info.stripCounts.size()) return false;
        uint64_t length = info.stripCounts[i];
        if (!file.Has(info.stripOffsets[i], length) || write + length > out.pixels.size()) return false;
        std::memcpy(out.pixels.data() + write, file.data + info.stripOffsets[i], static_cast<size_t>(length));
        write += length;
    }
    return true;
}

// Uncompressed tiles, each stored as TileLength consecutive rows of TileWidth pixels.
bool TiffReadTiles(const Reader& file, const TiffInfo& info, Image& out)
{
    uint64_t tileRows = info.length / info.tileLength;
    uint64_t tilesAcross = info.width / info.tileWidth;
    uint64_t bytesPerPixel = static_cast<uint64_t>(info.bitsPerSample >> 3) * info.samplesPerPixel;
    uint64_t tileRowBytes = info.tileWidth * bytesPerPixel;
    uint64_t index = 0;
    for (uint64_t row = 0; row < tileRows; ++row) {
        for (uint64_t column = 0; column < tilesAcross; ++column, ++index) {
            if (index >= info.tileOffsets.size()) return false;
            uint64_t from = info.tileOffsets[static_cast<size_t>(index)];
            for (uint64_t line = 0; line < info.tileLength; ++line, from += tileRowBytes) {
                uint64_t to = (info.tileLength * row + line) * info.width * bytesPerPixel + column * tileRowBytes;
                if (!file.Has(from, tileRowBytes) || to + tileRowBytes > out.pixels.size()) return false;
                std::memcpy(out.pixels.data() + to, file.data + from, static_cast<size_t>(tileRowBytes));
            }
        }
    }
    return true;
}

// PackBits strips: every strip holds RowsPerStrip rows (the last one the remainder); each row is
// unpacked until it has width * bytesPerPixel bytes. A run may cross into the next row's bytes, which that row then
// overwrites, as the format requires; the source pointer simply carries on.
bool TiffReadPackBits(const Reader& file, const TiffInfo& info, Image& out)
{
    if (info.rowsPerStrip == 0) return false; // avoids a division by zero
    uint32_t rowsInLastStrip = static_cast<uint32_t>(out.height) % info.rowsPerStrip;
    if (rowsInLastStrip == 0) rowsInLastStrip = info.rowsPerStrip;
    uint64_t rowBytes = static_cast<uint64_t>(out.bpp / 8) * static_cast<uint64_t>(out.width);
    for (uint32_t strip = 0; strip < info.stripCount; ++strip) {
        if (strip >= info.stripOffsets.size() || strip >= info.stripCounts.size()) return false;
        uint64_t at = info.stripOffsets[strip];
        uint64_t end = at + info.stripCounts[strip];
        if (!file.Has(at, info.stripCounts[strip])) return false;
        uint32_t rows = strip == info.stripCount - 1 ? rowsInLastStrip : info.rowsPerStrip;
        for (uint32_t row = 0; row < rows; ++row) {
            uint64_t write = (static_cast<uint64_t>(info.rowsPerStrip) * strip + row) * rowBytes;
            for (uint64_t produced = 0; produced < rowBytes;) {
                if (at >= end) return false;
                int8_t control = static_cast<int8_t>(file.data[at++]);
                if (control == -128) continue;
                uint64_t count;
                if (control < 0) {
                    count = static_cast<uint64_t>(1 - control);
                    if (at >= end || write + count > out.pixels.size()) return false;
                    std::memset(out.pixels.data() + write, file.data[at++], static_cast<size_t>(count));
                } else {
                    count = static_cast<uint64_t>(control) + 1;
                    if (at + count > end || write + count > out.pixels.size()) return false;
                    std::memcpy(out.pixels.data() + write, file.data + at, static_cast<size_t>(count));
                    at += count;
                }
                write += count;
                produced += count;
            }
        }
    }
    return true;
}

// 256 red, 256 green, 256 blue 16-bit values.
bool TiffReadColorMap(const Reader& file, uint32_t at, Image& out)
{
    if (!file.Has(at, 1536)) return false;
    for (int channel = 0; channel < 3; ++channel)
        for (int i = 0; i < 256; ++i) out.palette[i * 3 + channel] = TiffColorToByte(file.U16(at + (channel * 256 + i) * 2ull));
    return true;
}

// Drops the fourth byte of every pixel (and the palette).
bool Convert32To24(const Image& in, Image& out)
{
    if (!Allocate(out, in.width, in.height, 24, false)) return false;
    size_t count = static_cast<size_t>(in.width) * static_cast<size_t>(in.height);
    for (size_t i = 0; i < count; ++i) std::memcpy(out.pixels.data() + i * 3, in.pixels.data() + i * 4, 3);
    return true;
}

// High nibble first; only the first 8 palette entries are carried over.
bool Convert4To8(const Image& in, Image& out)
{
    if (!in.hasPalette() || !Allocate(out, in.width, in.height, 8, true)) return false;
    size_t count = static_cast<size_t>(in.width) * static_cast<size_t>(in.height);
    if (in.pixels.size() < (count + 1) / 2) return false;
    std::memcpy(out.palette.data(), in.palette.data(), 24);
    for (size_t i = 0; i < count; ++i) {
        uint8_t byte = in.pixels[i / 2];
        out.pixels[i] = (i & 1) ? (byte & 0x0f) : (byte >> 4);
    }
    return true;
}

bool DecodeTgaImpl(const uint8_t* data, size_t size, Image& out)
{
    Reader file{data, size};
    if (size < 18) return false;
    uint32_t idLength = data[0];
    uint32_t colorMapType = data[1];
    uint32_t imageType = data[2];
    uint32_t colorMapStart = file.U16(3);
    uint32_t colorMapLength = file.U16(5);
    uint32_t colorMapDepth = data[7];
    uint32_t width = file.U16(12);
    uint32_t height = file.U16(14);
    uint32_t bpp = data[16];
    uint32_t descriptor = data[17];

    bool palette = false;
    if (bpp == 8) {
        if (colorMapType != 0) {
            palette = true;
            if (colorMapDepth != 24) return false;
        }
    } else {
        if ((bpp != 24 && bpp != 32) || colorMapType != 0) return false;
    }
    if (colorMapStart != 0 || (descriptor & 0x30) != 0) return false;
    if (!Allocate(out, static_cast<int32_t>(width), static_cast<int32_t>(height), static_cast<int32_t>(bpp), palette)) return false;

    uint64_t at = 18ull + idLength;
    switch (imageType) {
    case 0:
    case 3:
    case 11: return false;
    case 1:
    case 9:
        if (!palette || !TgaReadPalette(file, at, colorMapLength, out)) // without a map reading would start at address 0
            return false;
        [[fallthrough]];
    case 2:
    case 10:
        if (colorMapType != 0) at += colorMapLength * 3ull;
        if (imageType <= 2) {
            if (!file.Has(at, out.pixels.size())) return false;
            TgaCopyPixelsFlipped(data + at, out);
            return true;
        }
        return TgaDecodeRle(file, at, out);
    default: return true; // unknown types: a blank image
    }
}

bool DecodeTiffImpl(const uint8_t* data, size_t size, Image& out)
{
    Reader file{data, size};
    if (size < 8 || file.U16(0) != 0x4949 || file.U16(2) != 0x2a) return false;
    TiffInfo info;
    if (!TiffReadIfd(file, file.U32(4), info)) return false;

    if (info.width > 0x7fffffffu || info.length > 0x7fffffffu) return false;
    bool palette = info.colorMap != 0;
    Image image;
    if (!Allocate(image, static_cast<int32_t>(info.width), static_cast<int32_t>(info.length),
                static_cast<int32_t>(info.samplesPerPixel) * 8, palette))
        return false;

    if (info.tileOffsetsValue == 0) {
        if (info.compression == 1) {
            if (!TiffReadStrips(file, info, image)) return false;
        } else if (info.compression == 0x8005) {
            if (!TiffReadPackBits(file, info, image)) return false;
        }
    } else {
        // Tile arrays exist only when the IFD has no strip offsets.
        if (info.stripOffsetsValue != 0 || !TiffReadTiles(file, info, image)) return false;
    }
    if (palette && !TiffReadColorMap(file, info.colorMap, image)) return false;

    if (info.samplesPerPixel == 4) {
        Image converted;
        if (!Convert32To24(image, converted)) return false;
        image = std::move(converted);
    }
    if (info.bitsPerSample == 4) {
        Image converted;
        if (!Convert4To8(image, converted)) return false;
        image = std::move(converted);
    }
    out = std::move(image);
    return true;
}

} // namespace

uint8_t TiffColorToByte(uint16_t value)
{
    // Preserve the Windows/SSE truncation for all 16-bit channels. The equivalent double expression falls just
    // below integers under i386 extended precision (the Linux converter), changing palettes at exact multiples of 257.
    return static_cast<uint8_t>(value / 257);
}

bool DecodeTga(const uint8_t* data, size_t size, Image& out)
{
    try {
        Image image;
        if (!DecodeTgaImpl(data, size, image)) return false;
        out = std::move(image);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool DecodeTiff(const uint8_t* data, size_t size, Image& out)
{
    try {
        return DecodeTiffImpl(data, size, out);
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool Equal(const Image& a, const Image& b, char* diff, size_t diffSize)
{
    auto report = [&](const char* fmt, long long x, long long y) {
        if (diff && diffSize) std::snprintf(diff, diffSize, fmt, x, y);
        return false;
    };
    if (a.width != b.width) return report("width %lld vs %lld", a.width, b.width);
    if (a.height != b.height) return report("height %lld vs %lld", a.height, b.height);
    if (a.bpp != b.bpp) return report("bpp %lld vs %lld", a.bpp, b.bpp);
    if (a.palette.size() != b.palette.size())
        return report("palette size %lld vs %lld", static_cast<long long>(a.palette.size()), static_cast<long long>(b.palette.size()));
    if (a.pixels.size() != b.pixels.size())
        return report("pixel bytes %lld vs %lld", static_cast<long long>(a.pixels.size()), static_cast<long long>(b.pixels.size()));
    for (size_t i = 0; i < a.palette.size(); ++i)
        if (a.palette[i] != b.palette[i])
            return report("palette byte %lld differs (%lld)", static_cast<long long>(i), a.palette[i] - b.palette[i]);
    for (size_t i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i] != b.pixels[i])
            return report("pixel byte %lld differs (%lld)", static_cast<long long>(i), a.pixels[i] - b.pixels[i]);
    return true;
}

uint64_t Hash(const Image& image)
{
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](const uint8_t* bytes, size_t count) {
        for (size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    };
    int32_t header[3] = {image.width, image.height, image.bpp};
    mix(reinterpret_cast<const uint8_t*>(header), sizeof(header));
    mix(image.palette.data(), image.palette.size());
    mix(image.pixels.data(), image.pixels.size());
    return hash;
}

bool LoadPng(const std::filesystem::path& path, Image& out, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open " + path.string();
        return false;
    }
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(stream), {});
    png_read::Image decoded;
    if (!png_read::Decode(bytes, decoded, error)) return false;
    if (decoded.bitDepth != 8 || decoded.channels == 2 || (decoded.indexed && decoded.palette.empty())) {
        error = "unsupported engine image";
        return false;
    }
    out = {};
    out.width = decoded.width;
    out.height = decoded.height;
    out.bpp = decoded.channels * 8;
    out.pixels = std::move(decoded.bytes);
    if (decoded.indexed) out.palette = std::move(decoded.palette);
    return true;
}

std::filesystem::path PackPath(std::string path)
{
    for (char& c : path) {
        if (c == '\\')
            c = '/';
        else
            c = ascii::Lower(c);
    }
    if (path.starts_with("./")) path.erase(0, 2);
    if (path == "data/cursor.tga") return "packs/base/images/cursor.png";
    const std::string prefix = "data/screens/";
    if (path.starts_with(prefix) && path.ends_with(".tga")) {
        auto key = path.substr(prefix.size(), path.size() - prefix.size() - 4);
        if (!key.empty() &&
                std::all_of(key.begin(), key.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }))
            return std::filesystem::path("packs/base/images/screens") / (key + ".png");
    }
    if (path.size() == 23 && path.starts_with("data/haze/wibble_0") && path.ends_with(".tif") && path[18] >= '0' && path[18] <= '5')
        return std::filesystem::path("packs/base/images/haze") / (std::string(1, path[18]) + ".png");
    return {};
}

} // namespace obl::image
