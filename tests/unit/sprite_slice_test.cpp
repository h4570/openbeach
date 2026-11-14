// sprite_pack::SliceFrames: frames cut from one encoding of a whole animation equal an encoding of just those frames.

#include "unit_test.h"

#include "formats/sprite_pack.h"

#include <cstdint>
#include <vector>

namespace {

// EncodeFrames' contract: each frame's bytes appended in order, its offset is `base` plus the bytes before it in `data`.
void Encode(const std::vector<std::vector<uint8_t>>& frames, int first, int end, uint32_t base, std::vector<uint8_t>& data,
        std::vector<uint32_t>& offsets)
{
    for (int u = first; u < end; ++u) {
        offsets[static_cast<size_t>(u)] = base + static_cast<uint32_t>(data.size());
        data.insert(data.end(), frames[static_cast<size_t>(u)].begin(), frames[static_cast<size_t>(u)].end());
    }
}

} // namespace

OBL_TEST_CASE("sprite_slice.ranges_cut_from_the_whole_equal_their_own_encoding")
{
    // Frames of different lengths, an empty one included; three directions [0, 2), [2, 5), [5, 6).
    const std::vector<std::vector<uint8_t>> frames = {{1, 2, 3}, {4}, {}, {5, 6}, {7, 8, 9, 10}, {11}};
    const int first[] = {0, 2, 5, 6};
    const uint32_t base[] = {0, 1000, 5000};
    std::vector<uint8_t> whole;
    std::vector<uint32_t> wholeOffsets(frames.size());
    Encode(frames, 0, 6, 0, whole, wholeOffsets);
    for (int d = 0; d < 3; ++d) {
        std::vector<uint8_t> expected;
        std::vector<uint32_t> expectedOffsets(frames.size()), sliceOffsets(frames.size());
        Encode(frames, first[d], first[d + 1], base[d], expected, expectedOffsets);
        const std::vector<uint8_t> slice =
                obl::sprite_pack::SliceFrames(whole, wholeOffsets, first[d], first[d + 1], base[d], sliceOffsets);
        OBL_CHECK(slice == expected);
        for (int u = first[d]; u < first[d + 1]; ++u)
            OBL_CHECK_EQ(sliceOffsets[static_cast<size_t>(u)], expectedOffsets[static_cast<size_t>(u)]);
    }
}
