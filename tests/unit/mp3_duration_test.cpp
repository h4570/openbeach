#include "unit_test.h"
#include "game/ui/mp3_duration.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace obl::audio;

namespace {

// Builds a 4-byte MPEG audio frame header (11-bit sync, no CRC, stereo).
std::array<uint8_t, 4> MakeHeader(uint8_t versionBits, uint8_t layerBits, uint8_t bitrateIndex, uint8_t sampleRateIndex, bool padding)
{
    return {
            0xFF,
            static_cast<uint8_t>(0xE0 | ((versionBits & 0x03) << 3) | ((layerBits & 0x03) << 1) | 0x01),
            static_cast<uint8_t>(((bitrateIndex & 0x0F) << 4) | ((sampleRateIndex & 0x03) << 2) | (padding ? 0x02 : 0x00)),
            0x00,
    };
}

} // namespace

OBL_TEST_CASE("mp3_duration.parse_frame_headers")
{
    // MPEG 1 (3), Layer III (1), 128 kbps (index 9), 44100 Hz (index 0).
    const auto mpeg1NoPad = MakeHeader(3, 1, 9, 0, false);
    const Mp3FrameHeader h1 = ParseMp3FrameHeader(mpeg1NoPad.data());
    OBL_CHECK(h1.IsValid());
    OBL_CHECK_EQ(h1.sampleRate, 44100u);
    OBL_CHECK_EQ(h1.bitrateBps, 128000u);
    OBL_CHECK_EQ(h1.samplesPerFrame, 1152u);
    OBL_CHECK_EQ(h1.frameBytes, 417u);

    const auto mpeg1Pad = MakeHeader(3, 1, 9, 0, true);
    OBL_CHECK_EQ(ParseMp3FrameHeader(mpeg1Pad.data()).frameBytes, 418u);

    // MPEG 2 (2), Layer III (1), 64 kbps (index 8), 22050 Hz (index 0).
    const auto mpeg2 = MakeHeader(2, 1, 8, 0, false);
    const Mp3FrameHeader h2 = ParseMp3FrameHeader(mpeg2.data());
    OBL_CHECK(h2.IsValid());
    OBL_CHECK_EQ(h2.sampleRate, 22050u);
    OBL_CHECK_EQ(h2.bitrateBps, 64000u);
    OBL_CHECK_EQ(h2.samplesPerFrame, 576u);
    OBL_CHECK_EQ(h2.frameBytes, 208u);

    // MPEG 2.5 (0), Layer III (1), 32 kbps (index 4), 11025 Hz (index 0).
    const auto mpeg25 = MakeHeader(0, 1, 4, 0, false);
    const Mp3FrameHeader h25 = ParseMp3FrameHeader(mpeg25.data());
    OBL_CHECK(h25.IsValid());
    OBL_CHECK_EQ(h25.sampleRate, 11025u);
    OBL_CHECK_EQ(h25.bitrateBps, 32000u);
    OBL_CHECK_EQ(h25.samplesPerFrame, 576u);
    OBL_CHECK_EQ(h25.frameBytes, 208u);

    // Invalid headers: bad sync, reserved version, reserved layer, free/bad bitrate, reserved sample rate.
    const std::array<uint8_t, 4> badSync = {0xFE, 0xFB, 0x90, 0x00};
    OBL_CHECK(!ParseMp3FrameHeader(badSync.data()).IsValid());
    OBL_CHECK(!ParseMp3FrameHeader(MakeHeader(1, 1, 9, 0, false).data()).IsValid());
    OBL_CHECK(!ParseMp3FrameHeader(MakeHeader(3, 0, 9, 0, false).data()).IsValid());
    OBL_CHECK(!ParseMp3FrameHeader(MakeHeader(3, 1, 0, 0, false).data()).IsValid());
    OBL_CHECK(!ParseMp3FrameHeader(MakeHeader(3, 1, 15, 0, false).data()).IsValid());
    OBL_CHECK(!ParseMp3FrameHeader(MakeHeader(3, 1, 9, 3, false).data()).IsValid());
}

OBL_TEST_CASE("mp3_duration.cbr_estimate_from_opening_frame_and_size")
{
    // Two back-to-back 128 kbps 44.1 kHz MPEG 1 Layer III frames in the opening window, representing a 232 s file.
    const auto hdr = MakeHeader(3, 1, 9, 0, false);
    std::vector<uint8_t> head(417 * 2, 0);
    std::copy(hdr.begin(), hdr.end(), head.begin());
    std::copy(hdr.begin(), hdr.end(), head.begin() + 417);

    const uint64_t audioBytes = 232ull * 128000ull / 8ull;
    const double seconds = EstimateMp3Duration(head, audioBytes);
    OBL_CHECK(std::abs(seconds - 232.0) < 1e-6);
}

OBL_TEST_CASE("mp3_duration.vbri_header_takes_precedence_over_cbr_size")
{
    // First frame is 128 kbps 44.1 kHz (417 bytes), containing a VBRI tag reporting 5000 frames (~130.612 s),
    // while audioBytes would suggest a much shorter CBR track if VBRI were ignored.
    const auto hdr = MakeHeader(3, 1, 9, 0, false);
    std::vector<uint8_t> head(417, 0);
    std::copy(hdr.begin(), hdr.end(), head.begin());
    head[36] = 'V';
    head[37] = 'B';
    head[38] = 'R';
    head[39] = 'I';
    // Total frames (big-endian uint32 at VBRI + 14) = 5000 = 0x00001388.
    head[36 + 14] = 0x00;
    head[36 + 15] = 0x00;
    head[36 + 16] = 0x13;
    head[36 + 17] = 0x88;

    const double expected = (5000.0 * 1152.0) / 44100.0;
    const double seconds = EstimateMp3Duration(head, 100000);
    OBL_CHECK(std::abs(seconds - expected) < 1e-6);
}

OBL_TEST_CASE("mp3_duration.skips_false_sync_before_matching_frames")
{
    // Put a false sync (320 kbps header) at offset 2 that does not land on a valid second frame,
    // followed by two real 128 kbps 44.1 kHz frames starting at offset 16.
    const auto falseHdr = MakeHeader(3, 1, 14, 0, false);
    const auto realHdr = MakeHeader(3, 1, 9, 0, false);
    std::vector<uint8_t> head(16 + 417 * 2, 0);
    std::copy(falseHdr.begin(), falseHdr.end(), head.begin() + 2);
    std::copy(realHdr.begin(), realHdr.end(), head.begin() + 16);
    std::copy(realHdr.begin(), realHdr.end(), head.begin() + 16 + 417);

    const uint64_t audioBytes = 16 + 10ull * 128000ull / 8ull;
    const double seconds = EstimateMp3Duration(head, audioBytes);
    OBL_CHECK(std::abs(seconds - 10.0) < 1e-6);
}

OBL_TEST_CASE("mp3_duration.invalid_or_empty_returns_zero")
{
    const std::array<uint8_t, 8> zeros{};
    OBL_CHECK_EQ(EstimateMp3Duration({}, 1000), 0.0);
    OBL_CHECK_EQ(EstimateMp3Duration(zeros, 1000), 0.0);
    const auto hdr = MakeHeader(3, 1, 9, 0, false);
    OBL_CHECK_EQ(EstimateMp3Duration(hdr, 0), 0.0);
}
