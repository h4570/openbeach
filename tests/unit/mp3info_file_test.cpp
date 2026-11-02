// Mp3Info edge paths not reached by mp3info_test.cpp: rejected headers, ScanFile and Equal diagnostics.

#include "unit_test.h"

#include "core/mp3info.h"
#include "temp_dir.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace obl::mp3;

namespace {

Mp3Info Parse(const std::vector<uint8_t>& bytes)
{
    Mp3Info info;
    Init(info);
    info.valid = 1;
    ParseFrames(info, bytes.data(), static_cast<uint32_t>(bytes.size()));
    return info;
}

void AppendFrame(std::vector<uint8_t>& out)
{
    out.insert(out.end(), {0xff, 0xfb, 0x90, 0x00});
    out.resize(out.size() + 417 - 4, 0x55);
}

} // namespace

OBL_TEST_CASE("mp3info.free_format_bitrate_header_is_junk")
{
    // Bitrate index 0 ("free format") gives length 0, below the accepted minimum: every byte counts as junk.
    std::vector<uint8_t> bytes = {0xff, 0xfb, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44};
    Mp3Info info = Parse(bytes);
    OBL_CHECK_EQ(info.frames, 0);
    OBL_CHECK_EQ(info.junkBytes, 5); // the last three bytes are carried, not counted
    OBL_CHECK_EQ(info.carryLen, 3);
}

OBL_TEST_CASE("mp3info.mpeg2_bitrate_index_15_overflows_to_junk")
{
    // MPEG2 table index 15 holds 44100 (a neighbouring table's word); 44100 * 72000 wraps in 32 bits to a
    // negative length, which the length check rejects.
    std::vector<uint8_t> bytes = {0xff, 0xf3, 0xf0, 0x00, 0, 0, 0, 0};
    Mp3Info info = Parse(bytes);
    OBL_CHECK_EQ(info.frames, 0);
    OBL_CHECK(info.junkBytes >= 1);
}

OBL_TEST_CASE("mp3info.mpeg2_header_uses_the_mpeg2_tables")
{
    // MPEG2, bitrate index 13 (144 kbit/s), rate index 2 (16 kHz): 144 * 72000 / 16000 = 648 bytes.
    std::vector<uint8_t> bytes = {0xff, 0xf3, 0xd8, 0x00};
    bytes.resize(700, 0);
    Mp3Info info = Parse(bytes);
    OBL_CHECK_EQ(info.frames, 1);
    OBL_CHECK_EQ(info.version, 2);
    OBL_CHECK_EQ(info.sampleRate, 16000);
    OBL_CHECK_EQ(info.frameBytes, 648);
}

OBL_TEST_CASE("mp3info.finish_rejects_more_junk_than_frames")
{
    std::vector<uint8_t> bytes(2000, 0x00);
    AppendFrame(bytes);
    Mp3Info info = Parse(bytes);
    Finish(info);
    OBL_CHECK_EQ(info.valid, 0);
    OBL_CHECK(!IsValidLayer3(info));
}

OBL_TEST_CASE("mp3info.finish_without_frames_is_invalid_not_a_crash")
{
    Mp3Info info;
    Init(info);
    info.valid = 1;
    Finish(info);
    OBL_CHECK_EQ(info.valid, 0);
}

OBL_TEST_CASE("mp3info.layer_other_than_three_is_not_layer3")
{
    std::vector<uint8_t> bytes;
    AppendFrame(bytes);
    bytes[1] = 0xfd; // layer bits 10 = Layer II
    Mp3Info info = Parse(bytes);
    Finish(info);
    OBL_CHECK_EQ(info.layer, 2);
    OBL_CHECK(!IsValidLayer3(info));
}

OBL_TEST_CASE("mp3info.scan_file_missing_leaves_info_untouched")
{
    Mp3Info info;
    Init(info);
    Mp3Info before = info;
    ScanFile(info, "/nonexistent-obl-dir", "none.mp3");
    OBL_CHECK(Equal(info, before));
    OBL_CHECK_EQ(info.valid, 0);
}

OBL_TEST_CASE("mp3info.scan_file_reads_in_chunks")
{
    std::vector<uint8_t> bytes;
    for (int i = 0; i < 30; ++i) // 12.5 KB: several 4 KB chunks with headers straddling them
        AppendFrame(bytes);
    const obl::test::TempPath tmp("obl_mp3_scan");
    const std::string dir = tmp.path.string();
    std::filesystem::create_directories(dir);
    std::string path = (std::filesystem::path(dir) / "scan.mp3").string();
    FILE* fp = std::fopen(path.c_str(), "wb");
    OBL_REQUIRE(fp != nullptr);
    std::fwrite(bytes.data(), 1, bytes.size(), fp);
    std::fclose(fp);

    Mp3Info info;
    Init(info);
    ScanFile(info, dir.c_str(), "scan.mp3");
    std::remove(path.c_str());
    std::filesystem::remove(dir);
    OBL_CHECK_EQ(info.frames, 30);
    OBL_CHECK_EQ(info.junkBytes, 0);
    OBL_CHECK_EQ(info.valid, 1);
    OBL_CHECK(IsValidLayer3(info));
    OBL_CHECK_EQ(info.lengthMs, 30 * 1152 / 44);

    Mp3Info whole = Parse(bytes);
    Finish(whole);
    OBL_CHECK(Equal(info, whole));
}

OBL_TEST_CASE("mp3info.equal_names_the_first_differing_field")
{
    Mp3Info a;
    Init(a);
    Mp3Info b = a;
    OBL_CHECK(Equal(a, b));
    b.lengthMs = 7;
    char diff[96] = "";
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("lengthMs: 0 vs 7"));
    OBL_CHECK(!Equal(a, b)); // no buffer: still false, nothing written

    b = a;
    a.carryLen = b.carryLen = 2;
    a.carry[1] = 1;
    b.carry[1] = 9;
    OBL_CHECK(!Equal(a, b, diff, sizeof(diff)));
    OBL_CHECK_EQ(std::string(diff), std::string("carry: 1 vs 9"));
    // carry bytes beyond carryLen are ignored
    b.carry[1] = 1;
    b.carry[3] = 5;
    OBL_CHECK(Equal(a, b));
}
