// The .cur decoder behind the SDL cursors (core/cursor_file.h): synthetic files only, no game data.

#include "unit_test.h"

#include "core/cursor_file.h"

#include <vector>

using namespace obl;

namespace {

using Bytes = std::vector<uint8_t>;

void Put16(Bytes& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void Put32(Bytes& out, uint32_t v)
{
    Put16(out, v & 0xffff);
    Put16(out, v >> 16);
}

// A 4x2 cursor: one directory entry, BITMAPINFOHEADER, `palette`, then the bottom-up XOR rows and AND rows (4 bytes each).
Bytes Cursor(int bpp, const Bytes& palette, const Bytes& xorRows, const Bytes& andRows, int hotX = 3, int hotY = 1)
{
    Bytes dib;
    Put32(dib, 40);
    Put32(dib, 4);
    Put32(dib, 4); // double height
    Put16(dib, 1);
    Put16(dib, static_cast<uint32_t>(bpp));
    for (int i = 0; i < 6; ++i) Put32(dib, 0);
    dib.insert(dib.end(), palette.begin(), palette.end());
    dib.insert(dib.end(), xorRows.begin(), xorRows.end());
    dib.insert(dib.end(), andRows.begin(), andRows.end());
    Bytes file;
    Put16(file, 0);
    Put16(file, 2);
    Put16(file, 1);
    file.insert(file.end(), {4, 2, 0, 0});
    Put16(file, static_cast<uint32_t>(hotX));
    Put16(file, static_cast<uint32_t>(hotY));
    Put32(file, static_cast<uint32_t>(dib.size()));
    Put32(file, 22);
    file.insert(file.end(), dib.begin(), dib.end());
    return file;
}

const Bytes kMono = {0, 0, 0, 0, 0xff, 0xff, 0xff, 0};

} // namespace

OBL_TEST_CASE("cursor_file: monochrome masks map to black, white, transparent and inverted-as-black")
{
    // Bottom row first. Top row XOR 0101, AND 0011 -> black, white, transparent, inverted. Bottom row all white, opaque.
    const Bytes file = Cursor(1, kMono, {0xf0, 0, 0, 0, 0x50, 0, 0, 0}, {0x00, 0, 0, 0, 0x30, 0, 0, 0});
    cursor_file::Image image;
    OBL_REQUIRE(cursor_file::Decode(file.data(), file.size(), image));
    OBL_CHECK(image.width == 4);
    OBL_CHECK(image.height == 2);
    OBL_CHECK(image.hotX == 3);
    OBL_CHECK(image.hotY == 1);
    OBL_REQUIRE(image.argb.size() == 8);
    OBL_CHECK(image.argb[0] == 0xff000000u);
    OBL_CHECK(image.argb[1] == 0xffffffffu);
    OBL_CHECK(image.argb[2] == 0u);
    OBL_CHECK(image.argb[3] == 0xff000000u);
    for (int x = 4; x < 8; ++x) OBL_CHECK(image.argb[x] == 0xffffffffu);
}

OBL_TEST_CASE("cursor_file: 32 bpp uses its alpha channel when present")
{
    Bytes xorRows;
    for (int i = 0; i < 8; ++i) xorRows.insert(xorRows.end(), {0x30, 0x20, 0x10, static_cast<uint8_t>(i * 30)});
    Bytes andRows(8, 0);
    const Bytes file = Cursor(32, {}, xorRows, andRows);
    cursor_file::Image image;
    OBL_REQUIRE(cursor_file::Decode(file.data(), file.size(), image));
    OBL_CHECK(image.argb[0] == ((4u * 30) << 24 | 0x102030u)); // top row = second stored row
    OBL_CHECK(image.argb[4] == 0u); // alpha 0 is transparent
    OBL_CHECK(image.argb[5] == (30u << 24 | 0x102030u));
}

OBL_TEST_CASE("cursor_file: truncated, icon and unsupported files are rejected")
{
    Bytes file = Cursor(1, kMono, Bytes(8, 0), Bytes(8, 0));
    cursor_file::Image image;
    for (size_t size : {size_t{0}, size_t{21}, file.size() - 1}) OBL_CHECK(!cursor_file::Decode(file.data(), size, image));
    Bytes icon = file;
    icon[2] = 1; // type 1 = icon
    OBL_CHECK(!cursor_file::Decode(icon.data(), icon.size(), image));
    Bytes compressed = file;
    compressed[22 + 16] = 1; // BI_RLE8
    OBL_CHECK(!cursor_file::Decode(compressed.data(), compressed.size(), image));
    Bytes png = file;
    png[22 + 0] = 0x89; // a PNG signature where the DIB header size is
    OBL_CHECK(!cursor_file::Decode(png.data(), png.size(), image));
}

OBL_TEST_CASE("cursor_file: an icon file decodes its richest entry without a hot spot")
{
    // Two entries pointing at DIBs: a 1 bpp one (planes 1, bits 1) and a 32 bpp one (planes 1, bits 32), same size.
    const Bytes mono = Cursor(1, kMono, Bytes(8, 0xff), Bytes(8, 0));
    Bytes xorRows;
    for (int i = 0; i < 8; ++i) xorRows.insert(xorRows.end(), {0x30, 0x20, 0x10, 0xff});
    const Bytes color = Cursor(32, {}, xorRows, Bytes(8, 0));
    const Bytes monoDib(mono.begin() + 22, mono.end()), colorDib(color.begin() + 22, color.end());
    Bytes file;
    Put16(file, 0);
    Put16(file, 1);
    Put16(file, 2);
    file.insert(file.end(), {4, 2, 0, 0});
    Put16(file, 1);
    Put16(file, 1);
    Put32(file, static_cast<uint32_t>(monoDib.size()));
    Put32(file, 38);
    file.insert(file.end(), {4, 2, 0, 0});
    Put16(file, 1);
    Put16(file, 32);
    Put32(file, static_cast<uint32_t>(colorDib.size()));
    Put32(file, static_cast<uint32_t>(38 + monoDib.size()));
    file.insert(file.end(), monoDib.begin(), monoDib.end());
    file.insert(file.end(), colorDib.begin(), colorDib.end());
    cursor_file::Image image;
    OBL_REQUIRE(cursor_file::DecodeIcon(file.data(), file.size(), image));
    OBL_CHECK(image.hotX == 0);
    OBL_CHECK(image.hotY == 0);
    OBL_CHECK(image.argb[0] == 0xff102030u);
    OBL_CHECK(!cursor_file::Decode(file.data(), file.size(), image)); // a cursor decoder refuses icons
    OBL_CHECK(!cursor_file::DecodeIcon(file.data(), file.size() - 1, image));
}
