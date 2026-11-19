#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace obl::audio {

// Parsed fields of one 4-byte MPEG audio frame header (MPEG 1, 2 or 2.5; Layer I, II or III).
struct Mp3FrameHeader {
    uint32_t sampleRate = 0;
    uint32_t bitrateBps = 0;
    uint32_t samplesPerFrame = 0;
    uint32_t frameBytes = 0;
    uint8_t versionBits = 0;
    uint8_t layerBits = 0;

    bool IsValid() const { return sampleRate != 0 && bitrateBps != 0 && frameBytes >= 4; }
};

inline Mp3FrameHeader ParseMp3FrameHeader(const uint8_t* header)
{
    if (!header || header[0] != 0xFF || (header[1] & 0xE0) != 0xE0) return {};

    const uint8_t versionBits = static_cast<uint8_t>((header[1] >> 3) & 0x03);
    const uint8_t layerBits = static_cast<uint8_t>((header[1] >> 1) & 0x03);
    const uint8_t bitrateIndex = static_cast<uint8_t>((header[2] >> 4) & 0x0F);
    const uint8_t sampleRateIndex = static_cast<uint8_t>((header[2] >> 2) & 0x03);
    const uint32_t padding = static_cast<uint32_t>((header[2] >> 1) & 0x01);

    if (versionBits == 1 || layerBits == 0 || bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3) return {};

    // Half-bitrates in kbps, indexed by [isMpeg1][layerBits - 1][bitrateIndex] (matches ISO 11172-3 / 13818-3).
    static constexpr uint8_t kHalfBitrates[2][3][15] = {
            {
                    {0, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80},
                    {0, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80},
                    {0, 16, 24, 28, 32, 40, 48, 56, 64, 72, 80, 88, 96, 112, 128},
            },
            {
                    {0, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160},
                    {0, 16, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192},
                    {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224},
            },
    };
    static constexpr uint32_t kMpeg1SampleRates[3] = {44100, 48000, 32000};

    const bool isMpeg1 = (versionBits == 3);
    const bool isMpeg25 = (versionBits == 0);
    const uint32_t bitrateKbps = 2u * kHalfBitrates[isMpeg1 ? 1 : 0][layerBits - 1][bitrateIndex];
    const uint32_t sampleRate = kMpeg1SampleRates[sampleRateIndex] >> (isMpeg1 ? 0u : (isMpeg25 ? 2u : 1u));
    const uint32_t samplesPerFrame = (layerBits == 3) ? 384u : ((layerBits == 1 && !isMpeg1) ? 576u : 1152u);

    uint32_t frameBytes = (samplesPerFrame * bitrateKbps * 125u) / sampleRate;
    if (layerBits == 3) {
        frameBytes = (frameBytes & ~3u) + padding * 4u;
    } else {
        frameBytes += padding;
    }

    return Mp3FrameHeader{sampleRate, bitrateKbps * 1000u, samplesPerFrame, frameBytes, versionBits, layerBits};
}

// Estimates an MP3 track's duration in seconds from the opening bytes of the audio stream (after ID3v2)
// and the audio byte length (excluding ID3v2 and trailing ID3v1/APE tags), without scanning the file.
// Uses a Fraunhofer VBRI header when present; otherwise falls back to the CBR size/bitrate ratio.
inline double EstimateMp3Duration(std::span<const uint8_t> head, uint64_t audioBytes)
{
    if (head.size() < 4 || audioBytes == 0) return 0.0;

    // Prefer a header whose computed frame size lands on a matching second header to skip false syncs in padding.
    size_t bestOffset = head.size();
    Mp3FrameHeader best{};
    for (size_t i = 0; i + 4 <= head.size(); ++i) {
        const Mp3FrameHeader hdr = ParseMp3FrameHeader(head.data() + i);
        if (!hdr.IsValid()) continue;
        if (bestOffset == head.size()) {
            bestOffset = i;
            best = hdr;
        }
        const size_t nextOffset = i + hdr.frameBytes;
        if (nextOffset + 4 <= head.size()) {
            const Mp3FrameHeader next = ParseMp3FrameHeader(head.data() + nextOffset);
            if (next.IsValid() && next.versionBits == hdr.versionBits && next.layerBits == hdr.layerBits &&
                    next.sampleRate == hdr.sampleRate) {
                bestOffset = i;
                best = hdr;
                break;
            }
        }
    }
    if (!best.IsValid()) return 0.0;

    // VBRI header sits 32 bytes after the 4-byte MPEG header; total MP3 frames is a big-endian uint32 at +14.
    constexpr size_t kVbriOffset = 36;
    constexpr size_t kVbriFramesOffset = 14;
    constexpr size_t kVbriMinSize = 18;
    if (best.frameBytes >= kVbriOffset + kVbriMinSize && bestOffset + kVbriOffset + kVbriMinSize <= head.size()) {
        const uint8_t* vbri = head.data() + bestOffset + kVbriOffset;
        if (vbri[0] == 'V' && vbri[1] == 'B' && vbri[2] == 'R' && vbri[3] == 'I') {
            const uint32_t frames = (static_cast<uint32_t>(vbri[kVbriFramesOffset]) << 24) |
                    (static_cast<uint32_t>(vbri[kVbriFramesOffset + 1]) << 16) | (static_cast<uint32_t>(vbri[kVbriFramesOffset + 2]) << 8) |
                    static_cast<uint32_t>(vbri[kVbriFramesOffset + 3]);
            if (frames > 0) return static_cast<double>(static_cast<uint64_t>(frames) * best.samplesPerFrame) / best.sampleRate;
        }
    }

    if (audioBytes <= bestOffset) return 0.0;
    const uint64_t payloadBytes = audioBytes - bestOffset;
    return (static_cast<double>(payloadBytes) * 8.0) / static_cast<double>(best.bitrateBps);
}

} // namespace obl::audio
