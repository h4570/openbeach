#include "core/mp3info.h"
#include "core/os.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace obl::mp3 {
namespace {

// The MPEG audio header tables for Layer III (kbit/s, Hz). Bitrate index 0 (free format) and 15 and sample rate index 3 are
// reserved here: the 0 makes the frame length or the rate invalid, so such a header is not a frame.
constexpr int32_t kBitrateMpeg1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
constexpr int32_t kBitrateMpeg2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
constexpr int32_t kRateMpeg1[4] = {44100, 48000, 32000, 0};
constexpr int32_t kRateMpeg2[4] = {22050, 24000, 16000, 0};

constexpr uint32_t kChunk = 0x1000;

} // namespace

void Init(Mp3Info& info)
{
    std::memset(&info, 0, sizeof(info));
    info.layer = -1;
}

void ParseFrames(Mp3Info& m, const uint8_t* data, uint32_t size)
{
    std::vector<uint8_t> joined;
    const uint8_t* base = data;
    uint32_t end = size;
    if (m.carryLen > 0) {
        joined.assign(m.carry, m.carry + m.carryLen);
        joined.insert(joined.end(), data, data + size);
        base = joined.data();
        end = static_cast<uint32_t>(joined.size());
    }
    uint32_t pos = static_cast<uint32_t>(m.skip);
    while (pos < end) {
        int32_t remaining = static_cast<int32_t>(end - pos);
        if (remaining < 4) {
            m.carryLen = remaining;
            std::memcpy(m.carry, base + pos, static_cast<size_t>(remaining));
            m.skip = 0;
            return;
        }
        const uint8_t* p = base + pos;
        uint8_t b1 = p[1];
        uint8_t sync = b1 & 0xf0;
        if (p[0] != 0xff || (sync != 0xf0 && sync != 0xe0)) {
            ++m.junkBytes;
            ++pos;
            continue;
        }
        uint8_t b2 = p[2];
        bool mpeg1 = (b1 & 8) != 0;
        int32_t rate = mpeg1 ? kRateMpeg1[(b2 >> 2) & 3] : kRateMpeg2[(b2 >> 2) & 3];
        if (rate == 0) {
            ++m.junkBytes;
            ++pos;
            continue;
        }
        int32_t version = mpeg1 ? 1 : 2;
        int32_t bitrate = mpeg1 ? kBitrateMpeg1[b2 >> 4] : kBitrateMpeg2[b2 >> 4];
        uint32_t length = static_cast<uint32_t>(bitrate * (version == 1 ? 144000 : 72000) / rate) + ((b2 >> 1) & 1u);
        if (length <= 1 || length > 0x800) {
            ++m.junkBytes;
            ++pos;
            continue;
        }
        if (m.frames == 0) {
            m.layer = (b1 >> 1) & 3;
            m.sampleRate = rate;
            m.bitrate = bitrate;
            m.version = version;
            m.notMono = (p[3] & 0xc0) != 0xc0;
        } else {
            if (m.sampleRate != rate) {
                ++m.junkBytes;
                ++pos;
                continue;
            }
            if (m.bitrate != 0 && m.bitrate != bitrate) m.bitrate = 0;
        }
        ++m.frames;
        m.frameBytes = static_cast<int32_t>(static_cast<uint32_t>(m.frameBytes) + length);
        m.bitrateSum += bitrate;
        pos += length;
    }
    m.skip = static_cast<int32_t>(pos - end);
    m.carryLen = 0;
}

void Finish(Mp3Info& m)
{
    if (m.junkBytes > m.frameBytes) {
        m.valid = 0;
        return;
    }
    if (m.frames == 0) {
        m.valid = 0;
        return;
    }
    int32_t khz = m.sampleRate / 1000;
    int32_t samples = static_cast<int32_t>(static_cast<uint32_t>(m.frames) * (m.version == 1 ? 1152u : 576u));
    m.valid = 1;
    m.lengthMs = samples / khz;
    m.bitrateSum /= m.frames;
}

bool IsValidLayer3(const Mp3Info& m)
{
    return m.valid != 0 && m.layer == 1;
}

void ScanFile(Mp3Info& m, const char* directory, const char* file)
{
    std::string path = std::string(directory) + "/" + file;
    FILE* fp = os::OpenFile(path, "rb");
    if (!fp) return;
    m.valid = 1;
    std::vector<uint8_t> buffer(kChunk);
    size_t n;
    while ((n = std::fread(buffer.data(), 1, kChunk, fp)) > 0) ParseFrames(m, buffer.data(), static_cast<uint32_t>(n));
    std::fclose(fp);
    Finish(m);
}

bool Equal(const Mp3Info& a, const Mp3Info& b, char* diff, size_t diffSize)
{
    auto report = [&](const char* field, long x, long y) {
        if (diff && diffSize) std::snprintf(diff, diffSize, "%s: %ld vs %ld", field, x, y);
        return false;
    };
#define OBL_MP3_FIELD(f)                                                                                                                   \
    if (a.f != b.f) return report(#f, static_cast<long>(a.f), static_cast<long>(b.f))
    OBL_MP3_FIELD(bitrate);
    OBL_MP3_FIELD(sampleRate);
    OBL_MP3_FIELD(notMono);
    OBL_MP3_FIELD(lengthMs);
    OBL_MP3_FIELD(frames);
    OBL_MP3_FIELD(version);
    OBL_MP3_FIELD(bitrateSum);
    OBL_MP3_FIELD(layer);
    OBL_MP3_FIELD(skip);
    OBL_MP3_FIELD(carryLen);
    OBL_MP3_FIELD(frameBytes);
    OBL_MP3_FIELD(junkBytes);
    OBL_MP3_FIELD(valid);
#undef OBL_MP3_FIELD
    for (int i = 0; i < a.carryLen && i < 4; ++i)
        if (a.carry[i] != b.carry[i]) return report("carry", a.carry[i], b.carry[i]);
    return true;
}

} // namespace obl::mp3
