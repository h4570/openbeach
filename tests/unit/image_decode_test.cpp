
#include "unit_test.h"

#include "core/image_decode.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace obl::image;

namespace {

using Bytes = std::vector<uint8_t>;

void Put16(Bytes& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void Put32(Bytes& out, uint32_t v)
{
    Put16(out, v);
    Put16(out, v >> 16);
}

Bytes TgaHeader(uint8_t idLength, uint8_t mapType, uint8_t type, uint16_t mapLength, uint8_t mapDepth, uint16_t w, uint16_t h, uint8_t bpp,
        uint8_t descriptor = 0)
{
    Bytes out{idLength, mapType, type};
    Put16(out, 0);
    Put16(out, mapLength);
    out.push_back(mapDepth);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, w);
    Put16(out, h);
    out.push_back(bpp);
    out.push_back(descriptor);
    return out;
}

// 2x2 24 bpp header; the pixel bytes are appended by the caller.
Bytes Tga24(uint8_t type = 2)
{
    return TgaHeader(0, 0, type, 0, 0, 2, 2, 24);
}

// Colour map of 3 entries with 2x2 indices.
Bytes Tga8(uint8_t type, const Bytes& body)
{
    Bytes f = TgaHeader(2, 1, type, 3, 24, 2, 2, 8);
    f.push_back('x');
    f.push_back('y'); // image id
    f.insert(f.end(), {10, 20, 30, 40, 50, 60, 70, 80, 90}); // BGR entries
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

bool DecodeT(const Bytes& f, Image& out)
{
    return DecodeTga(f.data(), f.size(), out);
}
bool DecodeI(const Bytes& f, Image& out)
{
    return DecodeTiff(f.data(), f.size(), out);
}

// ---- TIFF builder: 8-byte header, pixel data blob(s), then the IFD ----

struct Entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;
};

Bytes TiffFile(const Bytes& payload, std::vector<Entry> entries)
{
    Bytes f{'I', 'I', 0x2a, 0};
    Put32(f, static_cast<uint32_t>(8 + payload.size()));
    f.insert(f.end(), payload.begin(), payload.end());
    Put16(f, static_cast<uint32_t>(entries.size()));
    for (const Entry& e : entries) {
        Put16(f, e.tag);
        Put16(f, e.type);
        Put32(f, e.count);
        Put32(f, e.value);
    }
    Put32(f, 0);
    return f;
}

} // namespace

OBL_TEST_CASE("image.tga_raw_24_flips_rows_and_swaps_channels")
{
    Bytes f = Tga24();
    f.insert(f.end(), {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    Image img;
    OBL_REQUIRE(DecodeT(f, img));
    OBL_CHECK_EQ(img.width, 2);
    OBL_CHECK_EQ(img.height, 2);
    OBL_CHECK_EQ(img.bpp, 24);
    OBL_CHECK(!img.hasPalette());
    OBL_CHECK((img.pixels == Bytes{9, 8, 7, 12, 11, 10, 3, 2, 1, 6, 5, 4}));
}

OBL_TEST_CASE("image.tga_raw_32_keeps_alpha")
{
    Bytes f = TgaHeader(0, 0, 2, 0, 0, 2, 1, 32, 8);
    f.insert(f.end(), {1, 2, 3, 4, 5, 6, 7, 8});
    Image img;
    OBL_REQUIRE(DecodeT(f, img));
    OBL_CHECK_EQ(img.bpp, 32);
    OBL_CHECK((img.pixels == Bytes{3, 2, 1, 4, 7, 6, 5, 8}));
}

OBL_TEST_CASE("image.tga_8bit_with_colour_map")
{
    Image img;
    OBL_REQUIRE(DecodeT(Tga8(1, {0, 1, 2, 1}), img));
    OBL_CHECK_EQ(img.bpp, 8);
    OBL_REQUIRE(img.hasPalette());
    OBL_CHECK_EQ(img.palette.size(), 768u);
    OBL_CHECK((img.pixels == Bytes{2, 1, 0, 1}));
    // BGR -> RGB, the rest of the palette stays zero.
    OBL_CHECK((Bytes(img.palette.begin(), img.palette.begin() + 9) == Bytes{30, 20, 10, 60, 50, 40, 90, 80, 70}));
    OBL_CHECK_EQ(img.palette[9], 0);
}

OBL_TEST_CASE("image.tga_rle_matches_raw")
{
    // 4x2 24 bpp: row 0 (bottom) = A A B C, row 1 = D D D D (raw packet of 3 + repeat packet of 5 pixels: 1 + 3*3, 1 + 3)
    Bytes rle = TgaHeader(0, 0, 10, 0, 0, 4, 2, 24);
    rle.insert(rle.end(), {0x81, 1, 2, 3}); // 2 x (1,2,3)
    rle.insert(rle.end(), {0x01, 4, 5, 6, 7, 8, 9}); // raw: 2 pixels
    rle.insert(rle.end(), {0x83, 10, 11, 12}); // 4 x (10,11,12)
    Bytes raw = TgaHeader(0, 0, 2, 0, 0, 4, 2, 24);
    raw.insert(raw.end(), {1, 2, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 10, 11, 12, 10, 11, 12, 10, 11, 12});
    Image a, b;
    OBL_REQUIRE(DecodeT(rle, a));
    OBL_REQUIRE(DecodeT(raw, b));
    OBL_CHECK(Equal(a, b));
}

OBL_TEST_CASE("image.tga_8bit_rle_with_colour_map")
{
    Image a, b;
    OBL_REQUIRE(DecodeT(Tga8(9, {0x81, 2, 0x01, 0, 1}), a));
    OBL_REQUIRE(DecodeT(Tga8(1, {2, 2, 0, 1}), b));
    OBL_CHECK(Equal(a, b));
}

OBL_TEST_CASE("image.tga_unrecognized_type_is_blank_and_known_bad_types_fail")
{
    Bytes f = Tga24(4);
    Image img;
    OBL_REQUIRE(DecodeT(f, img));
    OBL_CHECK_EQ(img.pixels.size(), 12u);
    for (uint8_t v : img.pixels) OBL_CHECK_EQ(v, 0);
    for (int type : {0, 3, 11}) {
        f = Tga24(static_cast<uint8_t>(type));
        f.resize(f.size() + 12, 1);
        OBL_CHECK(!DecodeT(f, img));
    }
}

OBL_TEST_CASE("image.tga_rejects_unsupported_headers")
{
    Image img;
    Bytes f = TgaHeader(0, 0, 2, 0, 0, 2, 2, 16); // 16 bpp
    f.resize(f.size() + 8, 0);
    OBL_CHECK(!DecodeT(f, img));
    f = TgaHeader(0, 0, 2, 0, 0, 2, 2, 24, 0x20); // top-left origin
    f.resize(f.size() + 12, 0);
    OBL_CHECK(!DecodeT(f, img));
    f = TgaHeader(0, 1, 2, 3, 24, 2, 2, 24); // colour map on a 24 bpp image
    f.resize(f.size() + 21, 0);
    OBL_CHECK(!DecodeT(f, img));
    f = TgaHeader(0, 1, 1, 3, 15, 2, 2, 8); // 15 bit colour map
    f.resize(f.size() + 13, 0);
    OBL_CHECK(!DecodeT(f, img));
}

OBL_TEST_CASE("image.tga_rejects_truncated_and_overlong_data")
{
    Image img;
    Bytes f = Tga24();
    f.resize(f.size() + 11, 1); // one byte short
    OBL_CHECK(!DecodeT(f, img));
    OBL_CHECK(!DecodeTga(f.data(), 10, img));
    Bytes rle = TgaHeader(0, 0, 10, 0, 0, 2, 2, 24);
    rle.insert(rle.end(), {0x83, 1, 2, 3}); // 4 pixels fit...
    OBL_CHECK(DecodeT(rle, img));
    rle = TgaHeader(0, 0, 10, 0, 0, 2, 2, 24);
    rle.insert(rle.end(), {0x84, 1, 2, 3}); // ...5 do not
    OBL_CHECK(!DecodeT(rle, img));
    rle = TgaHeader(0, 0, 10, 0, 0, 2, 2, 24);
    rle.insert(rle.end(), {0x81, 1, 2, 3}); // ends early
    OBL_CHECK(!DecodeT(rle, img));
}

OBL_TEST_CASE("image.tiff_color_to_byte_truncates")
{
    OBL_CHECK_EQ(TiffColorToByte(0), 0);
    OBL_CHECK_EQ(TiffColorToByte(65535), 255);
    OBL_CHECK_EQ(TiffColorToByte(257), 1);
    OBL_CHECK_EQ(TiffColorToByte(256), 0);
    OBL_CHECK_EQ(TiffColorToByte(32768), 127);
    for (uint32_t v = 0; v < 65536; ++v)
        if (TiffColorToByte(static_cast<uint16_t>(v)) != (v * 255) / 65535)
            OBL_CHECK_EQ(TiffColorToByte(static_cast<uint16_t>(v)), (v * 255) / 65535);
}

OBL_TEST_CASE("image.tiff_uncompressed_strips_with_colour_map")
{
    // 4x2, 8 bit, one strip per row, palette of 768 values (red 0..255*257, others constant)
    Bytes payload{1, 2, 3, 4, 5, 6, 7, 8}; // strips at 8, 12
    uint32_t mapAt = 8 + static_cast<uint32_t>(payload.size());
    uint32_t arrays = mapAt + 1536;
    for (int i = 0; i < 256; ++i) Put16(payload, i * 257);
    for (int i = 0; i < 256; ++i) Put16(payload, 65535);
    for (int i = 0; i < 256; ++i) Put16(payload, 257);
    Put32(payload, 8); // offsets array
    Put32(payload, 12);
    Put32(payload, 4); // byte counts array
    Put32(payload, 4);
    Bytes f = TiffFile(payload,
            {{0x100, 4, 1, 4}, {0x101, 4, 1, 2}, {0x102, 3, 1, 8}, {0x103, 3, 1, 1}, {0x111, 4, 2, arrays}, {0x115, 3, 1, 1},
                    {0x116, 4, 1, 1}, {0x117, 4, 2, arrays + 8}, {0x140, 3, 768, mapAt}});
    Image img;
    OBL_REQUIRE(DecodeI(f, img));
    OBL_CHECK_EQ(img.width, 4);
    OBL_CHECK_EQ(img.height, 2);
    OBL_CHECK_EQ(img.bpp, 8);
    OBL_CHECK((img.pixels == Bytes{1, 2, 3, 4, 5, 6, 7, 8}));
    OBL_REQUIRE(img.hasPalette());
    OBL_CHECK_EQ(img.palette[3 * 7 + 0], 7); // red channel: 7*257*255/65535 = 7
    OBL_CHECK_EQ(img.palette[3 * 7 + 1], 255);
    OBL_CHECK_EQ(img.palette[3 * 7 + 2], 1);
}

OBL_TEST_CASE("image.tiff_packbits_strips")
{
    // 4x3, rows per strip 2 -> strips of 2 and 1 rows. Rows: literal, repeat, mixed with a no-op byte.
    Bytes payload;
    uint32_t strip0 = 8;
    payload.insert(payload.end(), {3, 1, 2, 3, 4}); // row 0: literal of 4
    payload.insert(payload.end(), {0xfd, 9}); // row 1: 4 x 9
    uint32_t strip1 = 8 + static_cast<uint32_t>(payload.size());
    payload.insert(payload.end(), {0x80, 0xff, 7, 1, 5, 6}); // no-op, 2 x 7, literal 2
    uint32_t arrays = 8 + static_cast<uint32_t>(payload.size());
    Put32(payload, strip0);
    Put32(payload, strip1);
    Put32(payload, 7); // strip 0: 5 + 2 bytes
    Put32(payload, 6);
    Bytes f = TiffFile(payload,
            {{0x100, 4, 1, 4}, {0x101, 4, 1, 3}, {0x102, 3, 1, 8}, {0x103, 3, 1, 32773}, {0x111, 4, 2, arrays}, {0x115, 3, 1, 1},
                    {0x116, 4, 1, 2}, {0x117, 4, 2, arrays + 8}});
    Image img;
    OBL_REQUIRE(DecodeI(f, img));
    OBL_CHECK(!img.hasPalette());
    OBL_CHECK((img.pixels == Bytes{1, 2, 3, 4, 9, 9, 9, 9, 7, 7, 5, 6}));
}

OBL_TEST_CASE("image.tiff_tiles")
{
    // 4x4 8 bit in 2x2 tiles: tile (r,c) holds 4 consecutive bytes (two rows of two).
    Bytes payload;
    for (uint8_t i = 0; i < 16; ++i) payload.push_back(i); // tile k at 8 + 4k
    uint32_t arrays = 8 + 16;
    for (uint32_t k = 0; k < 4; ++k) Put32(payload, 8 + 4 * k);
    for (uint32_t k = 0; k < 4; ++k) Put32(payload, 4);
    Bytes f = TiffFile(payload,
            {{0x100, 4, 1, 4}, {0x101, 4, 1, 4}, {0x102, 3, 1, 8}, {0x103, 3, 1, 1}, {0x115, 3, 1, 1}, {0x142, 4, 1, 2}, {0x143, 4, 1, 2},
                    {0x144, 4, 4, arrays}, {0x145, 4, 4, arrays + 16}});
    Image img;
    OBL_REQUIRE(DecodeI(f, img));
    OBL_CHECK((img.pixels == Bytes{0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15}));
}

OBL_TEST_CASE("image.tiff_rgba_becomes_rgb")
{
    Bytes payload{1, 2, 3, 4, 5, 6, 7, 8};
    Bytes f = TiffFile(payload,
            {{0x100, 4, 1, 2}, {0x101, 4, 1, 1}, {0x102, 3, 1, 8}, {0x103, 3, 1, 1}, {0x111, 4, 1, 8}, {0x115, 3, 1, 4}, {0x116, 4, 1, 1},
                    {0x117, 4, 1, 8}});
    Image img;
    OBL_REQUIRE(DecodeI(f, img));
    OBL_CHECK_EQ(img.bpp, 24);
    OBL_CHECK((img.pixels == Bytes{1, 2, 3, 5, 6, 7}));
}

OBL_TEST_CASE("image.tiff_4bit_samples_expand_to_8bit_with_eight_palette_entries")
{
    Bytes payload{0x12, 0x34, 0x56, 0x78}; // 4x2 pixels, high nibble first
    uint32_t mapAt = 8 + 4;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) Put16(payload, static_cast<uint32_t>(i == 255 ? 65535 : i * 100));
    Bytes f = TiffFile(payload,
            {{0x100, 4, 1, 4}, {0x101, 4, 1, 2}, {0x102, 3, 1, 4}, {0x103, 3, 1, 1}, {0x111, 4, 1, 8}, {0x115, 3, 1, 1}, {0x116, 4, 1, 2},
                    {0x117, 4, 1, 4}, {0x140, 3, 768, mapAt}});
    Image img;
    OBL_REQUIRE(DecodeI(f, img));
    OBL_CHECK_EQ(img.bpp, 8);
    OBL_CHECK((img.pixels == Bytes{1, 2, 3, 4, 5, 6, 7, 8}));
    OBL_REQUIRE(img.hasPalette());
    OBL_CHECK_EQ(img.palette[3 * 7], TiffColorToByte(700));
    OBL_CHECK_EQ(img.palette[3 * 8], 0); // beyond the 8th entry
    OBL_CHECK_EQ(img.palette[3 * 255], 0);
}

OBL_TEST_CASE("image.tiff_unrecognized_compression_leaves_zero_pixels")
{
    Bytes payload{1, 2, 3, 4};
    Bytes f = TiffFile(payload,
            {{0x100, 4, 1, 2}, {0x101, 4, 1, 2}, {0x102, 3, 1, 8}, {0x103, 3, 1, 5}, {0x111, 4, 1, 8}, {0x115, 3, 1, 1}, {0x116, 4, 1, 2},
                    {0x117, 4, 1, 4}});
    Image img;
    OBL_REQUIRE(DecodeI(f, img));
    OBL_CHECK((img.pixels == Bytes{0, 0, 0, 0}));
}

OBL_TEST_CASE("image.tiff_rejects_bad_files")
{
    Image img;
    Bytes payload{1, 2, 3, 4};
    std::vector<Entry> ok = {{0x100, 4, 1, 2}, {0x101, 4, 1, 2}, {0x102, 3, 1, 8}, {0x103, 3, 1, 1}, {0x111, 4, 1, 8}, {0x115, 3, 1, 1},
            {0x116, 4, 1, 2}, {0x117, 4, 1, 4}};
    Bytes f = TiffFile(payload, ok);
    OBL_CHECK(DecodeI(f, img));
    Bytes bad = f;
    bad[0] = 'M';
    OBL_CHECK(!DecodeI(bad, img));
    bad = f;
    bad[2] = 43;
    OBL_CHECK(!DecodeI(bad, img));
    OBL_CHECK(!DecodeTiff(f.data(), 7, img));
    OBL_CHECK(!DecodeTiff(f.data(), f.size() - 40, img)); // IFD cut off
    ok[7].value = 5; // strip longer than the image
    OBL_CHECK(!DecodeI(TiffFile(payload, ok), img));
    ok[7].value = 4;
    ok[4].value = 4000; // strip outside the file
    OBL_CHECK(!DecodeI(TiffFile(payload, ok), img));
    ok[4].value = 8;
    ok[3].value = 32773;
    ok[6].value = 0;
    OBL_CHECK(!DecodeI(TiffFile(payload, ok), img));
}

namespace {

bool ReadFile(const std::filesystem::path& path, Bytes& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

const std::filesystem::path kData = std::filesystem::path(OBL_GAME_DIR) / "data";

} // namespace

OBL_TEST_CASE("data.tga_files_decode_to_their_header_size")
{
    if (!std::filesystem::exists(kData)) OBL_SKIP("run/ has no imported game data");
    int count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(kData)) {
        std::string ext = entry.path().extension().string();
        if (ext != ".tga") continue;
        Bytes file;
        OBL_REQUIRE(ReadFile(entry.path(), file));
        Image img;
        if (!DecodeT(file, img)) {
            OBL_CHECK(!"TGA decode failed");
            continue;
        }
        OBL_CHECK_EQ(img.width, file[12] | (file[13] << 8));
        OBL_CHECK_EQ(img.height, file[14] | (file[15] << 8));
        OBL_CHECK_EQ(img.pixels.size(), static_cast<size_t>(img.width) * img.height * (img.bpp / 8));
        OBL_CHECK(img.hasPalette()); // every shipped TGA is 8 bpp with a colour map
        ++count;
    }
    OBL_CHECK(count > 0);
}

OBL_TEST_CASE("data.tiff_fonts_and_haze_decode")
{
    if (!std::filesystem::exists(kData)) OBL_SKIP("run/ has no imported game data");
    int fonts = 0, haze = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(kData)) {
        std::string ext = entry.path().extension().string();
        if (ext != ".tif" && ext != ".tiff") continue;
        Bytes file;
        OBL_REQUIRE(ReadFile(entry.path(), file));
        Image img;
        if (!DecodeI(file, img)) {
            OBL_CHECK(!"TIFF decode failed");
            continue;
        }
        OBL_CHECK_EQ(img.bpp, 8);
        OBL_CHECK(img.hasPalette());
        OBL_CHECK_EQ(img.pixels.size(), static_cast<size_t>(img.width) * img.height);
        if (ext == ".tiff") {
            OBL_CHECK(img.width == 512 || img.width == 256);
            OBL_CHECK_EQ(img.width, img.height);
            ++fonts;
        } else {
            OBL_CHECK_EQ(img.width, 32);
            OBL_CHECK_EQ(img.height, 24);
            ++haze;
        }
    }
    OBL_CHECK(fonts > 0);
    OBL_CHECK(haze > 0);
}
