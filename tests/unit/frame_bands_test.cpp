// The handheld panel's change detection and pixel conversion (platform/frame_bands.h).

#include "unit_test.h"

#include "platform/frame_bands.h"

#include <vector>

using namespace obl::platform;

namespace {

constexpr size_t kPitch = 8; // four 16 bpp pixels a row

bool Is(const std::vector<RowRun>& runs, std::vector<RowRun> expected)
{
    if (runs.size() != expected.size()) return false;
    for (size_t i = 0; i < runs.size(); ++i)
        if (runs[i].begin != expected[i].begin || runs[i].end != expected[i].end) return false;
    return true;
}

} // namespace

OBL_TEST_CASE("frame_bands.same_frame_changes_nothing")
{
    const std::vector<uint8_t> frame(kPitch * 40, 7);
    OBL_CHECK(ChangedRows(frame.data(), frame.data(), kPitch, 40, false).empty());
}

OBL_TEST_CASE("frame_bands.a_changed_row_marks_its_band")
{
    const std::vector<uint8_t> before(kPitch * 40, 0);
    std::vector<uint8_t> after = before;
    after[kPitch * 20] = 1; // row 20 lies in the band of rows 16-31
    OBL_CHECK(Is(ChangedRows(before.data(), after.data(), kPitch, 40, false), {{16, 32}}));
    after[kPitch * 39] = 1; // the last band is rows 32-39 only; it joins the run before it
    OBL_CHECK(Is(ChangedRows(before.data(), after.data(), kPitch, 40, false), {{16, 40}}));
    after[0] = 1;
    after[kPitch * 39] = 0; // bands 0 and 1 join, band 2 is unchanged again
    OBL_CHECK(Is(ChangedRows(before.data(), after.data(), kPitch, 40, false), {{0, 32}}));
}

OBL_TEST_CASE("frame_bands.separate_bands_are_separate_runs")
{
    const std::vector<uint8_t> before(kPitch * 48, 0);
    std::vector<uint8_t> after = before;
    after[kPitch * 1] = 1;
    after[kPitch * 40] = 1;
    OBL_CHECK(Is(ChangedRows(before.data(), after.data(), kPitch, 48, false), {{0, 16}, {32, 48}}));
}

OBL_TEST_CASE("frame_bands.differs_sees_every_byte")
{
    // 8-byte words, then a tail: a difference in any byte counts, the first and the last included.
    for (size_t bytes : {size_t{1}, size_t{7}, size_t{8}, size_t{13}, size_t{64}}) {
        const std::vector<uint8_t> a(bytes, 3);
        OBL_CHECK(!Differs(a.data(), a.data(), bytes));
        for (size_t at : {size_t{0}, bytes / 2, bytes - 1}) {
            std::vector<uint8_t> b = a;
            b[at] = 4;
            OBL_CHECK(Differs(a.data(), b.data(), bytes));
        }
    }
}

OBL_TEST_CASE("frame_bands.all_is_one_run")
{
    const std::vector<uint8_t> frame(kPitch * 40, 0);
    OBL_CHECK(Is(ChangedRows(frame.data(), frame.data(), kPitch, 40, true), {{0, 40}}));
}

OBL_TEST_CASE("frame_bands.rgba_bytes_are_red_green_blue_alpha")
{
    const uint16_t rgb565[4] = {0xf800, 0x07e0, 0x001f, 0xffff};
    uint32_t out[4];
    RowToRgba(rgb565, out, 4, false);
    OBL_CHECK(out[0] == 0xff0000ffu && out[1] == 0xff00ff00u && out[2] == 0xffff0000u && out[3] == 0xffffffffu);
    const uint16_t rgb555[3] = {0x7c00, 0x03e0, 0x001f};
    RowToRgba(rgb555, out, 3, true);
    OBL_CHECK(out[0] == 0xff0000ffu && out[1] == 0xff00ff00u && out[2] == 0xffff0000u);
}
