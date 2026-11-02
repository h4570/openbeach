#include "game/ui/renderer_api.h"
#include "game/ui/blit.h"

#include "game/game/game_accessors.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace obl::ui {
namespace {

// Call counters (`call-report ui.blit`), and the counters the drivers fill (`call-drive`).
test::CallCounter g_depthVerified("ui.blit-depth-verified"), g_litVerified("ui.blit-lit-verified");
test::CallCounter g_rampVerified("ui.blit-ramp-verified"), g_shadowVerified("ui.blit-shadow-verified");
test::CallCounter g_waterVerified("ui.blit-water-verified"), g_waterAliasVerified("ui.blit-water-alias-verified");
// The drivers are attached to these (the names of the blit entries they exercise).
test::CallCounter g_ui32("ui.blit-ui32"), g_waterFull("ui.blit-water-full"), g_depth32("ui.blit-depth32"), g_lit32("ui.blit-depth-lit32"),
        g_ramp("ui.blit-depth-ramp32"), g_shadow("ui.blit-shadow32"), g_actorSelect("ui.blit-actor-select");

blit::Masks CurrentMasks()
{
    return {game::GetBlitHalfMask(), game::GetBlitQuarterMask()};
}

// ---- private writers: every blitter runs src/game/ui/blit.h ------------------------------------------------------------------------

template<class Pixel> void WriteRle(Pixel* dst, const uint8_t* rle, const uint16_t* palette, blit::RleRect r, bool translucent)
{
    if (translucent)
        blit::Translucent(dst, rle, palette, r, CurrentMasks());
    else
        blit::Ui(dst, rle, palette, r);
}

#define OBL_RLE_WRITER(Name, Pixel, Translucent)                                                                                           \
    void Name(Pixel* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left, int top,  \
            int right, int bottom)                                                                                                         \
    {                                                                                                                                      \
        WriteRle(dst, rle, palette, {pitch, x, y, width, height, left, top, right, bottom}, Translucent);                                  \
    }

OBL_RLE_WRITER(WriteUi16, uint16_t, false)
OBL_RLE_WRITER(WriteUi32, uint32_t, false)
OBL_RLE_WRITER(WriteTranslucent16, uint16_t, true)
OBL_RLE_WRITER(WriteTranslucent32, uint32_t, true)
#undef OBL_RLE_WRITER

template<class Pixel>
void WriteBitmap(Pixel* dst, const uint8_t* src, const uint16_t* palette, blit::BitmapRect r, bool keyed, uint16_t mask)
{
    if constexpr (sizeof(Pixel) == 2)
        blit::Bitmap(dst, src, palette, r);
    else
        blit::Bitmap(dst, src, palette, r, keyed, mask);
}

#define OBL_BITMAP_WRITER(Name, Pixel, Keyed)                                                                                              \
    void Name(const uint8_t* src, int sp, Pixel* dst, int dp, int sx, int sy, int dx, int dy, int width, int height,                       \
            const uint16_t* palette)                                                                                                       \
    {                                                                                                                                      \
        WriteBitmap(dst, src, palette, {sp, dp, sx, sy, dx, dy, width, height}, Keyed, 0xffff);                                            \
    }

OBL_BITMAP_WRITER(WriteBitmap16, uint16_t, true)
OBL_BITMAP_WRITER(WriteOpaque32, uint32_t, false)
OBL_BITMAP_WRITER(WriteKeyed32, uint32_t, true)
#undef OBL_BITMAP_WRITER

void WriteMasked32(const uint8_t* src, int sp, uint32_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height,
        const uint16_t* palette, uint16_t mask)
{
    WriteBitmap(dst, src, palette, {sp, dp, sx, sy, dx, dy, width, height}, true, mask);
}

void WriteDepth(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, blit::RleRect r, uint32_t depth, int slant,
        blit::DepthKind kind, bool zoom)
{
    const auto* renderer = kind == blit::DepthKind::Lit ? game::GetRenderer() : nullptr;
    blit::Depth(dst, rle, palette, r, depth, slant, CurrentMasks(), kind, zoom, game::GetBlitKeylineColor(),
            renderer ? renderer->m_lightBuffer : nullptr, renderer ? renderer->m_width : 0);
}

#define OBL_DEPTH_WRITER(Name, Kind, Zoom)                                                                                                 \
    void Name(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,        \
            int top, int right, int bottom, uint32_t depth, int slant)                                                                     \
    {                                                                                                                                      \
        WriteDepth(dst, rle, palette, {pitch, x, y, width, height, left, top, right, bottom}, depth, slant, blit::DepthKind::Kind, Zoom);  \
    }
OBL_DEPTH_WRITER(WriteDepth32, Actor, false)
OBL_DEPTH_WRITER(WriteKeyline32, Keyline, false)
OBL_DEPTH_WRITER(WriteDepthZoom32, Actor, true)
OBL_DEPTH_WRITER(WriteLit32, Lit, false)
OBL_DEPTH_WRITER(WriteLitZoom32, Lit, true)
#undef OBL_DEPTH_WRITER

#define OBL_TYPE12_WRITER(Name, Zoom)                                                                                                      \
    void Name(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,        \
            int top, int right, int bottom, uint32_t depth)                                                                                \
    {                                                                                                                                      \
        WriteDepth(dst, rle, palette, {pitch, x, y, width, height, left, top, right, bottom}, depth, 0, blit::DepthKind::Type12, Zoom);    \
    }
OBL_TYPE12_WRITER(WriteType12, false)
OBL_TYPE12_WRITER(WriteType12Zoom, true)
#undef OBL_TYPE12_WRITER

test::CallCounter g_driverGolden("ui.blit-driver-golden");

struct DriverDigest {
    uint32_t hash = 2166136261u;
    template<class T> void Add(const std::vector<T>& values)
    {
        for (const auto value : values) hash = (hash ^ static_cast<uint32_t>(value)) * 16777619u;
    }
    template<class... Buffers> void Count(test::CallCounter& report, const char* detail, const Buffers&... buffers)
    {
        (Add(buffers), ...);
        report.Count();
    }
    void Finish(const char* name, int count)
    {
        OBL_LOG("autotest: blit-digest %s count=%d hash=%u", name, count, hash);
        struct Golden {
            const char* name;
            int count;
            uint32_t hash;
        };
        static constexpr Golden golden[]{
                {"depth", 2000, 688635058u},
                {"lit", 2000, 3401023521u},
                {"ramp", 2000, 3894659359u},
                {"shadow", 2000, 2497740171u},
                {"water", 2000, 3566004927u},
                {"extra", 2000, 2744399243u},
                {"selector", 10, 1293498821u},
        };
        for (const auto& entry : golden)
            if (count == entry.count && std::strcmp(name, entry.name) == 0) {
                g_driverGolden.Record(hash == entry.hash, std::string(name) + " whole-allocation digest");
                return;
            }
    }
};

void DriveDepthCases(int count, bool lit)
{
    DriverDigest digest;
    auto& half = game::GetBlitHalfMask();
    auto& quarter = game::GetBlitQuarterMask();
    const blit::Masks saved{half, quarter};
    uint32_t state = 19;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    uint16_t palette[256];
    constexpr int kColorPitch = 192, kLightPitch = 179, kRows = 40;
    std::vector<uint32_t> dst(kColorPitch * kRows);
    std::vector<uint8_t> light(kLightPitch * kRows);
    auto* renderer = game::GetRenderer();
    const int savedWidth = renderer->m_width;
    auto* savedLight = renderer->m_lightBuffer;
    if (lit) {
        renderer->m_width = kLightPitch;
        renderer->m_lightBuffer = light.data();
    }
    std::vector<uint8_t> rle;
    for (int i = 0; i < count; ++i) {
        half = i & 1 ? 0xf7de : 0x7bde;
        quarter = i & 1 ? 0xe79c : 0x739c;
        for (auto& v : palette) v = static_cast<uint16_t>(next());
        const int w = i < 2 ? 8 : 1 + next() % 131;
        const int h = i < 2 ? 2 : 1 + next() % 23;
        rle.clear();
        for (int row = 0; row < h; ++row) {
            for (int x = 0; x < w;) {
                const int n = i < 2 ? 1 : std::min(w - x, 1 + static_cast<int>(next() % (i % 17 == 0 ? 127 : 19)));
                const bool literal = i < 2 || (next() & 3) != 0;
                rle.push_back(static_cast<uint8_t>(literal ? n : -n));
                if (literal)
                    for (int k = 0; k < n; ++k) rle.push_back(static_cast<uint8_t>(next()));
                x += n;
            }
        }
        const int left = i < 2 || i % 3 == 0 ? 0 : next() % w;
        const int top = i < 2 || i % 4 == 0 ? 0 : next() % h;
        const int right = i < 2 || i % 5 == 0 ? w : left + 1 + next() % (w - left);
        const int bottom = i < 2 || i % 6 == 0 ? h : top + 1 + next() % (h - top);
        const uint32_t depth = i % 7 == 0 ? 0xffff : i % 7 == 1 ? 0 : next();
        const int slant = static_cast<int>(next() % 33) - 16;
        const int dx = i % 11 == 0 ? kLightPitch - w : 4 + (i & 1);
        const int dy = i % 11 == 0 ? kRows - h : 4 + ((i >> 1) & 1);
        // Include equality, occlusion and depth wrap; reset before each variant.
        auto fill = [&] {
            for (auto& v : dst) v = (i % 4 == 0 ? depth << 16 : i % 4 == 1 ? 0 : next() << 8) | static_cast<uint16_t>(next());
        };
        if (lit) {
            for (int variant = 0; variant < 2; ++variant) {
                const std::array entries{pixel_primitives::DepthLit32, pixel_primitives::DepthLitZoomOut32};
                fill();
                for (auto& v : light) v = static_cast<uint8_t>(next());
                renderer->m_lightBuffer = light.data();
                entries[variant](dst.data(), rle.data(), palette, kColorPitch, dx, dy, w, h, left, top, right, bottom, depth, slant);
                digest.Count(g_litVerified, "whole color/depth/light allocations differ", dst, light);
            }
        } else {
            for (int variant = 0; variant < 3; ++variant) {
                const std::array entries{pixel_primitives::Depth32, pixel_primitives::DepthKeyline32, pixel_primitives::DepthZoomOut32};
                fill();
                entries[variant](dst.data(), rle.data(), palette, kColorPitch, dx, dy, w, h, left, top, right, bottom, depth, slant);
                digest.Count(g_depthVerified, "whole color/depth allocation differs", dst);
            }
            for (int variant = 0; variant < 2; ++variant) {
                const std::array entries{pixel_primitives::DepthType12Unscaled32, pixel_primitives::DepthType12ZoomOut32};
                fill();
                entries[variant](dst.data(), rle.data(), palette, kColorPitch, dx, dy, w, h, left, top, right, bottom, depth);
                digest.Count(g_depthVerified, "whole Type12 color/depth allocation differs", dst);
            }
        }
    }
    half = saved.half;
    quarter = saved.quarter;
    if (lit) {
        renderer->m_width = savedWidth;
        renderer->m_lightBuffer = savedLight;
    }
    digest.Finish(lit ? "lit" : "depth", count);
}

void DriveDepth(int count)
{
    DriveDepthCases(count, false);
}
void DriveLit(int count)
{
    DriveDepthCases(count, true);
}

void WriteEffect(uint32_t* dst, const uint8_t* rle, const uint8_t* depthFrame, const uint16_t* palette, blit::RleRect r, uint32_t depth,
        float slope, bool ghost, bool zoom, bool ramp)
{
    if (ramp)
        blit::Ramp(dst, rle, depthFrame, palette, r, depth, slope, CurrentMasks(), ghost, zoom);
    else
        blit::Shadow(dst, rle, r, depth, CurrentMasks(), zoom);
}

#define OBL_RAMP_WRITER(Name, Ghost, Zoom)                                                                                                 \
    void Name(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y, int w, int h,     \
            int l, int t, int r, int b, uint32_t z, float slope)                                                                           \
    {                                                                                                                                      \
        WriteEffect(dst, rle, frame, palette, {pitch, x, y, w, h, l, t, r, b}, z, slope, Ghost, Zoom, true);                               \
    }
OBL_RAMP_WRITER(WriteRamp, false, false)
OBL_RAMP_WRITER(WriteGhost, true, false)
OBL_RAMP_WRITER(WriteRampZoom, false, true)
OBL_RAMP_WRITER(WriteGhostZoom, true, true)
#undef OBL_RAMP_WRITER

#define OBL_SHADOW_WRITER(Name, Zoom)                                                                                                      \
    void Name(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,      \
            int b, uint32_t z)                                                                                                             \
    {                                                                                                                                      \
        WriteEffect(dst, rle, nullptr, palette, {pitch, x, y, w, h, l, t, r, b}, z, 0, false, Zoom, false);                                \
    }
OBL_SHADOW_WRITER(WriteShadow, false)
OBL_SHADOW_WRITER(WriteShadowZoom, true)
#undef OBL_SHADOW_WRITER

blit::WaterColors CurrentWaterColors()
{
    return {game::GetBlitWaterQuarterWhite(), game::GetBlitWaterHalfWhite(), game::GetBlitWaterThreeQuarterWhite()};
}

void WriteWater(const uint32_t* src, uint32_t* dst, const uint8_t* mask, blit::WaterRect r, bool half)
{
    blit::Water(dst, src, mask, r, CurrentMasks(), CurrentWaterColors(), half);
}

#define OBL_WATER_WRITER(Name, Size, Half)                                                                                                 \
    void Name(const uint32_t* src, uint32_t sp, uint32_t* dst, int dp, uint32_t sx, uint32_t sy, int dx, int dy, Size w, Size h,           \
            const uint8_t* mask)                                                                                                           \
    {                                                                                                                                      \
        WriteWater(src, dst, mask, {sp, sx, sy, w, h, dp, dx, dy}, Half);                                                                  \
    }
OBL_WATER_WRITER(WriteWaterHalf, uint32_t, true)
OBL_WATER_WRITER(WriteWaterFull, uint16_t, false)
#undef OBL_WATER_WRITER

// Family drivers exercise patched entries and compare whole allocations too, covering
// prefix/suffix guards that the live region snapshot cannot include.
void DriveEffects(int count, bool ramp)
{
    DriverDigest digest;
    auto& half = game::GetBlitHalfMask();
    auto& quarter = game::GetBlitQuarterMask();
    const blit::Masks saved{half, quarter};
    uint32_t state = 37;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    constexpr int kPitch = 192, kRows = 40;
    std::vector<uint32_t> dst(kPitch * kRows);
    std::vector<uint8_t> rle, frame;
    uint16_t palette[256];
    for (int i = 0; i < count; ++i) {
        half = i & 1 ? 0xf7de : 0x7bde;
        quarter = i & 1 ? 0xe79c : 0x739c;
        for (auto& v : palette) v = static_cast<uint16_t>(next());
        const int w = 2 + next() % 129, h = 2 + next() % 22;
        rle.clear();
        frame.clear();
        for (int row = 0; row < h + 2; ++row) {
            for (int x = 0; x < w;) {
                const int n = std::min(w - x, 1 + static_cast<int>(next() % 19));
                const bool literal = (next() & 3) != 0;
                rle.push_back(0);
                frame.push_back(static_cast<uint8_t>(next()));
                rle.push_back(static_cast<uint8_t>(literal ? n : -n));
                frame.push_back(static_cast<uint8_t>(next()));
                if (literal && ramp)
                    for (int k = 0; k < n; ++k) {
                        rle.push_back(static_cast<uint8_t>(next()));
                        frame.push_back(static_cast<uint8_t>(next()));
                    }
                x += n;
            }
        }
        const int left = i % 3 == 0 ? 0 : next() % w;
        const int top = i % 4 == 0 ? 0 : next() % h;
        const int right = i % 5 == 0 ? w : left + 1 + next() % (w - left);
        const int bottom = i % 6 == 0 ? h : top + 1 + next() % (h - top);
        const uint32_t depth = i % 7 == 0 ? 0xffff : i % 7 == 1 ? 0 : next();
        constexpr float slopes[]{0, 1, -1, 0.1f, -0.1f, 257.125f, -257.125f};
        const float slope = slopes[i % 7];
        for (int variant = 0; variant < (ramp ? 4 : 2); ++variant) {
            for (auto& v : dst) v = (i % 4 == 0 ? depth << 16 : i % 4 == 1 ? 0 : next() << 8) | static_cast<uint16_t>(next());
            const bool zoom = ramp ? variant >= 2 : variant == 1;
            blit::RleRect rect{kPitch, 4 + (i & 1), 4 + ((i >> 1) & 1), w, h, left, top, right, bottom};
            if (ramp) {
                const std::array entries{pixel_primitives::DepthRamp32, pixel_primitives::DepthRampGhost32,
                        pixel_primitives::DepthRampZoomOut32, pixel_primitives::DepthRampGhostZoomOut32};
                auto raw = rect;
                entries[variant](dst.data(), rle.data(), frame.data(), palette, raw.pitch, raw.x, raw.y, w, h, left, top, right, bottom,
                        depth, slope);
                digest.Count(g_rampVerified, "whole Ramp/Ghost color/depth allocation differs", dst);
            } else {
                const auto entry = zoom ? pixel_primitives::ShadowZoomOut32 : pixel_primitives::Shadow32;
                entry(dst.data(), rle.data(), nullptr, rect.pitch, rect.x, rect.y, w, h, left, top, right, bottom, depth);
                digest.Count(g_shadowVerified, "whole Shadow color/depth allocation differs", dst);
            }
        }
    }
    half = saved.half;
    quarter = saved.quarter;
    digest.Finish(ramp ? "ramp" : "shadow", count);
}
void DriveRamp(int count)
{
    DriveEffects(count, true);
}
void DriveShadow(int count)
{
    DriveEffects(count, false);
}

void DriveWater(int count)
{
    DriverDigest digest;
    auto& half = game::GetBlitHalfMask();
    auto& quarter = game::GetBlitQuarterMask();
    auto& whiteQ = game::GetBlitWaterQuarterWhite();
    auto& whiteH = game::GetBlitWaterHalfWhite();
    auto& whiteT = game::GetBlitWaterThreeQuarterWhite();
    const blit::Masks saved{half, quarter};
    const auto savedColors = CurrentWaterColors();
    uint32_t state = 43;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    std::vector<uint32_t> src(173 * 40), dst(192 * 40);
    std::vector<uint8_t> mask(src.size());
    for (int i = 0; i < count; ++i) {
        half = i & 1 ? 0xf7de : 0x7bde;
        quarter = i & 1 ? 0xe79c : 0x739c;
        whiteQ = (0xffff & quarter) >> 2;
        whiteH = (0xffff & half) >> 1;
        whiteT = whiteH + whiteQ;
        for (size_t k = 0; k < src.size(); ++k) {
            src[k] = next() * 257u;
            mask[k] = k % 5;
        }
        const uint32_t w = i % 17 == 0 ? 0 : 1 + next() % 131, h = i % 19 == 0 ? 0 : 1 + next() % 23;
        const uint8_t* activeMask = i % 3 == 0 ? nullptr : mask.data();
        for (bool halfRes : {false, true}) {
            for (auto& v : dst) v = next() * 257u;
            blit::WaterRect rect{173, 3, 2, w, h, 192, 4 + (i & 1), 4 + ((i >> 1) & 1)};
            if (!halfRes) {
                // Full truncates source scalar slots to 16 bits.
                rect.srcPitch |= 0x12340000;
                rect.srcX |= 0xabcd0000;
                rect.srcY |= 0x56780000;
            }
            auto invoke = [&](const uint32_t* input, uint32_t* output, blit::WaterRect r) {
                if (halfRes)
                    pixel_primitives::WaterHalfRes(
                            input, r.srcPitch, output, r.dstPitch, r.srcX, r.srcY, r.dstX, r.dstY, r.width, r.height, activeMask);
                else
                    pixel_primitives::WaterFullRes(input, r.srcPitch, output, r.dstPitch, r.srcX, r.srcY, r.dstX, r.dstY,
                            static_cast<uint16_t>(r.width), static_cast<uint16_t>(r.height), activeMask);
            };
            invoke(src.data(), dst.data(), rect);
            digest.Count(g_waterVerified, "whole Water color/depth/source allocations differ", dst, src);
            for (int alias = 0; alias < 7; ++alias) {
                for (auto& v : dst) v = next() * 257u;
                // Exercise exact aliases, horizontal/vertical shifts both ways,
                // different pitches, and distinct base pointers into one allocation.
                blit::WaterRect overlap{192, 4, 4, w, h, 192, 4, 4};
                if (alias == 1) overlap.dstX += 1;
                if (alias == 2) overlap.dstX -= 1;
                if (alias == 3) overlap.dstY += 1;
                if (alias == 4) overlap.dstY -= 1;
                if (alias == 5) overlap.srcPitch = 173;
                const int base = alias == 6 ? 1 : 0;
                if (halfRes) {
                    overlap.dstX *= 2;
                    overlap.dstY *= 2;
                }
                invoke(dst.data() + base, dst.data(), overlap);
                char detail[100];
                std::snprintf(detail, sizeof(detail), "Water alias=%d half=%d case=%d mask=%d", alias, halfRes, i, activeMask != nullptr);
                digest.Count(g_waterAliasVerified, detail, dst);
            }
        }
    }
    half = saved.half;
    quarter = saved.quarter;
    whiteQ = savedColors.quarterWhite;
    whiteH = savedColors.halfWhite;
    whiteT = savedColors.threeQuarterWhite;
    digest.Finish("water", count);
}

// Runs on the game thread, restoring the two masks before returning to game rendering.
void Drive(int count)
{
    auto& half = game::GetBlitHalfMask();
    auto& quarter = game::GetBlitQuarterMask();
    const blit::Masks saved{half, quarter};
    uint32_t state = 7;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    uint16_t palette[256];
    std::vector<uint16_t> dst16(80 * 32);
    std::vector<uint32_t> dst32(80 * 32);
    std::vector<uint8_t> bitmap(80 * 32), rle;
    uint32_t hash = 2166136261u;
    auto hashBuffer = [&](const auto& buffer) {
        for (auto pixel : buffer) hash = (hash ^ pixel) * 16777619u;
    };
    for (int i = 0; i < count; ++i) {
        half = i & 1 ? 0xf7de : 0x7bde;
        quarter = i & 1 ? 0xe79c : 0x739c;
        for (auto& v : palette) v = static_cast<uint16_t>(next());
        for (auto& v : dst16) v = static_cast<uint16_t>(next());
        for (auto& v : dst32) v = next() * 257u;
        for (auto& v : bitmap) v = static_cast<uint8_t>(next() % 256);
        // Mix transparent runs, long literals and zero controls; clipping crosses run boundaries.
        const int w = 1 + static_cast<int>(next() % 55), h = 1 + static_cast<int>(next() % 16);
        rle.clear();
        for (int row = 0; row < h; ++row) {
            int x = 0;
            while (x < w) {
                const int length = std::min(w - x, 1 + static_cast<int>(next() % 17));
                rle.push_back(0);
                const bool literal = (next() & 3) != 0;
                rle.push_back(static_cast<uint8_t>(literal ? length : -length));
                if (literal)
                    for (int k = 0; k < length; ++k) rle.push_back(static_cast<uint8_t>(next()));
                x += length;
            }
        }
        const int left = i % 3 == 0 ? 0 : static_cast<int>(next() % w);
        const int top = i % 4 == 0 ? 0 : static_cast<int>(next() % h);
        const int right = i % 5 == 0 ? w : left + 1 + static_cast<int>(next() % (w - left));
        const int bottom = i % 6 == 0 ? h : top + 1 + static_cast<int>(next() % (h - top));
        // Exercise the native facade with the same case order and allocation digests.
        for (auto address : {pixel_primitives::Ui16, pixel_primitives::Translucent16}) {
            address(dst16.data(), rle.data(), palette, 80, 3, 2, w, h, left, top, right, bottom);
            hashBuffer(dst16);
        }
        for (auto address : {pixel_primitives::Ui32, pixel_primitives::Translucent32}) {
            address(dst32.data(), rle.data(), palette, 80, 3, 2, w, h, left, top, right, bottom);
            hashBuffer(dst32);
        }
        pixel_primitives::Bitmap8Keyed16(bitmap.data(), 80, dst16.data(), 160, 4, 3, 5, 7, w, h, palette);
        hashBuffer(dst16);
        for (auto address : {pixel_primitives::Bitmap8Opaque32, pixel_primitives::Bitmap8Keyed32}) {
            address(bitmap.data(), 80, dst32.data(), 80, 4, 3, 5, 7, w, h, palette);
            hashBuffer(dst32);
        }
        pixel_primitives::Bitmap8KeyedMasked32(
                bitmap.data(), 80, dst32.data(), 80, 4, 3, 5, 7, w, h, palette, static_cast<uint16_t>(next()));
        hashBuffer(dst32);
    }
    half = saved.half;
    quarter = saved.quarter;
    OBL_LOG("autotest: blit-drive hash=%u cases=%d", hash, count);
}

// Counters remain shared by the facade and the retained sprite ABI bridges.
test::CallCounter g_extraVerified("ui.blit-extra-verified");
test::CallCounter g_selectorVerified("ui.blit-selector-verified");
test::CallCounter g_callsActor("ui.blit-calls-actor-select"), g_callsLit("ui.blit-calls-depth-lit-select");
test::CallCounter g_callsTrans("ui.blit-calls-translucent-select"), g_callsType2("ui.blit-calls-type2");
test::CallCounter g_callsBytes("ui.blit-calls-rle-to-bytes8"), g_callsBytesZoom("ui.blit-calls-rle-to-bytes8ZoomOut");
test::CallCounter g_callsMax("ui.blit-calls-max-bytes8"), g_callsClear("ui.blit-calls-rle-copy32");
test::CallCounter g_callsHalf("ui.blit-calls-get-mask-half"), g_callsQuarter("ui.blit-calls-get-mask-quarter");

uint16_t WriteHalfMask()
{
    g_callsHalf.Count();

    return game::GetBlitHalfMask();
}
uint16_t WriteQuarterMask()
{
    g_callsQuarter.Count();

    return game::GetBlitQuarterMask();
}

void WriteActorSelect(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t,
        int r, int b, uint32_t z, int slant, char lit, char unused, char keyline)
{
    g_callsActor.Count();
    const bool zoom = game::GetRenderer()->m_zoomLevel == 2;
    const auto kind = lit ? blit::DepthKind::Lit
            : zoom        ? blit::DepthKind::Actor
            : keyline     ? blit::DepthKind::Keyline
                          : blit::DepthKind::Actor;
    WriteDepth(dst, rle, palette, {pitch, x, y, w, h, l, t, r, b}, z, slant, kind, zoom);
}
void WriteLitSelect(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,
        int b, uint32_t z, int slant)
{
    g_callsLit.Count();
    WriteDepth(dst, rle, palette, {pitch, x, y, w, h, l, t, r, b}, z, slant, blit::DepthKind::Lit, game::GetRenderer()->m_zoomLevel == 2);
}

void WriteTransSelect(
        uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b)
{
    g_callsTrans.Count();
    if (!game::GetRenderer()->m_directMode) {
        WriteRle(dst, rle, palette, {pitch, x, y, w, h, l, t, r, b}, true);
        return;
    }
    auto* renderer = game::GetRenderer();
    auto* bits = obl::ui::renderer::GetBackBufferBits(renderer);
    if (!bits) return;
    int origin[2];
    obl::ui::renderer::GetSurfaceOrigin(renderer, origin);
    const auto bytes = obl::ui::renderer::GetSurfacePitch(renderer);
    // Lock exactly once, also on DirectDraw.
    WriteRle(bits, rle, palette, {static_cast<int>(bytes >> 1), x - origin[0], y - origin[1], w, h, l, t, r, b}, true);
    obl::ui::renderer::UnlockSurface(renderer);
}

void WriteType2(
        uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b)
{
    g_callsType2.Count();

    blit::Type2(dst, rle, palette, {pitch, x, y, w, h, l, t, r, b});
}

#define OBL_BYTE_WRITER(Name, Calls, Operation)                                                                                            \
    void Name(uint8_t* dst, const uint8_t* src, uint32_t unused, int pitch, int x, int y, int w, int h, int l, int t, int r, int b)        \
    {                                                                                                                                      \
        Calls.Count();                                                                                                                     \
        const blit::RleRect rect{pitch, x, y, w, h, l, t, r, b};                                                                           \
        auto* out = dst;                                                                                                                   \
        Operation;                                                                                                                         \
    }
OBL_BYTE_WRITER(WriteBytes, g_callsBytes, blit::RleToBytes(out, src, rect, false))
OBL_BYTE_WRITER(WriteBytesZoom, g_callsBytesZoom, blit::RleToBytes(out, src, rect, true))
#undef OBL_BYTE_WRITER
void WriteMaxBytes(
        uint8_t* dst, const uint8_t* src, uint32_t, int pitch, int x, int y, int sourcePitch, int unusedHeight, int l, int t, int r, int b)
{
    g_callsMax.Count();

    // The seventh and eighth fields are the source pitch and the unused height, never destination extents.
    blit::MaxBytes(dst, src, {pitch, x, y, sourcePitch, unusedHeight, l, t, r, b});
}
void WriteClear(uint8_t* dst, const uint8_t* src, int pitch, int x, int y, int w, int h, const int* clip)
{
    g_callsClear.Count();

    blit::RleClear(dst, src, {pitch, x, y, w, h, clip[0], clip[1], clip[2], clip[3]});
}

void DriveExtraBlit(int count)
{
    DriverDigest digest;
    auto* renderer = game::GetRenderer();
    const auto saved = *renderer;
    auto& half = game::GetBlitHalfMask();
    auto& quarter = game::GetBlitQuarterMask();
    const auto masks = CurrentMasks();
    uint32_t state = 137;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    constexpr int pitch = 192, lightPitch = 179, rows = 40;
    std::vector<uint32_t> dst(pitch * rows);
    std::vector<uint16_t> surface(pitch * rows);
    std::vector<uint8_t> light(lightPitch * rows), bytes(pitch * rows);
    std::vector<uint8_t> source(173 * rows), rle, runs;
    uint16_t palette[256];
    ui::RendererSurface fake{};
    renderer->m_surfaces = {&fake};
    renderer->m_activeSurface = 0;
    renderer->m_windowed = 1;
    renderer->m_width = lightPitch;
    renderer->m_lightBuffer = light.data();
    fake.m_pitch = pitch * 2;
    fake.m_originX = 3;
    fake.m_originY = -2;
    for (int i = 0; i < count; ++i) {
        half = i & 1 ? 0xf7de : 0x7bde;
        quarter = i & 1 ? 0xe79c : 0x739c;
        for (auto& v : palette) v = static_cast<uint16_t>(next());
        for (auto& v : dst) v = next() * 257u;
        for (auto& v : surface) v = static_cast<uint16_t>(next());
        for (auto& v : light) v = static_cast<uint8_t>(next());
        for (auto& v : bytes) v = static_cast<uint8_t>(next());
        for (auto& v : source) v = static_cast<uint8_t>(next());
        const int w = 1 + next() % 131, h = 1 + next() % 23;
        rle.clear();
        runs.clear();
        for (int row = 0; row < h; ++row) {
            for (int x = 0; x < w;) {
                const int n = std::min(w - x, 1 + static_cast<int>(next() % (i % 17 == 0 ? 127 : 19)));
                const bool literal = (next() & 3) != 0;
                rle.push_back(static_cast<uint8_t>(literal ? n : -n));
                runs.push_back(static_cast<uint8_t>(literal ? n : -n));
                if (literal)
                    for (int k = 0; k < n; ++k) rle.push_back(static_cast<uint8_t>(next()));
                x += n;
            }
        }
        const int l = i % 3 == 0 ? 0 : next() % w, t = i % 4 == 0 ? 0 : next() % h;
        const int r = i % 5 == 0 ? w : l + 1 + next() % (w - l), b = i % 6 == 0 ? h : t + 1 + next() % (h - t);
        const int dx = 8 + (i & 1), dy = 4 + ((i >> 1) & 1);
        const uint32_t z = i % 7 == 0 ? 0xffff : i % 7 == 1 ? 0 : next();
        const int slant = static_cast<int>(next() % 33) - 16;
        char detail[200];
        std::snprintf(detail, sizeof(detail), "extra case=%d size=%dx%d clip=%d,%d,%d,%d", i, w, h, l, t, r, b);
        // Whole allocations include row gaps, leading/trailing guards, and light.
        for (int mode = 0; mode < 3; ++mode)
            for (int rgb = 0; rgb < 2; ++rgb) {
                renderer->m_zoomLevel = mode;
                renderer->m_rgb555 = rgb;
                for (int flags = 0; flags < 8; ++flags) {
                    renderer->m_lightBuffer = light.data();
                    pixel_primitives::ActorSelect(dst.data(), rle.data(), palette, pitch, dx, dy, w, h, l, t, r, b, z, slant,
                            flags & 1 ? static_cast<char>(0x80) : 0, flags & 2 ? 1 : 0, flags & 4 ? 2 : 0);
                    digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
                }
                renderer->m_lightBuffer = light.data();
                pixel_primitives::DepthLitSelect(dst.data(), rle.data(), palette, pitch, dx, dy, w, h, l, t, r, b, z, slant);
                digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
            }
        for (int direct = 0; direct < 3; ++direct) {
            renderer->m_directMode = direct != 0;
            renderer->m_surfaceLocked = 0;
            fake.m_cleared = 7;
            fake.m_bits = direct == 2 ? nullptr : surface.data();
            pixel_primitives::TranslucentSelect(dst.data(), rle.data(), palette, pitch, dx, dy, w, h, l, t, r, b);
            if (renderer->m_surfaceLocked != (direct == 2 ? 1 : 0) || fake.m_cleared != (direct == 0 ? 7 : 0))
                g_driverGolden.Record(false, "translucent selector lock/handed-out contract");
            digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
        }
        pixel_primitives::Type2(dst.data(), rle.data(), palette, pitch, dx, dy, w, h, l, t, r, b);
        digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
        for (int kind = 0; kind < 3; ++kind) {
            const auto address = kind == 0 ? pixel_primitives::RleToBytes8
                    : kind == 1            ? pixel_primitives::RleToBytes8ZoomOut
                                           : pixel_primitives::MaxBytes8;
            auto* src = kind == 2 ? source.data() : rle.data();
            const int width = kind == 2 ? 173 : w;
            const auto sourceBefore = source, rleBefore = rle;
            address(bytes.data(), src, 0xdeadbeef, pitch, dx, dy, width, h, l, t, r, b);
            if (source != sourceBefore || rle != rleBefore) g_driverGolden.Record(false, "byte blitter changed its source");
            digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
        }
        const screen::Rect clip{l, t, r, b};
        pixel_primitives::RleCopy32(bytes.data(), runs.data(), pitch, dx, dy, w, h, clip);
        digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
        const bool halfMatches = pixel_primitives::GetMaskHalf() == half;
        const bool quarterMatches = pixel_primitives::GetMaskQuarter() == quarter;
        digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
        digest.Count(g_extraVerified, detail, dst, surface, light, bytes);
        if (!halfMatches || !quarterMatches) g_driverGolden.Record(false, "mask getter differs from the global");
    }
    *renderer = saved;
    half = masks.half;
    quarter = masks.quarter;
    digest.Finish("extra", count);
}

void DriveSelector(int count)
{
    DriverDigest digest;
    auto* renderer = game::GetRenderer();
    const auto saved = *renderer;
    constexpr int pitch = 31, lightPitch = 37, rows = 12;
    std::vector<uint32_t> dst(pitch * rows);
    std::vector<uint8_t> light(lightPitch * rows);
    uint16_t palette[256]{};
    for (int i = 0; i < 256; ++i) palette[i] = static_cast<uint16_t>(i * 257);
    const uint8_t rle[]{1, 0, 255, 3, 65, 130, 195, 1, 0, 255, 3, 65, 130, 195, 1, 0, 255, 3, 65, 130, 195, 1, 0, 255, 3, 65, 130, 195};
    renderer->m_width = lightPitch;
    for (int i = 0; i < count; ++i)
        for (int zoom = 0; zoom < 3; ++zoom)
            for (int rgb = 0; rgb < 2; ++rgb) {
                renderer->m_zoomLevel = zoom;
                renderer->m_rgb555 = rgb;
                for (int flags = 0; flags < 8; ++flags) {
                    std::fill(dst.begin(), dst.end(), 0x10012345u);
                    std::fill(light.begin(), light.end(), uint8_t{77});
                    renderer->m_lightBuffer = light.data();
                    pixel_primitives::ActorSelect(dst.data(), rle, palette, pitch, 4 + (i & 1), 4, 5, 4, 0, 0, 5, 4, 0x1000, -2,
                            flags & 1 ? 1 : 0, flags & 2 ? 1 : 0, flags & 4 ? 1 : 0);
                    digest.Count(g_selectorVerified, "actor selection/unused lit dispatcher", dst, light);
                }
            }
    *renderer = saved;
    digest.Finish("selector", count);
}

bool RegisterSelectorDrivers()
{
    g_selectorVerified.SetDriver(&DriveSelector);
    g_actorSelect.SetDriver(&DriveExtraBlit);
    return true;
}

} // namespace

namespace pixel_primitives {
void ActorSelect(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,
        int b, uint32_t z, int slant, char lit, char unused, char keyline)
{
    WriteActorSelect(dst, rle, palette, pitch, x, y, w, h, l, t, r, b, z, slant, lit, unused, keyline);
}
void DepthLitSelect(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,
        int b, uint32_t z, int slant)
{
    WriteLitSelect(dst, rle, palette, pitch, x, y, w, h, l, t, r, b, z, slant);
}
void TranslucentSelect(
        uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b)
{
    WriteTransSelect(dst, rle, palette, pitch, x, y, w, h, l, t, r, b);
}
void Type2(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b)
{
    WriteType2(dst, rle, palette, pitch, x, y, w, h, l, t, r, b);
}
void RleToBytes8(uint8_t* dst, const uint8_t* src, uint32_t unused, int pitch, int x, int y, int w, int h, int l, int t, int r, int b)
{
    WriteBytes(dst, src, unused, pitch, x, y, w, h, l, t, r, b);
}
void RleToBytes8ZoomOut(
        uint8_t* dst, const uint8_t* src, uint32_t unused, int pitch, int x, int y, int w, int h, int l, int t, int r, int b)
{
    WriteBytesZoom(dst, src, unused, pitch, x, y, w, h, l, t, r, b);
}
void MaxBytes8(uint8_t* dst, const uint8_t* src, uint32_t unusedArg2, int pitch, int x, int y, int sourcePitch, int unusedHeight, int l,
        int t, int r, int b)
{
    WriteMaxBytes(dst, src, unusedArg2, pitch, x, y, sourcePitch, unusedHeight, l, t, r, b);
}
void RleCopy32(uint8_t* dst, const uint8_t* src, int pitch, int x, int y, int w, int h, screen::Rect clip)
{
    const int bounds[]{clip.left, clip.top, clip.right, clip.bottom};
    WriteClear(dst, src, pitch, x, y, w, h, bounds);
}
uint16_t GetMaskHalf()
{
    return WriteHalfMask();
}
uint16_t GetMaskQuarter()
{
    return WriteQuarterMask();
}
void Ui16(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left, int top,
        int right, int bottom)
{
    WriteUi16(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom);
}
void Ui32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left, int top,
        int right, int bottom)
{
    WriteUi32(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom);
}
void Translucent16(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom)
{
    WriteTranslucent16(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom);
}
void Translucent32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom)
{
    WriteTranslucent32(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom);
}
void Bitmap8Keyed16(
        const uint8_t* src, int sp, uint16_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height, const uint16_t* palette)
{
    WriteBitmap16(src, sp, dst, dp, sx, sy, dx, dy, width, height, palette);
}
void Bitmap8Opaque32(
        const uint8_t* src, int sp, uint32_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height, const uint16_t* palette)
{
    WriteOpaque32(src, sp, dst, dp, sx, sy, dx, dy, width, height, palette);
}
void Bitmap8Keyed32(
        const uint8_t* src, int sp, uint32_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height, const uint16_t* palette)
{
    WriteKeyed32(src, sp, dst, dp, sx, sy, dx, dy, width, height, palette);
}
void Bitmap8KeyedMasked32(const uint8_t* src, int sp, uint32_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height,
        const uint16_t* palette, uint16_t mask)
{
    WriteMasked32(src, sp, dst, dp, sx, sy, dx, dy, width, height, palette, mask);
}
void Depth32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left, int top,
        int right, int bottom, uint32_t depth, int slant)
{
    WriteDepth32(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom, depth, slant);
}
void DepthKeyline32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant)
{
    WriteKeyline32(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom, depth, slant);
}
void DepthZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant)
{
    WriteDepthZoom32(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom, depth, slant);
}
void DepthType12Unscaled32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height,
        int left, int top, int right, int bottom, uint32_t depth)
{
    WriteType12(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom, depth);
}
void DepthType12ZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height,
        int left, int top, int right, int bottom, uint32_t depth)
{
    WriteType12Zoom(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom, depth);
}
void DepthLit32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant)
{
    WriteLit32(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom, depth, slant);
}
void DepthLitZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant)
{
    WriteLitZoom32(dst, rle, palette, pitch, x, y, width, height, left, top, right, bottom, depth, slant);
}
void DepthRamp32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y, int w, int h,
        int l, int t, int r, int b, uint32_t z, float slope)
{
    WriteRamp(dst, rle, frame, palette, pitch, x, y, w, h, l, t, r, b, z, slope);
}
void DepthRampGhost32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y, int w,
        int h, int l, int t, int r, int b, uint32_t z, float slope)
{
    WriteGhost(dst, rle, frame, palette, pitch, x, y, w, h, l, t, r, b, z, slope);
}
void DepthRampZoomOut32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y, int w,
        int h, int l, int t, int r, int b, uint32_t z, float slope)
{
    WriteRampZoom(dst, rle, frame, palette, pitch, x, y, w, h, l, t, r, b, z, slope);
}
void DepthRampGhostZoomOut32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y,
        int w, int h, int l, int t, int r, int b, uint32_t z, float slope)
{
    WriteGhostZoom(dst, rle, frame, palette, pitch, x, y, w, h, l, t, r, b, z, slope);
}
void Shadow32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b,
        uint32_t z)
{
    WriteShadow(dst, rle, palette, pitch, x, y, w, h, l, t, r, b, z);
}
void ShadowZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,
        int b, uint32_t z)
{
    WriteShadowZoom(dst, rle, palette, pitch, x, y, w, h, l, t, r, b, z);
}
void WaterHalfRes(const uint32_t* src, uint32_t sp, uint32_t* dst, int dp, uint32_t sx, uint32_t sy, int dx, int dy, uint32_t w, uint32_t h,
        const uint8_t* mask)
{
    WriteWaterHalf(src, sp, dst, dp, sx, sy, dx, dy, w, h, mask);
}
void WaterFullRes(const uint32_t* src, uint32_t sp, uint32_t* dst, int dp, uint32_t sx, uint32_t sy, int dx, int dy, uint16_t w, uint16_t h,
        const uint8_t* mask)
{
    WriteWaterFull(src, sp, dst, dp, sx, sy, dx, dy, w, h, mask);
}
} // namespace pixel_primitives

bool RegisterBlitDrivers()
{
    if (!RegisterSelectorDrivers()) return false;
    g_ramp.SetDriver(&DriveRamp);
    g_shadow.SetDriver(&DriveShadow);
    g_waterFull.SetDriver(&DriveWater);
    g_ui32.SetDriver(&Drive);
    g_depth32.SetDriver(&DriveDepth);
    g_lit32.SetDriver(&DriveLit);
    return true;
}

} // namespace obl::ui
