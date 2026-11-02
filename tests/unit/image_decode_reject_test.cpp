
#include "unit_test.h"

#include "core/image_decode.h"

#include <cstring>
#include <string>
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

Bytes TgaHeader(uint8_t type, uint16_t mapLength, uint8_t mapType, uint16_t w, uint16_t h, uint8_t bpp)
{
    Bytes out{0, mapType, type};
    Put16(out, 0);
    Put16(out, mapLength);
    out.push_back(mapType ? 24 : 0);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, w);
    Put16(out, h);
    out.push_back(bpp);
    out.push_back(0);
    return out;
}

struct Entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;
};

Bytes TiffFile(const Bytes& payload, const std::vector<Entry>& entries)
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

bool T(const Bytes& f, Image& out)
{
    return DecodeTga(f.data(), f.size(), out);
}
bool I(const Bytes& f, Image& out)
{
    return DecodeTiff(f.data(), f.size(), out);
}

// 2x2 8-bit TIFF, one strip at offset 8 (4 bytes), PackBits or raw; tags in the usual order.
std::vector<Entry> StripEntries(uint32_t compression, uint32_t stripBytes)
{
    return {{0x100, 4, 1, 2}, {0x101, 4, 1, 2}, {0x102, 3, 1, 8}, {0x103, 3, 1, compression}, {0x111, 4, 1, 8}, {0x115, 3, 1, 1},
            {0x116, 4, 1, 2}, {0x117, 4, 1, stripBytes}};
}

} // namespace

OBL_TEST_CASE("imagerej.tga_rle_truncated_or_overrunning_packets_fail")
{
    Image img;
    // 2x1 24 bpp RLE: repeat packet of 2 pixels, but the colour bytes are missing.
    Bytes f = TgaHeader(10, 0, 0, 2, 1, 24);
    f.push_back(0x81);
    OBL_CHECK(!T(f, img));
    f.insert(f.end(), {1, 2, 3});
    OBL_CHECK(T(f, img));
    // Packet longer than the pixels left in the image.
    Bytes over = TgaHeader(10, 0, 0, 2, 1, 24);
    over.insert(over.end(), {0x82, 1, 2, 3});
    OBL_CHECK(!T(over, img));
    // Raw packet whose pixels are cut off.
    Bytes raw = TgaHeader(10, 0, 0, 2, 1, 24);
    raw.insert(raw.end(), {0x01, 1, 2, 3, 4});
    OBL_CHECK(!T(raw, img));
    // Stream ends before all pixels were produced.
    Bytes shortStream = TgaHeader(10, 0, 0, 2, 1, 24);
    shortStream.insert(shortStream.end(), {0x00, 1, 2, 3});
    OBL_CHECK(!T(shortStream, img));
}

OBL_TEST_CASE("imagerej.tga_colour_map_problems_fail")
{
    Image img;
    // Colour map entry count above 256 is not a valid palette.
    Bytes big = TgaHeader(1, 300, 1, 1, 1, 8);
    big.resize(big.size() + 300 * 3 + 1, 0);
    OBL_CHECK(!T(big, img));
    // Colour map cut off by the end of the file.
    Bytes cut = TgaHeader(1, 3, 1, 1, 1, 8);
    cut.insert(cut.end(), {1, 2, 3});
    OBL_CHECK(!T(cut, img));
    Bytes noMap = TgaHeader(1, 0, 0, 1, 1, 24);
    noMap.insert(noMap.end(), {1, 2, 3});
    OBL_CHECK(!T(noMap, img));
}

OBL_TEST_CASE("imagerej.tga_tiny_inputs_fail")
{
    Image img;
    OBL_CHECK(!DecodeTga(nullptr, 0, img));
    Bytes h = TgaHeader(2, 0, 0, 1, 1, 24);
    OBL_CHECK(!DecodeTga(h.data(), 17, img));
    OBL_CHECK(!T(h, img)); // header only, no pixel bytes
}

OBL_TEST_CASE("imagerej.tiff_strip_array_problems_fail")
{
    Image img;
    Bytes payload{1, 2, 3, 4};
    OBL_CHECK(I(TiffFile(payload, StripEntries(1, 4)), img));
    // StripByteCounts value points outside the file (count 1 stores the value inline, so use two strips).
    std::vector<Entry> two = StripEntries(1, 4);
    two[4].count = 2;
    two[4].value = 5000; // offsets array outside the file
    OBL_CHECK(!I(TiffFile(payload, two), img));
    // Fewer strips announced than rows need is accepted, more offsets than counts are not read: both arrays must exist.
    two[4].value = 8;
    two[4].count = 4; // four strips announced, offsets array reads past the payload into the IFD
    OBL_CHECK(!I(TiffFile(payload, two), img));
}

OBL_TEST_CASE("imagerej.tiff_packbits_overruns_fail")
{
    Image img;
    // Literal run longer than the strip bytes that remain.
    Bytes lit{0x05, 1, 2, 3, 4};
    OBL_CHECK(!I(TiffFile(lit, StripEntries(32773, 5)), img));
    // Repeat run without its data byte.
    Bytes rep{0xfe};
    OBL_CHECK(!I(TiffFile(rep, StripEntries(32773, 1)), img));
    // Strip ends before the rows are complete.
    Bytes shortRow{0x00, 9};
    OBL_CHECK(!I(TiffFile(shortRow, StripEntries(32773, 2)), img));
    // Run longer than the whole pixel buffer.
    Bytes huge{0xe0, 7, 0xe0, 7};
    OBL_CHECK(!I(TiffFile(huge, StripEntries(32773, 4)), img));
    // A valid control stream still decodes: two rows of repeat-2.
    Bytes ok{0xff, 7, 0xff, 8};
    OBL_REQUIRE(I(TiffFile(ok, StripEntries(32773, 4)), img));
    OBL_CHECK((img.pixels == Bytes{7, 7, 8, 8}));
}

OBL_TEST_CASE("imagerej.tiff_tile_problems_fail")
{
    Image img;
    Bytes payload;
    for (uint8_t i = 0; i < 16; ++i) payload.push_back(i);
    uint32_t arrays = 8 + 16;
    for (uint32_t k = 0; k < 4; ++k) Put32(payload, 8 + 4 * k);
    for (uint32_t k = 0; k < 4; ++k) Put32(payload, 4);
    std::vector<Entry> tiles = {{0x100, 4, 1, 4}, {0x101, 4, 1, 4}, {0x102, 3, 1, 8}, {0x103, 3, 1, 1}, {0x115, 3, 1, 1}, {0x142, 4, 1, 2},
            {0x143, 4, 1, 2}, {0x144, 4, 4, arrays}, {0x145, 4, 4, arrays + 16}};
    OBL_CHECK(I(TiffFile(payload, tiles), img));
    // Tile data outside the file.
    Bytes far = payload;
    std::memset(far.data() + 16, 0xff, 4); // first offset becomes 0xffffffff
    OBL_CHECK(!I(TiffFile(far, tiles), img));
    // Neither strips nor a usable tile size.
    auto noSize = tiles;
    noSize[5].value = 0;
    OBL_CHECK(!I(TiffFile(payload, noSize), img));
    // Tile offsets array outside the file.
    auto outside = tiles;
    outside[7].value = 9000;
    OBL_CHECK(!I(TiffFile(payload, outside), img));
}

OBL_TEST_CASE("imagerej.tiff_colour_map_and_4bit_problems_fail")
{
    Image img;
    Bytes payload{1, 2, 3, 4};
    // Colour map beyond the end of the file.
    auto entries = StripEntries(1, 4);
    entries.push_back({0x140, 3, 768, 60000});
    OBL_CHECK(!I(TiffFile(payload, entries), img));
    // 4-bit samples need a palette.
    auto nibbles = StripEntries(1, 2);
    nibbles[2].value = 4;
    OBL_CHECK(!I(TiffFile(payload, nibbles), img));
    // Dimensions that do not fit.
    auto negative = StripEntries(1, 4);
    negative[0].value = 0x80000000u;
    OBL_CHECK(!I(TiffFile(payload, negative), img));
}

OBL_TEST_CASE("imagerej.tiff_short_strip_leaves_the_rest_of_the_image_zero")
{
    Image img;
    Bytes payload{0x12, 0x34};
    uint32_t mapAt = 8 + 2;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) Put16(payload, static_cast<uint32_t>(i * 257));
    std::vector<Entry> entries = {{0x100, 4, 1, 4}, {0x101, 4, 1, 4}, {0x102, 3, 1, 4}, {0x103, 3, 1, 1}, {0x111, 4, 1, 8},
            {0x115, 3, 1, 1}, {0x116, 4, 1, 4}, {0x117, 4, 1, 2}, {0x140, 3, 768, mapAt}};
    OBL_REQUIRE(I(TiffFile(payload, entries), img));
    OBL_CHECK_EQ(img.bpp, 8);
    OBL_CHECK_EQ(img.pixels.size(), 16u);
    OBL_CHECK((Bytes(img.pixels.begin(), img.pixels.begin() + 6) == Bytes{1, 2, 3, 4, 0, 0}));
}

OBL_TEST_CASE("imagerej.equal_reports_each_field")
{
    Image a;
    a.width = 2;
    a.height = 1;
    a.bpp = 8;
    a.pixels = {1, 2};
    Image b = a;
    OBL_CHECK(Equal(a, b));
    char diff[96] = "";
    b.width = 3;
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("width 2 vs 3"));
    b = a;
    b.height = 4;
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("height 1 vs 4"));
    b = a;
    b.bpp = 24;
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("bpp 8 vs 24"));
    b = a;
    b.palette.assign(768, 0);
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("palette size 0 vs 768"));
    b = a;
    b.pixels = {1};
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("pixel bytes 2 vs 1"));
    a.palette.assign(768, 5);
    b = a;
    b.palette[10] = 9;
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("palette byte 10 differs (-4)"));
    b = a;
    b.pixels[1] = 7;
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("pixel byte 1 differs (-5)"));
    OBL_CHECK(!Equal(a, b)); // diagnostics are optional
}

OBL_TEST_CASE("imagerej.hash_covers_size_depth_palette_and_pixels")
{
    Image a;
    a.width = 2;
    a.height = 1;
    a.bpp = 8;
    a.pixels = {1, 2};
    uint64_t base = Hash(a);
    OBL_CHECK_EQ(Hash(a), base);
    Image b = a;
    b.pixels[0] = 9;
    OBL_CHECK(Hash(b) != base);
    b = a;
    b.width = 1;
    b.height = 2;
    OBL_CHECK(Hash(b) != base);
    b = a;
    b.palette.assign(768, 1);
    OBL_CHECK(Hash(b) != base);
    Image empty;
    OBL_CHECK(Hash(empty) != base);
}
