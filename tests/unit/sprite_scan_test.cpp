// Frame scans of the SpriteInstance cursor-hit code (src/game/ui/sprite_scan.h): FindFirstOpaque and HitTest over
// hand-encoded and random RLE frames (formats/blsprite.md).

#include "unit_test.h"

#include "game/ui/sprite_scan.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace obl::sprite_scan;

namespace {

// One row as runs: a positive length is that many opaque pixels (with palette bytes unless the frame is a mask),
// a negative one that many transparent pixels.
using Row = std::vector<int>;

std::vector<uint8_t> Encode(const std::vector<Row>& rows, bool mask)
{
    std::vector<uint8_t> out;
    uint8_t palette = 1;
    for (const Row& row : rows)
        for (int run : row) {
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>(run)));
            if (run > 0 && !mask)
                for (int i = 0; i < run; ++i) out.push_back(palette++ | 0x80);
        }
    return out;
}

// What the hit test should answer, from a decoded bitmap: column x of the call is pixel x - 1.
std::vector<std::vector<bool>> Decode(const std::vector<Row>& rows)
{
    std::vector<std::vector<bool>> bitmap;
    for (const Row& row : rows) {
        bitmap.emplace_back();
        for (int run : row) bitmap.back().insert(bitmap.back().end(), static_cast<size_t>(run < 0 ? -run : run), run > 0);
    }
    return bitmap;
}

uint32_t g_state = 5;
int Random(int bound)
{
    g_state = g_state * 1664525u + 1013904223u;
    return static_cast<int>((g_state >> 8) % static_cast<uint32_t>(bound));
}

std::vector<Row> RandomRows(int width, int height)
{
    std::vector<Row> rows;
    for (int y = 0; y < height; ++y) {
        Row row;
        for (int x = 0; x < width;) {
            const int n = 1 + Random(std::min(width - x, 9));
            row.push_back(Random(3) ? n : -n);
            x += n;
        }
        rows.push_back(row);
    }
    return rows;
}

} // namespace

OBL_TEST_CASE("sprite_scan.hit_test_reads_the_pixel_left_of_x")
{
    // Row 0: 3 transparent, 4 opaque, 2 transparent (width 9); row 1: 9 opaque.
    const std::vector<Row> rows = {{-3, 4, -2}, {9}};
    for (bool mask : {false, true}) {
        const auto frame = Encode(rows, mask);
        OBL_CHECK(!HitTest(frame.data(), 9, 0, 0, mask)); // x = 0 never hits
        OBL_CHECK(!HitTest(frame.data(), 9, 1, 0, mask)); // pixel 0
        OBL_CHECK(!HitTest(frame.data(), 9, 3, 0, mask)); // pixel 2: last transparent
        OBL_CHECK(HitTest(frame.data(), 9, 4, 0, mask)); // pixel 3: first opaque
        OBL_CHECK(HitTest(frame.data(), 9, 7, 0, mask)); // pixel 6: last opaque
        OBL_CHECK(!HitTest(frame.data(), 9, 8, 0, mask)); // pixel 7
        OBL_CHECK(!HitTest(frame.data(), 9, 9, 0, mask));
        OBL_CHECK(HitTest(frame.data(), 9, 1, 1, mask));
        OBL_CHECK(HitTest(frame.data(), 9, 9, 1, mask));
    }
}

OBL_TEST_CASE("sprite_scan.hit_test_matches_decoded_bitmap")
{
    for (int round = 0; round < 200; ++round) {
        const int width = 1 + Random(40), height = 1 + Random(12);
        const auto rows = RandomRows(width, height);
        const auto bitmap = Decode(rows);
        for (bool mask : {false, true}) {
            const auto frame = Encode(rows, mask);
            for (int y = 0; y < height; ++y)
                for (int x = 1; x <= width; ++x)
                    if (HitTest(frame.data(), width, x, y, mask) != bitmap[y][x - 1]) {
                        OBL_CHECK(false);
                        return;
                    }
        }
    }
}

OBL_TEST_CASE("sprite_scan.hit_test_negative_y_skips_no_rows")
{
    const auto frame = Encode({{4}, {-4}}, false);
    OBL_CHECK(HitTest(frame.data(), 4, 2, -5, false));
    OBL_CHECK(HitTest(frame.data(), 4, 2, 0, false));
    OBL_CHECK(!HitTest(frame.data(), 4, 2, 1, false));
}

OBL_TEST_CASE("sprite_scan.hit_test_column_past_the_width_spills_into_the_next_row")
{
    // Row 0: 2 transparent, 2 opaque; row 1: 4 opaque. Column 7 is two pixels into row 1 when row 0 is not skipped.
    const auto frame = Encode({{-2, 2}, {4}}, false);
    OBL_CHECK(HitTest(frame.data(), 4, 6, 0, false)); // 6th pixel from the row start: row 1's pixel 1
    OBL_CHECK(HitTest(frame.data(), 4, 8, 0, false));
}

OBL_TEST_CASE("sprite_scan.hit_test_control_minus_128_is_128_transparent")
{
    const std::vector<uint8_t> frame = {0x80, 0x00};
    OBL_CHECK(!HitTest(frame.data(), 128, 128, 0, true));
}

OBL_TEST_CASE("sprite_scan.hit_test_mask_frames_carry_no_palette_bytes")
{
    // The same silhouette hit through a frame with and without palette bytes; payload bytes that look like
    // controls (negative) must be skipped when present and not skipped when the frame is a mask.
    const std::vector<Row> rows = {{2, -3, 2}};
    const auto full = Encode(rows, false);
    const auto mask = Encode(rows, true);
    OBL_CHECK_EQ(full.size(), mask.size() + 4);
    for (int x = 1; x <= 7; ++x) OBL_CHECK_EQ(HitTest(full.data(), 7, x, 0, false), HitTest(mask.data(), 7, x, 0, true));
}

OBL_TEST_CASE("sprite_scan.find_first_opaque_reports_the_end_of_the_first_run_followed_by_a_nonzero_byte")
{
    // Row 0 is transparent; row 1: 2 transparent, 3 opaque, then the next control byte (-2) is non-zero.
    const auto frame = Encode({{-5}, {-2, 3, -2}}, false);
    Opaque found{-1, -1};
    OBL_REQUIRE(FindFirstOpaque(frame.data(), 5, 2, &found));
    OBL_CHECK_EQ(found.x, 5);
    OBL_CHECK_EQ(found.y, 1);
}

OBL_TEST_CASE("sprite_scan.find_first_opaque_leaves_the_result_when_nothing_matches")
{
    const auto transparent = Encode({{-4}, {-4}}, false);
    Opaque found{7, 9};
    OBL_CHECK(!FindFirstOpaque(transparent.data(), 4, 2, &found));
    OBL_CHECK_EQ(found.x, 7);
    OBL_CHECK_EQ(found.y, 9);
    // Literal runs followed only by zero bytes (the frame ends in padding zeros) never match.
    std::vector<uint8_t> padded = {2, 0x81, 0x82, 0, 0, 0};
    OBL_CHECK(!FindFirstOpaque(padded.data(), 2, 1, &found));
    OBL_CHECK(!FindFirstOpaque(padded.data(), 0, 4, &found));
    OBL_CHECK(!FindFirstOpaque(padded.data(), 4, 0, &found));
    OBL_CHECK_EQ(found.x, 7);
}

OBL_TEST_CASE("sprite_scan.find_first_opaque_on_random_frames_is_the_first_literal_followed_by_a_nonzero_byte")
{
    for (int round = 0; round < 200; ++round) {
        const int width = 1 + Random(30), height = 1 + Random(8);
        const auto rows = RandomRows(width, height);
        auto frame = Encode(rows, false);
        frame.push_back(0); // what follows the frame
        // Reference: walk the rows as runs, remembering the control byte that follows each literal run.
        bool expectFound = false;
        int expectX = 0, expectY = 0;
        for (int y = 0; y < height && !expectFound; ++y) {
            int column = 0;
            for (size_t i = 0; i < rows[y].size() && !expectFound; ++i) {
                column += rows[y][i] < 0 ? -rows[y][i] : rows[y][i];
                if (rows[y][i] > 0) {
                    const bool lastOfFrame = y == height - 1 && i + 1 == rows[y].size();
                    // The following control byte is never zero in these frames (runs are 1..9); only the pad is.
                    if (!lastOfFrame) {
                        expectFound = true;
                        expectX = column;
                        expectY = y;
                    }
                }
            }
        }
        Opaque found{-1, -1};
        const bool got = FindFirstOpaque(frame.data(), width, height, &found);
        OBL_CHECK_EQ(got, expectFound);
        if (got && expectFound) {
            OBL_CHECK_EQ(found.x, expectX);
            OBL_CHECK_EQ(found.y, expectY);
        }
    }
}
