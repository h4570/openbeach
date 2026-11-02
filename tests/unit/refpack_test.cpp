// RefPack decoder (src/core/refpack.cpp): hand-encoded streams for every command class, rejection of
// malformed input, and a data case decoding the real scenario maps (docs/architecture/data-formats/converter/scenarios.md).

#include "unit_test.h"

#include "core/refpack.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace obl::refpack;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes Decoded(const Bytes& packed, size_t capacity, int64_t& produced)
{
    Bytes out(capacity);
    produced = Decode(packed.data(), out.data(), capacity);
    if (produced > 0) out.resize(static_cast<size_t>(produced));
    return out;
}

} // namespace

OBL_TEST_CASE("refpack.literals_and_stop")
{
    // 4 literals (0xE0), then stop with 1 trailing literal (0xFD).
    Bytes packed = {0x10, 0xFB, 0, 0, 5, 0xE0, 'a', 'b', 'c', 'd', 0xFD, 'e'};
    OBL_CHECK_EQ(DecodedSize(packed.data()), int64_t{5});
    int64_t n;
    Bytes out = Decoded(packed, 5, n);
    OBL_CHECK_EQ(n, int64_t{5});
    OBL_CHECK(out == Bytes({'a', 'b', 'c', 'd', 'e'}));
}

OBL_TEST_CASE("refpack.short_copy_overlaps_itself")
{
    // 2-byte command: 1 literal 'x', copy 7 from distance 1 -> a run of 8 x.
    Bytes packed = {0x10, 0xFB, 0, 0, 8, 0x11, 0x00, 'x', 0xFC};
    int64_t n;
    Bytes out = Decoded(packed, 8, n);
    OBL_CHECK_EQ(n, int64_t{8});
    OBL_CHECK(out == Bytes(8, 'x'));
}

OBL_TEST_CASE("refpack.medium_and_long_copies")
{
    // 8 literals "abcdefgh" (0xE1); 3-byte command: no literals, copy 4 from distance 8 -> "abcd";
    // 4-byte command: 2 literals "ZZ", copy 5 from distance 6 -> "abcdZ".
    Bytes packed = {
            0x10, 0xFB, 0, 0, 19, 0xE1, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 0x80, 0x00, 0x07, 0xC2, 0x00, 0x05, 0x00, 'Z', 'Z', 0xFC};
    int64_t n;
    Bytes out = Decoded(packed, 19, n);
    OBL_CHECK_EQ(n, int64_t{19});
    OBL_CHECK_EQ(std::string(out.begin(), out.end()), std::string("abcdefghabcdZZabcdZ"));
}

OBL_TEST_CASE("refpack.header_with_compressed_size")
{
    // 0x11FB carries a 3-byte compressed size before the decoded size.
    Bytes packed = {0x11, 0xFB, 0, 0, 12, 0, 0, 1, 0xFD, 'q'};
    OBL_CHECK_EQ(DecodedSize(packed.data()), int64_t{1});
    int64_t n;
    Bytes out = Decoded(packed, 1, n);
    OBL_CHECK_EQ(n, int64_t{1});
    OBL_CHECK(out == Bytes({'q'}));
}

OBL_TEST_CASE("refpack.rejects_bad_input")
{
    Bytes notPacked = {0x12, 0x34, 0, 0, 1};
    OBL_CHECK_EQ(DecodedSize(notPacked.data()), int64_t{-1});
    OBL_CHECK_EQ(DecodedSize(nullptr), int64_t{-1});
    uint8_t dst[8];
    OBL_CHECK_EQ(Decode(nullptr, dst, sizeof(dst)), int64_t{-1});

    // Output larger than the buffer, literal run and copy variants.
    Bytes literals = {0x10, 0xFB, 0, 0, 4, 0xE0, 'a', 'b', 'c', 'd', 0xFC};
    OBL_CHECK_EQ(Decode(literals.data(), dst, 3), int64_t{-1});
    Bytes copy = {0x10, 0xFB, 0, 0, 8, 0x11, 0x00, 'x', 0xFC};
    OBL_CHECK_EQ(Decode(copy.data(), dst, 7), int64_t{-1});
    // Copy distance reaching before the start of the output.
    Bytes distance = {0x10, 0xFB, 0, 0, 4, 0x01, 0x05, 'x', 0xFC};
    OBL_CHECK_EQ(Decode(distance.data(), dst, sizeof(dst)), int64_t{-1});
}

// Oracle: the maps are the extractor's input (tools/extract_maps.py); each of the 1024 blocks must decode to
// exactly the size its header announces, which is 2 planes of 128x128 16-bit pixels.
OBL_TEST_CASE("data.refpack_decodes_scenario_maps")
{
    const std::filesystem::path scenarios = std::filesystem::path(OBL_GAME_DIR) / "Data" / "Scenario";
    if (!std::filesystem::exists(scenarios)) OBL_SKIP("run/ has no imported game data");
    constexpr int kBlocks = 32 * 32;
    constexpr size_t kBlockBytes = 128 * 128 * 2 * 2;
    int maps = 0;
    for (const auto& dir : std::filesystem::directory_iterator(scenarios)) {
        if (!dir.is_directory()) continue;
        for (const auto& file : std::filesystem::directory_iterator(dir)) {
            if (file.path().extension() != ".biz") continue;
            std::ifstream in(file.path(), std::ios::binary);
            // Bulk read: a per-byte istreambuf_iterator over ~290 MB of maps dominates the Debug run time.
            Bytes data(static_cast<size_t>(file.file_size()));
            in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            OBL_REQUIRE(data.size() >= size_t{kBlocks} * 8);
            std::vector<uint32_t> table(kBlocks * 2);
            std::memcpy(table.data(), data.data(), table.size() * 4);
            Bytes out(kBlockBytes);
            for (int k = 0; k < kBlocks; ++k) {
                OBL_REQUIRE(size_t{table[k]} < data.size());
                const uint8_t* src = data.data() + table[k];
                OBL_CHECK_EQ(DecodedSize(src), int64_t(kBlockBytes));
                OBL_CHECK_EQ(Decode(src, out.data(), out.size()), int64_t(kBlockBytes));
            }
            ++maps;
        }
    }
    if (maps == 0) OBL_SKIP("no .biz maps in run/Data/Scenario");
}
