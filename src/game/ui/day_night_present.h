#pragma once

#include <cstdint>

// Pixel loops of the day/night present (PresentDayNight) and its six variants: the scene buffer (low 16 bits colour, high 16 bits depth)
// goes through the weather tone tables into the 16 bpp back buffer. Pure functions over the buffers the caller gathers from Renderer and
// Weather.
namespace obl::day_night {

// Which loop PresentDayNight picks (before the zoom): HeatWave (`CurWeather` 0) always shimmers, every other weather uses the
// light buffer when [Video] LightEffects is on and the plain tone otherwise.
enum class Variant { NormalWeather, Lightmap, Plain };
Variant ChooseVariant(uint32_t curWeather, bool lightEffects);

// The tone tables (`tones`, 0x100 B per light level) hold 16-bit entries: R at entry 0, G at entry 0x20, B at entry 0x40 (RGB555) or B at entry 0x60 (RGB565,
// 64 G entries). The light buffer byte of a pixel selects the level.
inline constexpr int kShimmerRowBytes = 0x20; // one row of the shimmer table: 32 per-column pixel offsets
inline constexpr int kShimmerBytes = 0x300; // 24 rows; the row advances once per source row and wraps
inline constexpr int kToneTableWords = 0x80;

struct Frame {
    int width = 0; // Renderer+4: scene width in pixels (even)
    int height = 0; // scene view height
    int bottomTrim = 0; // rows at the bottom of the scene that are not presented
    bool rgb555 = true; // the pixel format is RGB555 (otherwise RGB565)
    bool doubled = false; // zoom level 0: the scene is half size and every pixel becomes 2x2 (the first half of each scene row is used)
    const uint32_t* scene = nullptr; // the scene buffer
    const uint8_t* light = nullptr; // the light buffer, one byte per scene pixel (Lightmap only)
    const uint16_t* tones = nullptr; // the weather tone tables
    const uint8_t* shimmer = nullptr; // kShimmerBytes (NormalWeather only)
};

// Writes the presented rows into `dest` (`pitchBytes` per row). Reads one pixel left of each row start but the first and up to 255
// pixels right of the shimmered ones, so `scene` needs that much slack after it. A frame with no
// pixel columns or no rows presents nothing (the game would loop through the whole 32-bit counter).
void Present(Variant variant, const Frame& frame, uint8_t* dest, uint32_t pitchBytes);

// Rows (of `dest`) one Present writes.
int PresentedRows(const Frame& frame);

// NormalWeather and Lightmap at zoom 0, and Lightmap at any zoom, clear the light buffer and set the renderer's "light buffer clean" flags
// afterwards; the other loops leave them alone.
bool ClearsLightBuffer(Variant variant, bool doubled);

} // namespace obl::day_night
