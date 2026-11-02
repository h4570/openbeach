

#include "unit_test.h"

#include "game/ui/map_blocks.h"

#include <cstring>
#include <vector>

using namespace obl::map_blocks;

OBL_TEST_CASE("map_blocks.span_is_the_blocks_slice_of_the_file")
{
    const BlockSpan origin = SpanOf(32, 128, 0, 0);
    OBL_CHECK_EQ(origin.offset, 0u);
    OBL_CHECK_EQ(origin.size, 128u * 128u * 4u);
    const BlockSpan second = SpanOf(32, 128, 1, 0);
    OBL_CHECK_EQ(second.offset, 128u * 128u * 4u);
    const BlockSpan row = SpanOf(32, 128, 3, 2);
    OBL_CHECK_EQ(row.offset, (2u * 32u + 3u) * 128u * 128u * 4u);
}

OBL_TEST_CASE("map_blocks.span_wraps_in_32_bits_like_the_exe")
{
    const BlockSpan wrapped = SpanOf(1 << 20, 128, 0, 1 << 10);
    OBL_CHECK_EQ(wrapped.offset, static_cast<uint32_t>((1u << 30) * 128u * 128u * 4u));
}

OBL_TEST_CASE("map_blocks.index_is_row_major")
{
    OBL_CHECK_EQ(BlockIndex(32, 5, 0), 5);
    OBL_CHECK_EQ(BlockIndex(32, 5, 2), 69);
}

OBL_TEST_CASE("map_blocks.map_pixels_per_screen_pixel_follows_the_zoom")
{
    OBL_CHECK_EQ(MapPixelsPerScreenPixel(0), 0.5f);
    OBL_CHECK_EQ(MapPixelsPerScreenPixel(1), 1.0f);
    OBL_CHECK_EQ(MapPixelsPerScreenPixel(2), 2.0f);
    OBL_CHECK_EQ(MapPixelsPerScreenPixel(3), 1.0f);
    OBL_CHECK_EQ(MapPixelsPerScreenPixel(-1), 1.0f);
}

OBL_TEST_CASE("map_blocks.view_range_covers_the_camera_to_the_far_corner")
{
    const BlockRange normal = ViewBlockRange(1500, 1201, 128, 1024, 768, 1);
    OBL_CHECK(normal == (BlockRange{11, 9, 19, 15}));

    const BlockRange zoomedIn = ViewBlockRange(1500, 1201, 128, 1024, 768, 0);
    OBL_CHECK(zoomedIn == (BlockRange{11, 9, 15, 12}));
    const BlockRange zoomedOut = ViewBlockRange(1500, 1201, 128, 1024, 768, 2);
    OBL_CHECK(zoomedOut == (BlockRange{11, 9, 27, 21}));
}

OBL_TEST_CASE("map_blocks.view_range_divides_toward_zero")
{
    const BlockRange range = ViewBlockRange(-100, -300, 128, 256, 256, 1);
    OBL_CHECK_EQ(range.x0, 0);
    OBL_CHECK_EQ(range.y0, -2);
    OBL_CHECK_EQ(range.x1, 1);
    OBL_CHECK_EQ(range.y1, 0);
}

OBL_TEST_CASE("map_blocks.outside_means_any_edge_crossed")
{
    const BlockRange range{2, 3, 5, 6};
    OBL_CHECK(!IsOutside(range, 2, 3));
    OBL_CHECK(!IsOutside(range, 5, 6));
    OBL_CHECK(IsOutside(range, 1, 4));
    OBL_CHECK(IsOutside(range, 6, 4));
    OBL_CHECK(IsOutside(range, 3, 2));
    OBL_CHECK(IsOutside(range, 3, 7));
}

OBL_TEST_CASE("map_blocks.interleave_puts_colour_first_and_depth_second")
{
    const uint16_t colour[] = {0x1111, 0x2222, 0x3333};
    const uint16_t depth[] = {0xaaaa, 0xbbbb, 0xcccc};
    uint16_t out[6] = {};
    InterleavePlanes(colour, depth, 3, out);
    const uint16_t expected[] = {0x1111, 0xaaaa, 0x2222, 0xbbbb, 0x3333, 0xcccc};
    OBL_CHECK_EQ(std::memcmp(out, expected, sizeof(expected)), 0);

    uint32_t first;
    std::memcpy(&first, out, 4);
    OBL_CHECK_EQ(first, 0xaaaa1111u);
}

OBL_TEST_CASE("map_blocks.fnv1a_matches_the_reference_vectors")
{
    OBL_CHECK_EQ(Fnv1a(kFnvOffset, "", 0), 0x811c9dc5u);
    OBL_CHECK_EQ(Fnv1a(kFnvOffset, "a", 1), 0xe40c292cu);
    OBL_CHECK_EQ(Fnv1a(kFnvOffset, "foobar", 6), 0xbf9cf968u);

    OBL_CHECK_EQ(Fnv1a(Fnv1a(kFnvOffset, "foo", 3), "bar", 3), Fnv1a(kFnvOffset, "foobar", 6));
}

OBL_TEST_CASE("map_blocks.column_row_interleaves_each_blocks_planes_at_fixed_strides")
{
    std::vector<uint8_t> column(2 * kColumnBlockBytes, 0xee);
    const auto put = [&](size_t byteOffset, uint16_t value) {
        column[byteOffset] = static_cast<uint8_t>(value & 0xff);
        column[byteOffset + 1] = static_cast<uint8_t>(value >> 8);
    };
    for (size_t block = 0; block < 2; ++block)
        for (size_t i = 0; i < 3; ++i) {
            put(block * kColumnBlockBytes + 2 * i, static_cast<uint16_t>(0x1000 * (block + 1) + i));
            put(block * kColumnBlockBytes + kColumnDepthOffset + 2 * i, static_cast<uint16_t>(0xa000 + 0x100 * block + i));
        }
    uint16_t out[12] = {};
    InterleaveColumnRow(column.data(), 2, 3, out);
    const uint16_t expected[] = {0x1000, 0xa000, 0x1001, 0xa001, 0x1002, 0xa002, 0x2000, 0xa100, 0x2001, 0xa101, 0x2002, 0xa102};
    OBL_CHECK_EQ(std::memcmp(out, expected, sizeof(expected)), 0);

    uint32_t second;
    std::memcpy(&second, out + 6, 4);
    OBL_CHECK_EQ(second, 0xa1002000u);
}

OBL_TEST_CASE("map_blocks.column_row_with_no_blocks_writes_nothing")
{
    const std::vector<uint8_t> column(kColumnBlockBytes, 1);
    uint16_t out[2] = {0x1234, 0x5678};
    InterleaveColumnRow(column.data(), 0, 1, out);
    OBL_CHECK_EQ(out[0], 0x1234);
    OBL_CHECK_EQ(out[1], 0x5678);
}

OBL_TEST_CASE("map_blocks.temp_map_row_geometry_is_the_exes")
{
    OBL_CHECK_EQ(128u * 128u * kTempMapRowBlocks * kBytesPerPixel, 128u * 128u * 0x80u);
    OBL_CHECK_EQ(kColumnDepthOffset, 128u * 128u * 2u);
}

OBL_TEST_CASE("map_blocks.temp_map_progress_is_the_done_fraction_of_the_fade")
{
    OBL_CHECK_EQ(TempMapProgress(8, 1024, 1.0f), 0.0078125f);
    OBL_CHECK_EQ(TempMapProgress(512, 1024, 0.5f), 0.25f);
    OBL_CHECK_EQ(TempMapProgress(1024, 1024, 0.75f), 0.75f);

    OBL_CHECK(TempMapProgress(0x80000000u, 1 << 30, 1.0f) > 0.0f);
}
