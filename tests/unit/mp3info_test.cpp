// MP3 length scanner (src/core/mp3info.h): synthetic streams and the shipped soundtrack as oracle.

#include "unit_test.h"

#include "core/mp3info.h"

#include <filesystem>
#include <vector>

using namespace obl::mp3;

namespace {

// MPEG1 Layer III, 128 kbit/s, 44.1 kHz: 417 bytes (+1 with the padding bit).
void AppendFrame(std::vector<uint8_t>& out, bool pad = false)
{
    out.insert(out.end(), {0xff, 0xfb, static_cast<uint8_t>(pad ? 0x92 : 0x90), 0x00});
    out.resize(out.size() + 417 + (pad ? 1 : 0) - 4, 0x55);
}

Mp3Info ParseChunked(const std::vector<uint8_t>& bytes, size_t chunk)
{
    Mp3Info info;
    Init(info);
    info.valid = 1;
    for (size_t at = 0; at < bytes.size(); at += chunk)
        ParseFrames(info, bytes.data() + at, static_cast<uint32_t>(std::min(chunk, bytes.size() - at)));
    Finish(info);
    return info;
}

} // namespace

OBL_TEST_CASE("mp3info.counts_frames_and_length")
{
    std::vector<uint8_t> bytes;
    for (int i = 0; i < 100; ++i) AppendFrame(bytes, i % 3 == 0);
    Mp3Info info = ParseChunked(bytes, bytes.size());
    OBL_CHECK_EQ(info.frames, 100);
    OBL_CHECK_EQ(info.version, 1);
    OBL_CHECK_EQ(info.sampleRate, 44100);
    OBL_CHECK_EQ(info.bitrate, 128);
    OBL_CHECK_EQ(info.junkBytes, 0);
    OBL_CHECK_EQ(info.frameBytes, 100 * 417 + 34);
    // The game divides by the sampling rate truncated to whole kHz (44), not 44.1.
    OBL_CHECK_EQ(info.lengthMs, 100 * 1152 / 44);
    OBL_CHECK_EQ(info.bitrateSum, 128);
    OBL_CHECK(IsValidLayer3(info));
}

OBL_TEST_CASE("mp3info.chunk_boundaries_do_not_change_the_result")
{
    std::vector<uint8_t> bytes = {0x12, 0xff, 0x00, 0x34}; // leading junk
    for (int i = 0; i < 60; ++i) AppendFrame(bytes, i & 1);
    Mp3Info whole = ParseChunked(bytes, bytes.size());
    for (size_t chunk : {1u, 3u, 4u, 5u, 7u, 100u, 417u, 418u, 4096u}) {
        Mp3Info split = ParseChunked(bytes, chunk);
        char diff[96] = "";
        OBL_CHECK(Equal(whole, split, diff, sizeof(diff)));
    }
    OBL_CHECK_EQ(whole.frames, 60);
    OBL_CHECK_EQ(whole.junkBytes, 4);
}

OBL_TEST_CASE("mp3info.rejects_junk_and_changing_rate")
{
    std::vector<uint8_t> noise(2000, 0x41);
    Mp3Info info = ParseChunked(noise, noise.size());
    OBL_CHECK(!IsValidLayer3(info));
    OBL_CHECK_EQ(info.junkBytes, 1997); // the last 3 bytes stay in the carry buffer, never counted

    // A frame at another sampling rate is junk, a different bitrate zeroes the average bitrate (VBR).
    std::vector<uint8_t> bytes;
    AppendFrame(bytes);
    bytes.insert(bytes.end(), {0xff, 0xfb, 0x94, 0x00}); // 48 kHz
    bytes.resize(bytes.size() + 400, 0x55);
    Mp3Info vbr = ParseChunked(bytes, bytes.size());
    OBL_CHECK_EQ(vbr.frames, 1);
    OBL_CHECK(vbr.junkBytes > 0);

    std::vector<uint8_t> mixed;
    AppendFrame(mixed);
    mixed.insert(mixed.end(), {0xff, 0xfb, 0xa0, 0x00}); // 160 kbit/s: 522 bytes
    mixed.resize(mixed.size() + 522 - 4, 0x55);
    Mp3Info varying = ParseChunked(mixed, mixed.size());
    OBL_CHECK_EQ(varying.frames, 2);
    OBL_CHECK_EQ(varying.bitrate, 0);
}

OBL_TEST_CASE("mp3info.empty_input_is_invalid_not_a_crash")
{
    Mp3Info info = ParseChunked({}, 4096);
    OBL_CHECK(!IsValidLayer3(info));
}

OBL_TEST_CASE("mp3info.layer_must_be_three")
{
    std::vector<uint8_t> bytes;
    for (int i = 0; i < 4; ++i) {
        bytes.insert(bytes.end(), {0xff, 0xfd, 0x90, 0x00}); // Layer II
        bytes.resize(bytes.size() + 417 - 4, 0x55);
    }
    Mp3Info info = ParseChunked(bytes, bytes.size());
    OBL_CHECK_EQ(info.frames, 4);
    OBL_CHECK(!IsValidLayer3(info));
}

// Reference values for the shipped soundtrack (docs/architecture/presentation-fonts-audio.md).
OBL_TEST_CASE("data.mp3info_reads_the_soundtrack_tracks")
{
    const std::filesystem::path dir = std::filesystem::path(OBL_GAME_DIR) / "data" / "soundtrack";
    if (!std::filesystem::exists(dir / "Day1.mp3")) OBL_SKIP("run/ has no imported game data");
    struct Expected {
        const char* file;
        int frames;
        int ms;
    };
    for (const Expected& e : {Expected{"Day1.mp3", 8882, 232546}, Expected{"Night1.mp3", 21868, 572544}}) {
        Mp3Info info;
        Init(info);
        ScanFile(info, dir.string().c_str(), e.file);
        OBL_CHECK_EQ(info.frames, e.frames);
        OBL_CHECK_EQ(info.lengthMs, e.ms);
        OBL_CHECK(IsValidLayer3(info));
    }
}
