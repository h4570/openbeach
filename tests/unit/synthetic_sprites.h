#pragma once

// A synthetic sprite archive (BLSprite.hdr and .dat, importer/src/readers/sprite_bank.cpp) with our own names and pixels:
// convert_sprite_test.cpp checks the parser and decoder on it, convert_import_test.cpp imports a whole synthetic copy.

#include "importer/src/readers/sprite_bank.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obl::test {

using convert::SpriteAnimRecord;

struct SpriteBytes {
    std::vector<uint8_t> b;
    SpriteBytes& U8(uint32_t v)
    {
        b.push_back(static_cast<uint8_t>(v));
        return *this;
    }
    SpriteBytes& U16(uint32_t v) { return U8(v).U8(v >> 8); }
    SpriteBytes& U32(uint32_t v) { return U16(v).U16(v >> 16); }
    SpriteBytes& Name(const std::string& s)
    {
        U8(static_cast<uint32_t>(s.size() + 1));
        b.insert(b.end(), s.begin(), s.end());
        return U8(0);
    }
    SpriteBytes& Raw(std::initializer_list<int> v)
    {
        for (int x : v) U8(static_cast<uint32_t>(x));
        return *this;
    }
};

// One unique frame of 4 x 2: row 0 "skip 1, three pixels 0 7 9", row 1 "two pixels 5 6, skip 2". Index 0 is opaque.
inline const std::vector<int> kFrameA = {0xff, 3, 0, 7, 9, 2, 5, 6, 0xfe};
// Row 0 "four pixels 1 2 3 4", row 1 "skip 4".
inline const std::vector<int> kFrameB = {4, 1, 2, 3, 4, 0xfc};
// A block of 2 directions x 2 frames over unique frames A, B, A' (A' = A at another offset): map 0 1 2 1.
inline std::vector<uint8_t> Block(SpriteAnimRecord& anim)
{
    SpriteBytes pixels;
    for (int x : kFrameA) pixels.U8(static_cast<uint32_t>(x));
    const uint32_t offB = static_cast<uint32_t>(pixels.b.size());
    for (int x : kFrameB) pixels.U8(static_cast<uint32_t>(x));
    const uint32_t offA2 = static_cast<uint32_t>(pixels.b.size());
    for (int x : kFrameA) pixels.U8(static_cast<uint32_t>(x));
    anim.frames = 2;
    anim.directions = 2;
    anim.width = 4;
    anim.height = 2;
    anim.uniqueFrames = 3;
    anim.size = static_cast<uint32_t>(pixels.b.size());
    SpriteBytes block;
    block.U16(0).U16(1).U16(2).U16(1).U32(0).U32(offB).U32(offA2);
    block.b.insert(block.b.end(), pixels.b.begin(), pixels.b.end());
    return block.b;
}

// A synthetic archive: "Walker" (type 3, two palettes, the animation of Block()) and "HUT" (type 5, planes image and
// pick, extents 1 2 3 4). `stamp` is the build stamp both files start with. Returns hdr and dat.
inline std::pair<std::vector<uint8_t>, std::vector<uint8_t>> Archive(
        const std::string& walkerName = "Walker", std::string_view stamp = "2099:01:01 00:00:00")
{
    SpriteBytes dat;
    dat.b.assign(stamp.begin(), stamp.end());
    dat.b.push_back(0);
    dat.U32(4);
    SpriteAnimRecord walk, image, pick;
    const auto walkBlock = Block(walk);
    const uint32_t walkAt = static_cast<uint32_t>(dat.b.size());
    dat.b.insert(dat.b.end(), walkBlock.begin(), walkBlock.end());
    const auto imageBlock = Block(image);
    const uint32_t imageAt = static_cast<uint32_t>(dat.b.size());
    dat.b.insert(dat.b.end(), imageBlock.begin(), imageBlock.end());
    // The pick mask: the same runs without pixel bytes.
    SpriteBytes pickBlock;
    pickBlock.U16(0).U32(0).Raw({0xff, 3, 2, 0xfe});
    const uint32_t pickAt = static_cast<uint32_t>(dat.b.size());
    dat.b.insert(dat.b.end(), pickBlock.b.begin(), pickBlock.b.end());

    SpriteBytes hdr;
    hdr.b.assign(stamp.begin(), stamp.end());
    hdr.b.push_back(0);
    hdr.U32(4).U32(2);
    auto animHeader = [&](const std::string& name, const SpriteAnimRecord& a, uint32_t at, int16_t hx, int16_t hy) {
        hdr.Name(name).U16(a.frames).U8(a.directions).U16(a.width).U16(a.height).U16(static_cast<uint16_t>(hx));
        hdr.U16(static_cast<uint16_t>(hy)).U32(at).U32(a.size).U16(a.uniqueFrames);
    };
    hdr.Name(walkerName).U32(3).U32(2);
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < 768; ++i) hdr.U8(static_cast<uint32_t>(i * (p + 1)));
    hdr.U32(1);
    animHeader("D_Walk", walk, walkAt, -2, -1);
    hdr.Name("HUT").U32(5).Raw({1, 2, 3, 4}).U32(1);
    for (int i = 0; i < 768; ++i) hdr.U8(static_cast<uint32_t>(255 - i % 256));
    hdr.U32(2);
    animHeader("image", image, imageAt, 0, 0);
    SpriteAnimRecord pickAnim;
    pickAnim.frames = 1;
    pickAnim.directions = 1;
    pickAnim.width = 4;
    pickAnim.height = 2;
    pickAnim.uniqueFrames = 1;
    pickAnim.size = 4;
    animHeader("pick", pickAnim, pickAt, 0, 0);
    return {hdr.b, dat.b};
}

} // namespace obl::test
