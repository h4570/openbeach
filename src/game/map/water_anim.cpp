#include "water_anim.h"
#include "core/os.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace obl::water_anim {

namespace {

// Truncation toward zero; a value that does not fit gives INT_MIN.
int FloatToIntWrap(double value)
{
    if (!(value > -2147483649.0 && value < 2147483648.0)) return INT_MIN;
    return static_cast<int>(value);
}

} // namespace

int FrameAt(float seconds)
{
    return FloatToIntWrap(static_cast<double>(seconds) * kFramesPerSecond) % kFrames;
}

void Construct(ui::WaterAnim& anim)
{
    anim.m_data = nullptr;
    anim.m_loaded = 0;
    anim.m_lastFrame = -1;
    anim.m_enabled = 1;
}

void SetEnabled(ui::WaterAnim& anim, uint8_t enabled)
{
    anim.m_enabled = enabled;
}

const uint8_t* GetMask(const ui::WaterAnim& anim, int column, int row)
{
    if (!anim.m_loaded || !anim.m_enabled) return nullptr;
    const size_t offset = (static_cast<size_t>(row & 1) << 15) | ((column & 1) ? 0x4000u : 0u);
    return anim.m_mask + offset;
}

bool Update(ui::WaterAnim& anim, float seconds)
{
    const int frame = FrameAt(seconds);
    if (!anim.m_loaded || frame < 0) return false;
    bool rewritten = false;
    if (frame != anim.m_lastFrame) {
        const int8_t* runs = reinterpret_cast<const int8_t*>(anim.m_data + anim.m_frameOffset[frame]);
        uint8_t* out = anim.m_mask;
        uint8_t* const end = anim.m_mask + sizeof(anim.m_mask);
        for (int remaining = anim.m_frameRuns[frame]; remaining > 0; --remaining) {
            const int run = *runs++;
            // A positive run sets that many bytes to 1, anything else clears -run bytes (0 clears none). A malformed frame stops
            // at the end of the mask.
            const int length = run > 0 ? run : -run;
            const size_t room = static_cast<size_t>(end - out);
            const size_t count = static_cast<size_t>(length) < room ? static_cast<size_t>(length) : room;
            std::memset(out, run > 0 ? 1 : 0, count);
            out += count;
        }
        rewritten = true;
    }
    anim.m_lastFrame = frame;
    return rewritten;
}

bool Load(ui::WaterAnim& anim, const char* path, const Allocator& heap, uint32_t* sizeOut)
{
    std::FILE* file = os::OpenFile(path, "rb");
    if (!file) {
        anim.m_loaded = 0;
        return false;
    }
    uint32_t size = 0;
    (void)!std::fread(&size, 4, 1, file);
    if (anim.m_data) heap.release(anim.m_data);
    anim.m_data = static_cast<uint8_t*>(heap.allocate(size));
    (void)!std::fread(anim.m_frameOffset, 4, kFrames, file);
    (void)!std::fread(anim.m_frameRuns, 4, kFrames, file);
    (void)!std::fread(anim.m_data, 1, size, file);
    std::fclose(file);
    anim.m_loaded = 1;
    if (sizeOut) *sizeOut = size;
    return true;
}

void LoadSheet(ui::WaterAnim& anim, const uint8_t* sheet, const Allocator& heap)
{
    std::vector<int8_t> data;
    for (int frame = 0; frame < kFrames; ++frame) {
        anim.m_frameOffset[frame] = static_cast<uint32_t>(data.size());
        const size_t first = data.size();
        int pending = 0; // signed run being built: > 0 on bytes, < 0 off bytes
        for (size_t pixel = 0; pixel < 65536; ++pixel) {
            const size_t mask = pixel / 16384, within = pixel % 16384;
            const bool on = sheet[(mask * 128 + within / 128) * kSheetWidth + static_cast<size_t>(frame) * 128 + within % 128] != 0;
            if (pending != 0 && (pending > 0) == on && (on ? pending < 127 : pending > -127)) {
                pending += on ? 1 : -1;
                continue;
            }
            if (pending != 0) data.push_back(static_cast<int8_t>(pending));
            pending = on ? 1 : -1;
        }
        data.push_back(static_cast<int8_t>(pending));
        anim.m_frameRuns[frame] = static_cast<uint32_t>(data.size() - first);
    }
    if (anim.m_data) heap.release(anim.m_data);
    anim.m_data = static_cast<uint8_t*>(heap.allocate(data.size()));
    std::memcpy(anim.m_data, data.data(), data.size());
    anim.m_loaded = 1;
}

SourceRect StartRect(const uint32_t rect[4], uint32_t imageWidth, uint32_t imageHeight)
{
    SourceRect out{static_cast<int>(rect[0]), static_cast<int>(rect[1]), static_cast<int>(rect[2]), static_cast<int>(rect[3])};
    if (rect[2] == 0 || rect[3] == 0) {
        out.right = static_cast<int>(imageWidth + rect[0]);
        out.bottom = static_cast<int>(imageHeight + rect[1]);
    }
    return out;
}

const uint8_t* BlockMask(const ui::WaterAnim* anim, int column, int row, int useMask)
{
    return anim && useMask == 1 ? GetMask(*anim, column, row) : nullptr;
}

} // namespace obl::water_anim
