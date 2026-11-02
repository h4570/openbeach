
#include "unit_test.h"

#include "game/map/water_anim.h"
#include "temp_dir.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace obl;
using namespace obl::water_anim;

namespace {

std::unique_ptr<ui::WaterAnim> NewAnim()
{
    auto anim = std::make_unique<ui::WaterAnim>();
    std::memset(anim.get(), 0xcd, sizeof(*anim));
    Construct(*anim);
    return anim;
}

const Allocator kHeap{[](size_t size) -> void* { return std::malloc(size ? size : 1); }, [](void* memory) { std::free(memory); }};

// A water file: frame f is `runs[f]` (signed byte runs).
void WriteFile(const std::filesystem::path& path, const std::vector<std::vector<int8_t>>& frames)
{
    std::vector<int8_t> data;
    uint32_t offsets[kFrames] = {}, counts[kFrames] = {};
    for (size_t f = 0; f < frames.size() && f < kFrames; ++f) {
        offsets[f] = static_cast<uint32_t>(data.size());
        counts[f] = static_cast<uint32_t>(frames[f].size());
        data.insert(data.end(), frames[f].begin(), frames[f].end());
    }
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    const uint32_t size = static_cast<uint32_t>(data.size());
    std::fwrite(&size, 4, 1, file);
    std::fwrite(offsets, 4, kFrames, file);
    std::fwrite(counts, 4, kFrames, file);
    std::fwrite(data.data(), 1, data.size(), file);
    std::fclose(file);
}

void Free(ui::WaterAnim& anim)
{
    std::free(anim.m_data);
    anim.m_data = nullptr;
}

} // namespace

OBL_TEST_CASE("water_anim.construct_sets_four_fields_and_leaves_the_rest")
{
    auto anim = NewAnim();
    OBL_CHECK(anim->m_data == nullptr);
    OBL_CHECK_EQ(anim->m_lastFrame, -1);
    OBL_CHECK_EQ(anim->m_loaded, 0);
    OBL_CHECK_EQ(anim->m_enabled, 1);
    OBL_CHECK_EQ(anim->m_mask[0], 0xcd);
    OBL_CHECK_EQ(anim->m_frameRuns[3], static_cast<int>(0xcdcdcdcd));
}

OBL_TEST_CASE("water_anim.frame_is_seven_and_a_half_per_second_modulo_fifteen")
{
    OBL_CHECK_EQ(FrameAt(0.0f), 0);
    OBL_CHECK_EQ(FrameAt(0.2f), 1); // 1.5 -> 1
    OBL_CHECK_EQ(FrameAt(1.0f), 7); // 7.5 -> 7
    OBL_CHECK_EQ(FrameAt(2.0f), 0); // 15 -> 0
    OBL_CHECK_EQ(FrameAt(2.1f), 0); // 15.75 -> 15 % 15
    OBL_CHECK_EQ(FrameAt(2.2f), 1); // 16.5 -> 16 % 15
    OBL_CHECK(FrameAt(-1.0f) < 0);
    OBL_CHECK(FrameAt(1e30f) < 0); // the "integer indefinite" result
}

OBL_TEST_CASE("water_anim.get_mask_picks_the_quadrant_from_the_low_bits")
{
    auto anim = NewAnim();
    OBL_CHECK(GetMask(*anim, 0, 0) == nullptr); // not loaded
    anim->m_loaded = 1;
    OBL_CHECK(GetMask(*anim, 0, 0) == anim->m_mask);
    OBL_CHECK(GetMask(*anim, 1, 0) == anim->m_mask + 0x4000);
    OBL_CHECK(GetMask(*anim, 2, 2) == anim->m_mask);
    OBL_CHECK(GetMask(*anim, 3, 1) == anim->m_mask + 0xc000);
    OBL_CHECK(GetMask(*anim, 0, -1) == anim->m_mask + 0x8000); // the row is a signed byte; only its bit 0 counts
    SetEnabled(*anim, 0);
    OBL_CHECK(GetMask(*anim, 1, 1) == nullptr);
}

OBL_TEST_CASE("water_anim.update_decodes_runs_of_ones_and_zeros")
{
    auto anim = NewAnim();
    const obl::test::TempPath file("obl_water_anim_test", ".bin");
    const auto& path = file.path;
    WriteFile(path, {{3, -2, 5}, {1, 1, -127, 127}});
    OBL_CHECK(Load(*anim, path.string().c_str(), kHeap));
    OBL_CHECK_EQ(anim->m_loaded, 1);
    OBL_CHECK(Update(*anim, 0.0f));
    const uint8_t expected[] = {1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
    OBL_CHECK(std::memcmp(anim->m_mask, expected, sizeof(expected)) == 0);
    OBL_CHECK_EQ(anim->m_lastFrame, 0);
    OBL_CHECK(!Update(*anim, 0.05f)); // same frame: nothing rewritten
    OBL_CHECK(Update(*anim, 0.2f)); // frame 1
    OBL_CHECK_EQ(anim->m_mask[0], 1);
    OBL_CHECK_EQ(anim->m_mask[1], 1);
    OBL_CHECK_EQ(anim->m_mask[2], 0);
    OBL_CHECK_EQ(anim->m_mask[128], 0);
    OBL_CHECK_EQ(anim->m_mask[129], 1);
    OBL_CHECK_EQ(anim->m_mask[129 + 126], 1);
    Free(*anim);
}

OBL_TEST_CASE("water_anim.update_does_nothing_while_not_loaded_or_for_negative_times")
{
    auto anim = NewAnim();
    OBL_CHECK(!Update(*anim, 1.0f));
    OBL_CHECK_EQ(anim->m_lastFrame, -1); // not even the bookkeeping
    const obl::test::TempPath file("obl_water_anim_test", ".bin");
    const auto& path = file.path;
    WriteFile(path, {{1}});
    OBL_CHECK(Load(*anim, path.string().c_str(), kHeap));
    OBL_CHECK(!Update(*anim, -3.0f));
    OBL_CHECK_EQ(anim->m_lastFrame, -1);
    Free(*anim);
}

OBL_TEST_CASE("water_anim.load_failure_clears_the_loaded_flag_and_reload_keeps_the_frame")
{
    auto anim = NewAnim();
    const obl::test::TempPath file("obl_water_anim_test", ".bin");
    const auto& path = file.path;
    WriteFile(path, {{2}});
    uint32_t size = 0;
    OBL_CHECK(Load(*anim, path.string().c_str(), kHeap, &size));
    OBL_CHECK_EQ(size, 1u);
    OBL_CHECK(Update(*anim, 0.0f));
    OBL_CHECK_EQ(anim->m_lastFrame, 0);
    OBL_CHECK(Load(*anim, path.string().c_str(), kHeap));
    OBL_CHECK_EQ(anim->m_lastFrame, 0); // not reset
    OBL_CHECK(!Load(*anim, (path.string() + ".missing").c_str(), kHeap));
    OBL_CHECK_EQ(anim->m_loaded, 0);
    Free(*anim);
}

OBL_TEST_CASE("water_anim.start_rect_defaults_to_the_whole_image")
{
    const uint32_t whole[4] = {4, 6, 0, 0};
    SourceRect r = StartRect(whole, 128, 128);
    OBL_CHECK_EQ(r.left, 4);
    OBL_CHECK_EQ(r.right, 132);
    OBL_CHECK_EQ(r.bottom, 134);
    const uint32_t half[4] = {4, 6, 10, 0}; // either edge 0 selects the default
    OBL_CHECK_EQ(StartRect(half, 128, 128).right, 132);
    const uint32_t given[4] = {4, 6, 20, 30};
    r = StartRect(given, 128, 128);
    OBL_CHECK_EQ(r.right, 20);
    OBL_CHECK_EQ(r.bottom, 30);
}

OBL_TEST_CASE("water_anim.block_mask_only_for_the_exact_flag_one")
{
    auto anim = NewAnim();
    anim->m_loaded = 1;
    OBL_CHECK(BlockMask(anim.get(), 1, 1, 1) == anim->m_mask + 0xc000);
    OBL_CHECK(BlockMask(anim.get(), 1, 1, 0) == nullptr);
    OBL_CHECK(BlockMask(anim.get(), 1, 1, 2) == nullptr);
    OBL_CHECK(BlockMask(nullptr, 1, 1, 1) == nullptr);
}

OBL_TEST_CASE("water_anim.load_sheet_reproduces_the_sheet_masks")
{
    std::vector<uint8_t> sheet(kSheetWidth * kSheetHeight, 0);
    // Frame 2: long on stretch (more than one 127 run) in mask 1, a lone pixel in mask 3; frame 5 all on.
    for (int x = 0; x < 128; ++x)
        for (int y = 0; y < 128; ++y) sheet[(128 + y) * kSheetWidth + 2 * 128 + x] = y < 3 ? 255 : 0;
    sheet[(3 * 128 + 127) * kSheetWidth + 2 * 128 + 127] = 255;
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 128; ++x) sheet[y * kSheetWidth + 5 * 128 + x] = 255;
    auto anim = NewAnim();
    LoadSheet(*anim, sheet.data(), kHeap);
    OBL_CHECK(anim->m_loaded == 1);
    for (int frame : {2, 5, 0}) {
        OBL_REQUIRE(Update(*anim, static_cast<float>((frame + 0.25) / 7.5)));
        for (size_t pixel = 0; pixel < 65536; ++pixel) {
            const size_t mask = pixel / 16384, within = pixel % 16384;
            const uint8_t expected = sheet[(mask * 128 + within / 128) * kSheetWidth + frame * 128 + within % 128] ? 1 : 0;
            if (anim->m_mask[pixel] != expected) {
                OBL_CHECK_EQ(anim->m_mask[pixel], expected);
                break;
            }
        }
    }
    Free(*anim);
}
