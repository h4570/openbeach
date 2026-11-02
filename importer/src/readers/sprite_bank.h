#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace obl::convert {

struct SpriteAnimRecord {
    std::string name; // up to the first NUL
    uint16_t frames = 0;
    uint8_t directions = 0;
    uint16_t width = 0, height = 0;
    int16_t hotspotX = 0, hotspotY = 0;
    uint32_t offset = 0; // of the block in BLSprite.dat
    uint32_t size = 0; // of the pixel data, without the two tables in front of it
    uint16_t uniqueFrames = 0;

    // Bytes of the block: frame index table, frame offset table, pixel data.
    uint64_t BlockSize() const { return 2ull * frames * directions + 4ull * uniqueFrames + size; }
};

struct SpriteRecord {
    std::string name; // up to the first NUL
    uint32_t type = 0; // 1 plain, 2 (one sprite), 3 character, 5 building, 6 object with variants
    std::array<uint8_t, 4> extra{}; // type 5 only
    std::vector<std::array<uint8_t, 768>> palettes; // 256 RGB colors each as stored; types 3 and 6 use the first 64
    std::vector<SpriteAnimRecord> anims;

    int PaletteColors() const { return type == 3 || type == 6 ? 64 : 256; }
};

struct SpriteBank {
    std::string stamp; // the 20-byte build stamp up to its NUL, the same at the start of BLSprite.dat
    uint32_t version = 0;
    std::vector<SpriteRecord> sprites;
};

// Parses BLSprite.hdr; false with `error` on a truncated file, trailing bytes or an unrecognized sprite type.
bool ParseSpriteBank(std::span<const uint8_t> header, SpriteBank& bank, std::string& error);

// Receives each unique frame in order: width * height 8-bit gray + alpha pairs, rows top down (gray = palette index or
// plane value, alpha 0 transparent / 255 opaque). The span is valid only during the call.
using SpriteFrameSink = std::function<bool(int uniqueFrame, std::span<const uint8_t> pixels, std::string& error)>;

bool DecodeSpriteAnim(const SpriteAnimRecord& anim, std::span<const uint8_t> block, bool mask, std::vector<uint16_t>& frameMap,
        int& runLength, const SpriteFrameSink& sink, std::string& error);

} // namespace obl::convert
