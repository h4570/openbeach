#pragma once

#include "game/map/path_tile_script.h"
#include "game/map/path_tiles_state.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// Making and releasing the path tiles, their autotile lookup and the four tile getters
// (docs/architecture/data-formats/game-data.md). Pure logic over path::PathTiles: the sprite bank, converted metadata and the
// current-tiles pointer belong to Env, so unit tests drive it with a fake.
//
//   Env: SpriteHandle CreateSprite(const char* name)        a sprite instance of the bank
//        void SetExternalFrame(SpriteHandle)              the renderer draws the frame the tiles pick (SpriteInstance::m_animType 3)
//        const char* SpriteName(SpriteHandle)              the sprite's name
//        void ReleaseSprite(SpriteHandle)                  returns it to the bank
//        void ReadLookup(const char* spriteName, uint8_t (&table)[256], int32_t& fullFrame)  converted tileset metadata
//        void SetGlobal(path::PathTiles*)                  the current-tiles pointer (game::Context)
namespace obl::path_tiles {

inline constexpr const char* kDefaultPavement = "PathTile_10";
inline constexpr const char* kDefaultBeachEdge = "PathTile_10b";
inline constexpr const char* kGrassSprite = "Grass08";

// Surface variants in the order the constructor adds them; the number stored in a cell is the index + 1.
inline constexpr const char* kSurfaceNames[] = {
        "DarkSquares", "PaleSquares", "Tarmac", "PaleHerring", "DarkHerring", "BlueHerring", "Grass08", "QuadSand", "QuadBase"};

namespace detail {

inline int Spaces(const char* p)
{
    int n = 0;
    while (path_tile_script::IsSpace(p[n])) ++n;
    return n;
}

// The whitespace/comment run skipped in front of the first record and after every record: whitespace, then one
// comment line (a lone `/` counts), the whitespace behind it, any further comment lines and whitespace again.
inline int Gap(const char* p)
{
    const int lead = Spaces(p);
    const char* q = p + lead;
    if (q[0] != '/') return lead;
    int line = 1; // the `/` itself is never tested for LF
    while (q[line] != '\n') ++line;
    const char* after = q + line;
    const int ws = Spaces(after);
    const int comments = path_tile_script::SkipComments(after + ws);
    const int tail = Spaces(after + ws + comments);
    return lead + line + ws + comments + tail;
}

} // namespace detail

// Parser for the converter tests and synthetic fixtures; the engine loads JSON.
// The walk over decoded lookup text of `size` bytes: records of eight neighbour bits (N is bit 0) and a frame, stored as
// one byte at table[mask]; later duplicates win. A record with a bit above 1 would index past the table and is dropped.
inline void ParseLookup(const char* text, uint32_t size, uint8_t (&table)[256])
{
    int64_t consumed = detail::Gap(text);
    const char* at = text + consumed;
    std::string token;
    while (consumed < static_cast<int64_t>(size)) {
        int mask = 0;
        for (int bit = 0; bit < 8; ++bit) {
            const int used = path_tile_script::ReadToken(at, token);
            at += used;
            consumed += used;
            mask += std::atoi(token.c_str()) << bit;
        }
        const int used = path_tile_script::ReadToken(at, token);
        const int frame = std::atoi(token.c_str());
        if (mask >= 0 && mask < 256) table[mask] = static_cast<uint8_t>(frame);
        const int gap = detail::Gap(at + used);
        at += used + gap;
        consumed += used + gap;
    }
}

template<class Env> void AddSurface(path::PathTiles& m, Env& env, const char* name)
{
    m.m_surfaces[m.m_surfaceCount] = env.CreateSprite(name);
    m.m_surfaceCount += 1;
}

// The selected pavement supplies its autotile lookup and full-tile frame; null pavement/beach names select the defaults.
template<class Env> void Construct(path::PathTiles& m, Env& env, const char* pavement, const char* beachEdge)
{
    env.SetGlobal(&m);
    m.m_pavement = env.CreateSprite(pavement ? pavement : kDefaultPavement);
    env.SetExternalFrame(m.m_pavement);
    m.m_grass = env.CreateSprite(kGrassSprite);
    env.SetExternalFrame(m.m_grass);
    m.m_beachEdge = env.CreateSprite(beachEdge ? beachEdge : kDefaultBeachEdge);
    env.SetExternalFrame(m.m_beachEdge);
    m.m_pavementName = env.SpriteName(m.m_pavement);
    m.m_grassName = env.SpriteName(m.m_grass);
    std::memset(m.m_lookup, 0, sizeof(m.m_lookup));
    m.m_fullTileFrame = kFullTileFrame;
    env.ReadLookup(m.m_pavementName.c_str(), m.m_lookup, m.m_fullTileFrame);
    m.m_lastSurface = 0;
    m.m_surfaceCount = 0;
    m.m_grassSurfaceNumber = kNoGrassSurface;
    for (const char* name : kSurfaceNames) {
        AddSurface(m, env, name);
        if (std::strcmp(name, kGrassSprite) == 0) m.m_grassSurfaceNumber = m.m_surfaceCount;
    }
}

template<class Env> void Destruct(path::PathTiles& m, Env& env)
{
    env.ReleaseSprite(m.m_pavement);
    env.ReleaseSprite(m.m_grass);
    env.ReleaseSprite(m.m_beachEdge);
    for (int32_t i = 0; i < m.m_surfaceCount; ++i) env.ReleaseSprite(m.m_surfaces[i]);
    env.SetGlobal(nullptr);
}

// The two characters after `PathTile_` of the pavement sprite name (`PathTile_10` -> 10), through
// atoi of a three-byte copy.
inline int TilesetNumber(const path::PathTiles& m)
{
    const std::string& name = m.m_pavementName;
    const std::string digits = name.substr(name.size() < 9 ? name.size() : 9, 2);
    return std::atoi(digits.c_str());
}

inline const char* GrassSpriteName(const path::PathTiles& m)
{
    return m.m_grassName.c_str();
}
inline const char* PavementSpriteName(const path::PathTiles& m)
{
    return m.m_pavementName.c_str();
}
inline int FullTileFrame(const path::PathTiles& m)
{
    return m.m_fullTileFrame;
}

} // namespace obl::path_tiles
