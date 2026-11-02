#include "day_night_present.h"

#include <cstddef>
#include <cstring>

namespace obl::day_night {
namespace {

// Table lookup of one pixel. `shift` is the light level (level * 0x80 entries) added to every index with 16-bit wrap, as the game
// adds it in AX/SI/DX. The indices are not masked above the channel width (a set top bit of an RGB555 pixel reads the next run).
struct Tone {
    uint16_t r, g, b;
    uint16_t Sum() const { return static_cast<uint16_t>(r + g + b); }
};

template<bool k555> Tone Lookup(const uint16_t* tones, uint16_t pixel, uint16_t shift)
{
    const uint16_t ri = static_cast<uint16_t>((k555 ? pixel >> 10 : pixel >> 11) + shift);
    const uint16_t gi = static_cast<uint16_t>(((pixel >> 5) & (k555 ? 0x1f : 0x3f)) + shift);
    const uint16_t bi = static_cast<uint16_t>((pixel & 0x1f) + shift);
    return {tones[ri], tones[0x20 + gi], tones[(k555 ? 0x40 : 0x60) + bi]};
}

// Average of a pixel and its left neighbour, per channel, kept in 16-bit registers like the game: RGB555 adds the entries and halves
// the sum, RGB565 halves each red entry first.
template<bool k555> uint16_t Blend(const Tone& a, const Tone& b)
{
    uint16_t red;
    if (k555) {
        red = static_cast<uint16_t>(static_cast<uint16_t>(a.r + b.r) >> 1) & 0x7c00;
    } else {
        red = static_cast<uint16_t>((a.r >> 1) + (b.r >> 1)) & 0xf800;
    }
    const uint16_t blue = static_cast<uint16_t>(static_cast<uint16_t>(a.b + b.b) >> 1) & 0x1f;
    const uint16_t green = static_cast<uint16_t>(static_cast<uint16_t>(a.g + b.g) >> 1) & (k555 ? 0x3e0 : 0x7e0);
    return static_cast<uint16_t>(red + blue + green);
}

inline void Store(uint8_t* row, int pixel, uint16_t value)
{
    std::memcpy(row + pixel * 2, &value, 2);
}

inline uint16_t Color(const uint32_t* p)
{
    return static_cast<uint16_t>(*p & 0xffff);
}

// Zoom 0: one scene pixel becomes two columns (the blend with its left neighbour, then the plain tone) written to two rows.
// `shimmer` null: no horizontal jitter. `light` null: level 0.
template<bool k555> void PresentDoubled(const Frame& f, const uint8_t* shimmer, bool useLight, uint8_t* dest, uint32_t pitch)
{
    const int halfWidth = static_cast<int>(static_cast<uint32_t>(f.width) >> 1);
    const int rows = PresentedRows(f);
    const int stride = halfWidth * 2;
    int shimmerRow = 0;
    for (int y = 0; y < rows; ++y) {
        const uint32_t* src = f.scene + static_cast<std::ptrdiff_t>(y) * stride;
        const uint8_t* lightRow = useLight ? f.light + static_cast<std::ptrdiff_t>(y) * stride : nullptr;
        uint8_t* out0 = dest + static_cast<size_t>(y) * 2 * pitch;
        uint8_t* out1 = out0 + pitch;
        uint16_t lastBlended = 0;
        for (int x = 0; x < halfWidth; ++x) {
            int index = x;
            if (shimmer) index += shimmer[shimmerRow + ((x - halfWidth) & 0x1f)];
            // The light byte of the pixel itself colours its left neighbour's lookup and the next byte colours the pixel (the loop reads
            // the pair as one word at the pixel's own column).
            const uint16_t cur = Color(src + index);
            // The first scene pixel has no left neighbour: the cell before it is outside the buffer and differs per host. Other rows
            // start by blending with the end of the row above.
            const uint16_t prev = Color(y == 0 && index == 0 ? src : src + index - 1);
            const uint16_t curShift = useLight ? static_cast<uint16_t>(lightRow[x + 1] << 7) : 0;
            const uint16_t prevShift = useLight ? static_cast<uint16_t>(lightRow[x] << 7) : 0;
            const Tone c = Lookup<k555>(f.tones, cur, curShift);
            const Tone p = Lookup<k555>(f.tones, prev, prevShift);
            const uint16_t blended = Blend<k555>(c, p);
            const uint16_t plain = c.Sum();
            Store(out0, x * 2, blended);
            Store(out0, x * 2 + 1, plain);
            Store(out1, x * 2, blended);
            Store(out1, x * 2 + 1, plain);
            lastBlended = blended;
        }
        // The RGB565 light loop ends each row with a store of BX where the other loops store BX >> 16 (a slip in the game): the last
        // column of the first of the two rows shows the blended value instead of the plain tone.
        if (!k555 && useLight) Store(out0, halfWidth * 2 - 1, lastBlended);
        if (shimmer) {
            shimmerRow += kShimmerRowBytes;
            if (shimmerRow >= kShimmerBytes) shimmerRow = 0;
        }
    }
}

// Other zoom levels: 1:1, two scene pixels per loop pass. HeatWave jitters the pair's pixels independently (the first one reads shimmer
// entry `column + 1`, the second `column`).
template<bool k555> void PresentScaled(const Frame& f, const uint8_t* shimmer, bool useLight, uint8_t* dest, uint32_t pitch)
{
    const int rows = PresentedRows(f);
    int shimmerRow = 0;
    for (int y = 0; y < rows; ++y) {
        const uint32_t* src = f.scene + static_cast<std::ptrdiff_t>(y) * f.width;
        const uint8_t* lightRow = useLight ? f.light + static_cast<std::ptrdiff_t>(y) * f.width : nullptr;
        uint8_t* out = dest + static_cast<size_t>(y) * pitch;
        for (int x = 0; x < f.width; x += 2) {
            int first = x, second = x + 1;
            if (shimmer) {
                first += shimmer[shimmerRow + (((x - f.width) & 0x1f) + 1)];
                second += shimmer[shimmerRow + ((x - f.width) & 0x1f)];
            }
            const uint16_t firstShift = useLight ? static_cast<uint16_t>(lightRow[x] << 7) : 0;
            const uint16_t secondShift = useLight ? static_cast<uint16_t>(lightRow[x + 1] << 7) : 0;
            Store(out, x, Lookup<k555>(f.tones, Color(src + first), firstShift).Sum());
            Store(out, x + 1, Lookup<k555>(f.tones, Color(src + second), secondShift).Sum());
        }
        if (shimmer) {
            shimmerRow += kShimmerRowBytes;
            if (shimmerRow >= kShimmerBytes) shimmerRow = 0;
        }
    }
}

} // namespace

Variant ChooseVariant(uint32_t curWeather, bool lightEffects)
{
    if (curWeather == 0) return Variant::NormalWeather;
    return lightEffects ? Variant::Lightmap : Variant::Plain;
}

int PresentedRows(const Frame& f)
{
    const int rows = f.height - f.bottomTrim;
    if (rows <= 0 || f.width <= 0) return 0;
    return f.doubled ? rows / 2 : rows;
}

bool ClearsLightBuffer(Variant variant, bool doubled)
{
    switch (variant) {
    case Variant::Lightmap: return true;
    case Variant::NormalWeather: return doubled;
    case Variant::Plain: return false;
    }
    return false;
}

void Present(Variant variant, const Frame& frame, uint8_t* dest, uint32_t pitchBytes)
{
    if (PresentedRows(frame) == 0 || (frame.doubled && (static_cast<uint32_t>(frame.width) >> 1) == 0)) return;
    const uint8_t* shimmer = variant == Variant::NormalWeather ? frame.shimmer : nullptr;
    const bool useLight = variant == Variant::Lightmap;
    if (frame.doubled) {
        if (frame.rgb555)
            PresentDoubled<true>(frame, shimmer, useLight, dest, pitchBytes);
        else
            PresentDoubled<false>(frame, shimmer, useLight, dest, pitchBytes);
    } else {
        if (frame.rgb555)
            PresentScaled<true>(frame, shimmer, useLight, dest, pitchBytes);
        else
            PresentScaled<false>(frame, shimmer, useLight, dest, pitchBytes);
    }
}

} // namespace obl::day_night
