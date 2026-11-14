#pragma once

// The converted sprites of the pack (`sprites/<key>.json`, palettes and sheet PNGs; docs/architecture/data-formats/assets.md
// "Sprites") as the engine needs them: the header fields of the bank and, per animation, the run-length frames the blitters
// draw. Pure logic: no Windows APIs, no game layouts.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::sprite_pack {

// Palette width for an engine sprite type and RGB conversion for the renderer.
size_t PaletteSize(uint32_t type);
uint16_t PaletteColor(uint8_t r, uint8_t g, uint8_t b, bool is555);

inline constexpr int kDefaultRunLength = 127;

// Run-length encodes one frame: `pixels` are width * height gray + alpha pairs (alpha 0 transparent, else opaque). Each row
// is a sequence of signed bytes: -n skips n transparent pixels, +n is followed by n pixel bytes (none for a mask). Runs are
void EncodeFrame(std::span<const uint8_t> pixels, int width, int height, bool mask, int runLength, std::vector<uint8_t>& out);

struct Anim {
    std::string name;
    std::string plane; // empty for a plain animation
    uint16_t frames = 0;
    uint8_t directions = 0;
    uint16_t width = 0, height = 0;
    int16_t hotspotX = 0, hotspotY = 0;
    uint16_t uniqueFrames = 0;
    int columns = 0;
    int framesPerPage = 0; // columns * rows of a sheet page
    int runLength = kDefaultRunLength;
    std::vector<std::string> sheets; // relative to the sprites directory
    std::vector<uint16_t> frameMap; // direction * frames + frame -> unique frame, the identity expanded

    bool Mask() const { return plane == "pick" || plane == "shadow"; }
};

struct Autotile {
    std::array<uint8_t, 256> masks{}; // 255 is the no-frame sentinel (-1 in JSON)
    int full = 0;
};

struct Sprite {
    std::string name; // the key
    uint32_t type = 0;
    int toneGroup = 0;
    std::array<uint8_t, 4> depthExtents{};
    int paletteCount = 0; // L1: variant selection consumes random numbers using this count
    std::optional<Autotile> autotile;
    std::vector<std::vector<uint16_t>> palettes; // 64 or 256 colors in the renderer's 555 or 565 format
    std::vector<Anim> anims;
};

// The keys of every `obl.sprite` document in `spritesDir` and its mod layer, sorted and deduplicated. False with `error` when
// neither layer holds any sprites.
bool ListSprites(const std::filesystem::path& spritesDir, std::vector<std::string>& keys, std::string& error);

// Reads `<spritesDir>/<key>.json` and its palettes. False with `error`, naming the file, for a missing or malformed one.
enum class ReadMode { Metadata, Palettes };
// Metadata reads the L1 JSON only and leaves palettes empty; palette and sheet PNGs may be absent.
bool ReadSprite(const std::filesystem::path& spritesDir, std::string_view key, bool is555, Sprite& sprite, std::string& error,
        ReadMode mode = ReadMode::Palettes);

// Encodes the unique frames [first, end) of `anim` from its sheets: appends them to `data` and stores the offset of frame u
// (relative to the start of `data` plus `base`) in offsets[u] (`offsets` holds uniqueFrames entries). Only the pages holding
// the range are decoded.
bool EncodeFrames(const std::filesystem::path& spritesDir, const Anim& anim, int first, int end, uint32_t base, std::vector<uint8_t>& data,
        std::vector<uint32_t>& offsets, std::string& error);

// The frames [first, end) out of `data` and `offsets` (EncodeFrames of frames [0, offsets.size()) with base 0): the bytes and the
// offsets EncodeFrames(first, end, base) would have produced, so that one decode of a sheet can serve several ranges. `outOffsets`
// receives the offsets of frames first..end-1 at the same indices.
inline std::vector<uint8_t> SliceFrames(const std::vector<uint8_t>& data, const std::vector<uint32_t>& offsets, int first, int end,
        uint32_t base, std::vector<uint32_t>& outOffsets)
{
    const size_t begin = offsets[static_cast<size_t>(first)];
    const size_t stop = static_cast<size_t>(end) < offsets.size() ? offsets[static_cast<size_t>(end)] : data.size();
    for (int u = first; u < end; ++u)
        outOffsets[static_cast<size_t>(u)] = base + (offsets[static_cast<size_t>(u)] - static_cast<uint32_t>(begin));
    return std::vector<uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(begin), data.begin() + static_cast<std::ptrdiff_t>(stop));
}

} // namespace obl::sprite_pack
