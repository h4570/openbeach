#include "png.h"

#include <cstdio>
#include <vector>

namespace obl::test {

namespace {

uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void PutBE(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void Chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data)
{
    PutBE(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    PutBE(out, Crc32(out.data() + start, out.size() - start));
}

} // namespace

bool WritePngRgb555(const PathChar* path, const uint16_t* pixels, int width, int height, int pitchPixels)
{
    // Raw scanlines: filter byte 0 + RGB triplets.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint16_t* row = pixels + static_cast<size_t>(y) * pitchPixels;
        for (int x = 0; x < width; ++x) {
            uint16_t p = row[x];
            uint8_t r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
            raw.push_back(static_cast<uint8_t>((r << 3) | (r >> 2)));
            raw.push_back(static_cast<uint8_t>((g << 3) | (g >> 2)));
            raw.push_back(static_cast<uint8_t>((b << 3) | (b >> 2)));
        }
    }

    // zlib stream with stored (uncompressed) deflate blocks - simple and good enough for test shots.
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len));
        z.push_back(static_cast<uint8_t>(len >> 8));
        z.push_back(static_cast<uint8_t>(~len));
        z.push_back(static_cast<uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
        if (last) break;
    }
    PutBE(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    PutBE(ihdr, static_cast<uint32_t>(width));
    PutBE(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});

#ifdef _WIN32
    FILE* f = _wfopen(path, L"wb");
#else
    FILE* f = fopen(path, "wb");
#endif
    if (!f) return false;
    bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    fclose(f);
    return ok;
}

} // namespace obl::test
