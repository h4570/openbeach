#include "core/pack_path.h"
#include "game/ui/sprite_anim.h"
#include "game/ui/sprite_bank.h"

#include "game/game/game_accessors.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <cstdlib>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace obl::ui {
namespace {

using Anim = ui::SpriteAnim;
test::CallCounter g_calls("ui.sprite-anim");

constexpr uint32_t kDirectionalSize = 0x1f400;

// The converted animation an engine animation reads its frames from; SpriteAnim::m_dataOffset holds its index.
struct PackAnim {
    sprite_pack::Anim anim;
    bool metadataOnly = false;
    bool directional = false; // the unique frames of each direction are a range of their own, in direction order
    std::array<int, 9> first{}; // first unique frame of each direction; first[directions] = uniqueFrames
    std::array<uint32_t, 8> base{}; // offset of each direction's frames in the animation's (virtual) frame data
};
std::filesystem::path g_spritesDirectory;
std::vector<std::unique_ptr<PackAnim>> g_anims;

[[noreturn]] void Failure(const char* detail)
{
    OBL_LOG("FatalError: sprite animation: %s", detail);
    std::_Exit(1); // the log is unbuffered: nothing to flush
}

const PackAnim& Pack(const Anim& a)
{
    if (a.m_dataOffset >= g_anims.size()) Failure("animation without converted frames");
    return *g_anims[a.m_dataOffset];
}

// Destruct and Unload release the typed frame/name arrays.
template<class T> T* Copy(const T* data, size_t count)
{
    auto* result = new T[count];
    if (!result && count) Failure("allocation failed");
    if (count) std::memcpy(result, data, count * sizeof(T));
    return result;
}

std::vector<uint8_t> Encode(const PackAnim& p, int first, int end, uint32_t base, std::vector<uint32_t>& offsets)
{
    std::vector<uint8_t> data;
    if (p.metadataOnly) {
        // Background rebuilds request frames even without a display. One transparent frame serves every unique frame;
        // logical dimensions, hotspots, timing and frame maps still come from the L1 metadata.
        const std::vector<uint8_t> blank(static_cast<size_t>(p.anim.width) * p.anim.height * 2);
        sprite_pack::EncodeFrame(blank, p.anim.width, p.anim.height, p.anim.Mask(), p.anim.runLength, data);
        std::fill(offsets.begin() + first, offsets.begin() + end, base);
        return data;
    }
    std::string error;
    if (!sprite_pack::EncodeFrames(g_spritesDirectory, p.anim, first, end, base, data, offsets, error)) Failure(error.c_str());
    return data;
}

void LoadAll(Anim* a)
{
    const PackAnim& p = Pack(*a);
    std::vector<uint32_t> offsets(a->m_uniqueFrames);
    const std::vector<uint8_t> data = Encode(p, 0, a->m_uniqueFrames, 0, offsets);
    a->m_frameIndices = Copy(p.anim.frameMap.data(), p.anim.frameMap.size());
    a->m_frameOffsets = Copy(offsets.data(), offsets.size());
    a->m_allLoaded = 1;
    a->m_allData = Copy(data.data(), data.size());
    a->m_loaded = 1;
}

// The offsets of the frames of other directions stay zero until their direction loads: GetFrame reads only those of the
// direction it returns a frame of.
void LoadDirection(Anim* a, int d)
{
    const PackAnim& p = Pack(*a);
    a->m_allLoaded = 0;
    if (!a->m_directionIndexLoaded) {
        a->m_frameIndices = Copy(p.anim.frameMap.data(), p.anim.frameMap.size());
        const std::vector<uint32_t> zero(a->m_uniqueFrames);
        a->m_frameOffsets = Copy(zero.data(), zero.size());
        for (int i = 0; i < a->m_directions; ++i) a->m_directionOffsets[i] = p.base[i];
        a->m_directionIndexLoaded = 1;
    }
    std::vector<uint32_t> offsets(a->m_uniqueFrames);
    const std::vector<uint8_t> data = Encode(p, p.first[d], p.first[d + 1], p.base[d], offsets);
    std::copy(offsets.begin() + p.first[d], offsets.begin() + p.first[d + 1], a->m_frameOffsets + p.first[d]);
    a->m_directionData[d] = Copy(data.data(), data.size());
    a->m_loaded = 1;
}

uint8_t* GetFrame(Anim* a, int frame, int d)
{
    frame = std::min(frame, static_cast<int>(a->m_frames) - 1);
    d = std::min(d, static_cast<int>(a->m_directions) - 1);
    if (!a->m_loaded || (!a->m_allLoaded && !a->m_directionData[d])) {
        if (a->m_dataSize < kDirectionalSize)
            LoadAll(a);
        else
            LoadDirection(a, d);
    }
    uint32_t offset = a->m_frameOffsets[a->m_frameIndices[d * a->m_frames + frame]];
    return a->m_allLoaded ? a->m_allData + offset : a->m_directionData[d] + (offset - a->m_directionOffsets[d]);
}

// Constructor writes every field except m_lastUsedTime and padding, which stay as the allocator left them.
void Construct(Anim* a)
{
    a->m_name = nullptr;
    a->m_frames = 0;
    a->m_loaded = 0;
    a->m_directions = 0;
    a->m_width = a->m_height = 0;
    a->m_hotspotX = a->m_hotspotY = 0;
    a->m_dataSize = a->m_dataOffset = 0;
    a->m_allData = nullptr;
    a->m_frameOffsets = nullptr;
    a->m_frameIndices = nullptr;
    a->m_uniqueFrames = 0;
    a->m_allLoaded = 0;
    a->m_directionIndexLoaded = 0;
    for (int d = 0; d < 8; ++d) {
        a->m_directionOffsets[d] = 0;
        a->m_directionData[d] = nullptr;
    }
}

void Destruct(Anim* a)
{
    delete[] a->m_name;
    delete[] a->m_allData;
    delete[] a->m_frameOffsets;
    delete[] a->m_frameIndices;
    for (auto* data : a->m_directionData)
        if (data) delete[] data;
}

// Unlike Destruct this frees unconditionally (delete(nullptr) is a no-op) and clears what it released.
void Unload(Anim* a)
{
    a->m_loaded = 0;
    delete[] a->m_frameIndices;
    delete[] a->m_frameOffsets;
    a->m_directionIndexLoaded = 0;
    a->m_frameIndices = nullptr;
    a->m_frameOffsets = nullptr;
    if (a->m_allLoaded) {
        delete[] a->m_allData;
        a->m_allData = nullptr;
        return;
    }
    for (auto*& data : a->m_directionData) {
        if (data) {
            delete[] data;
            data = nullptr;
        }
    }
}

uint32_t Hash(uint32_t hash, const void* data, size_t count)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

// The bytes of the run-length frame at `frame` (sprite_pack::EncodeFrame's form).
size_t FrameBytes(const uint8_t* frame, const Anim& a, bool mask)
{
    const uint8_t* p = frame;
    for (int y = 0; y < a.m_height; ++y)
        for (int x = 0; x < a.m_width;) {
            const int run = static_cast<int8_t>(*p++);
            if (run > 0 && !mask) p += run;
            x += run < 0 ? -run : run;
        }
    return static_cast<size_t>(p - frame);
}

// Every `stride`-th animation of the live bank (decoding all 447 MB of sheets takes minutes in Debug) loaded whole and
// direction by direction from the converted sheets: both caches return the same frames, a repeated or out-of-range request
void Drive(int stride)
{
    stride = std::max(stride, 1);
    int index = 0;
    const auto* bank = GetSpriteBank();
    if (!bank) Failure("no sprite bank");
    unsigned anims = 0, frames = 0, directional = 0, assertions = 0;
    uint32_t hash = 2166136261u;
    auto check = [&](bool match) {
        ++assertions;
        if (!match) Failure("driver assertion failed");
    };
    for (int s = 0; s < bank->m_spriteCount; ++s) {
        const ui::Sprite& sprite = *bank->m_sprites[s];
        for (int n = 0; n < sprite.m_animCount; ++n) {
            const Anim& live = *sprite.m_anims[n];
            if (index++ % stride) continue;
            const PackAnim& p = Pack(live);
            Anim whole, parts;
            for (Anim* a : {&whole, &parts}) {
                std::memset(a, 0xa5, sizeof(Anim));
                Construct(a);
                AttachPackAnim(a, p.anim); // the pixel oracle loads L2 even when the live bank is metadata-only
            }
            whole.m_dataSize = 0;
            parts.m_dataSize = kDirectionalSize;
            const bool split = p.directional;
            for (int d = live.m_directions - 1; d >= 0; --d) {
                for (int f = 0; f < live.m_frames; ++f) {
                    const uint8_t* a = (&whole)->GetFrame(f, d);
                    const size_t bytes = FrameBytes(a, whole, p.anim.Mask());
                    check(whole.m_allLoaded && a == whole.m_allData + whole.m_frameOffsets[p.anim.frameMap[d * live.m_frames + f]]);
                    if (split) {
                        const uint8_t* b = (&parts)->GetFrame(f, d);
                        check(!parts.m_allLoaded && parts.m_directionData[d] && std::memcmp(a, b, bytes) == 0);
                    }
                    hash = Hash(hash, a, bytes);
                    ++frames;
                }
            }
            uint8_t* last = (&whole)->GetFrame(live.m_frames - 1, live.m_directions - 1);
            check((&whole)->GetFrame(live.m_frames, live.m_directions) == last);
            check((&whole)->GetFrame(0x7fffffff, 0x7fffffff) == last);
            check(std::memcmp(whole.m_frameIndices, p.anim.frameMap.data(), p.anim.frameMap.size() * 2) == 0);
            for (Anim* a : {&whole, &parts}) {
                Unload(a);
                check(!a->m_loaded && !a->m_directionIndexLoaded && !a->m_frameIndices && !a->m_frameOffsets && !a->m_allData);
                for (auto* data : a->m_directionData) check(!data);
                Destruct(a);
            }
            directional += split ? 1 : 0;
            ++anims;
        }
    }
    OBL_LOG("autotest: sprite-anim-drive anims=%u frames=%u directional=%u assertions=%u hash=%u", anims, frames, directional, assertions,
            hash);
}

} // namespace

void SpriteAnim::LoadAll()
{
    g_calls.Count();
    ::obl::ui::LoadAll(this);
}
void SpriteAnim::LoadDirection(int d)
{
    g_calls.Count();
    ::obl::ui::LoadDirection(this, d);
}
uint8_t* SpriteAnim::GetFrame(int frame, int d)
{
    g_calls.Count();
    return ::obl::ui::GetFrame(this, frame, d);
}

void SpriteAnim::Construct()
{
    g_calls.Count();
    ::obl::ui::Construct(this);
}

void SpriteAnim::Destruct()
{
    g_calls.Count();
    ::obl::ui::Destruct(this);
}

void SpriteAnim::Unload()
{
    g_calls.Count();
    ::obl::ui::Unload(this);
}

void AttachPackAnim(Anim* a, sprite_pack::Anim pack, bool metadataOnly)
{
    if (pack.directions > 8) Failure("more than eight directions");
    a->m_name = Copy(pack.name.c_str(), pack.name.size() + 1);
    a->m_frames = pack.frames;
    a->m_directions = pack.directions;
    a->m_width = pack.width;
    a->m_height = pack.height;
    a->m_hotspotX = pack.hotspotX;
    a->m_hotspotY = pack.hotspotY;
    a->m_uniqueFrames = pack.uniqueFrames;
    auto p = std::make_unique<PackAnim>();
    p->metadataOnly = metadataOnly;
    // Direction d may load alone when its frames are the unique frames [first[d], first[d + 1]). Its data then sits at a
    // fixed base of two bytes per pixel and frame before it, the most a frame can encode to.
    bool ranges = true;
    for (int d = 0; d < pack.directions; ++d) p->first[d] = pack.frameMap[static_cast<size_t>(d) * pack.frames];
    p->first[pack.directions] = pack.uniqueFrames;
    for (int d = 0; d < pack.directions && ranges; ++d) {
        ranges = p->first[d] < p->first[d + 1] && (d > 0 || p->first[0] == 0);
        for (int f = 0; f < pack.frames && ranges; ++f) {
            const int u = pack.frameMap[static_cast<size_t>(d) * pack.frames + f];
            ranges = u >= p->first[d] && u < p->first[d + 1];
        }
    }
    const uint64_t stride = 2ull * pack.width * pack.height;
    p->directional = ranges && stride * pack.uniqueFrames <= 0xffffffffull;
    for (int d = 0; d < pack.directions; ++d) p->base[d] = static_cast<uint32_t>(p->directional ? stride * p->first[d] : 0);
    const uint64_t pixels = static_cast<uint64_t>(pack.width) * pack.height * pack.uniqueFrames;
    a->m_dataSize = p->directional ? static_cast<uint32_t>(std::min<uint64_t>(pixels, 0xffffffffull)) : 0;
    a->m_dataOffset = static_cast<uint32_t>(g_anims.size());
    p->anim = std::move(pack);
    g_anims.push_back(std::move(p));
}

bool ConfigureSpriteAnimations(const std::filesystem::path& gameDirectory)
{
    g_spritesDirectory = gameDirectory / pack_path::Selected() / "sprites";
    g_calls.SetDriver(&Drive);
    return true;
}

} // namespace obl::ui
