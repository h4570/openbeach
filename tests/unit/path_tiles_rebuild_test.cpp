// PathTiles rebuilds (src/common/path_tiles_rebuild.h): the order of every call the logic makes into the game, the flag arithmetic of SetSurface and the
// neighbour walks, the early exits of RebuildTile, the loops of RebuildAll/RebuildRect and the overlap rectangle, over a fake Env that records a trace.
// The game result is pinned by the `map-hash` values of tests/path_tiles_rebuild.txt.

#include "game/objects/object_base.h"
#include "unit_test.h"

#include "game/map/path_tiles_rebuild.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace obl;
using object::Scenery;
using path::PathTiles;
using path::SpriteHandle;
using screen::Rect;

namespace {

struct FakeEnv {
    PathTiles& tiles;
    std::map<uint64_t, uint32_t> grid;
    std::vector<std::string> trace;
    int cameraX = 100, cameraY = 200;
    int screenX = 50, screenY = 60; // what WorldToScreen answers
    int drawOffsetX = 3;
    std::vector<object::Scenery*> scenery;
    std::vector<object::Base*> list;
    Rect intersection{0, 0, 0, 0};
    bool intersects = true;
    int8_t mask[4] = {};
    int tileRect[4] = {2, 3, 2, 2}; // x, y, width, height

    explicit FakeEnv(PathTiles& t) : tiles(t) {}

    static uint64_t Key(uint32_t x, uint32_t y) { return (static_cast<uint64_t>(y % 128) << 32) | (x % 128); }
    void Log(const std::string& s) { trace.push_back(s); }
    static std::string Tile(const char* name, uint32_t x, uint32_t y)
    {
        return std::string(name) + "(" + std::to_string(static_cast<int32_t>(x)) + "," + std::to_string(y) + ")";
    }

    uint32_t& Cell(uint32_t x, uint32_t y) { return grid[Key(x, y)]; }
    void Neighbour(uint32_t x, uint32_t y, int direction, uint32_t out[2])
    {
        // A square fake: direction d steps along the compass N, NE, E, SE, S, SW, W, NW.
        static const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        out[0] = x + dx[direction];
        out[1] = y + dy[direction];
        Log("neighbour d" + std::to_string(direction));
    }
    void TileToWorld(uint32_t x, uint32_t y, float out[3])
    {
        out[0] = static_cast<float>(x);
        out[1] = static_cast<float>(y);
        out[2] = 0;
        Log(Tile("world", x, y));
    }
    void WorldToScreen(const float*, int out[2])
    {
        out[0] = screenX;
        out[1] = screenY;
    }
    int CameraX() { return cameraX; }
    int CameraY() { return cameraY; }
    int fullFrame = 67;
    int FullTileFrame() { return fullFrame; }
    const int8_t* FullTileMask(SpriteHandle) { return mask; }
    void RestoreTileBackground(int x, int y, const int8_t* m)
    {
        Log("restore " + std::to_string(x) + "," + std::to_string(y) + (m == mask ? " mask" : " ?"));
    }
    std::map<SpriteHandle, int> frames; // the frame each sprite was last drawn with
    void DrawGroundTile(SpriteHandle sprite, int frame, const float world[3])
    {
        frames[sprite] = frame;
        const char* name = sprite == tiles.m_pavement ? "pavement"
                : sprite == tiles.m_grass             ? "grass"
                : sprite == tiles.m_beachEdge         ? "edge"
                                                      : "surface";
        Log(std::string("draw ") + name + " frame " + std::to_string(frame) + " at " + std::to_string(static_cast<int>(world[0])) + "," +
                std::to_string(static_cast<int>(world[1])));
    }
    void RebuildTile(uint32_t x, uint32_t y, int32_t remove) { Log(Tile("rebuild", x, y) + (remove ? " remove" : "")); }
    void AddNeighbourBits(uint32_t x, uint32_t y) { Log(Tile("add", x, y)); }
    void RemoveNeighbourBits(uint32_t x, uint32_t y) { Log(Tile("remove", x, y)); }
    void UpdateBuildingOverlap(uint32_t x, uint32_t y) { Log(Tile("overlap", x, y)); }
    int DrawOffsetX(SpriteHandle) { return drawOffsetX; }
    void AnimBox(SpriteHandle, int& hotspotY, int& width, int& height)
    {
        hotspotY = -4;
        width = 64;
        height = 32;
    }
    object::Base** Scenery(uint32_t& count)
    {
        count = static_cast<uint32_t>(list.size());
        return list.data();
    }
    bool Intersect(const Rect& a, const Rect& b, Rect& out)
    {
        Log("intersect " + std::to_string(a.left) + "," + std::to_string(a.top) + "," + std::to_string(a.right) + "," +
                std::to_string(a.bottom) + " with " + std::to_string(b.left));
        if (intersects) out = intersection;
        return intersects;
    }
    void Bake(object::Scenery&, const Rect& clip, const float world[3], int maskX, int maskY)
    {
        Log("bake " + std::to_string(clip.left) + "," + std::to_string(clip.top) + " world " + std::to_string(static_cast<int>(world[0])) +
                " mask " + std::to_string(maskX) + "," + std::to_string(maskY));
    }
    void ScreenRectToTileRect(const int*, int& x, int& y, int& w, int& h)
    {
        x = tileRect[0];
        y = tileRect[1];
        w = tileRect[2];
        h = tileRect[3];
    }
    void SetLoadingProgress(float p) { Log("progress " + std::to_string(p)); }
};

struct Fixture {
    PathTiles tiles{};

    Fixture()
    {
        tiles.m_pavement = SpriteHandle{1};
        tiles.m_grass = SpriteHandle{2};
        tiles.m_beachEdge = SpriteHandle{3};
        tiles.m_surfaceCount = 9;
        for (int i = 0; i < 9; ++i) tiles.m_surfaces[i] = static_cast<SpriteHandle>(10 + i);
        tiles.m_lookup[0x01] = 7;
        tiles.m_lookup[0x03] = 9;
    }
};

} // namespace

OBL_TEST_CASE("path_tiles_rebuild.set_surface keeps the other flags and spills a large number")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.Cell(5, 6) = 0xffffffff;
    path_tiles::SetSurface(env, f.tiles, 5, 6, 3);
    OBL_CHECK_EQ(env.Cell(5, 6), 0xf3ffffffu);
    OBL_CHECK_EQ(f.tiles.m_lastSurface, 2);
    env.Cell(1, 1) = 0x00000002;
    path_tiles::SetSurface(env, f.tiles, 1, 1, 0x11);
    OBL_CHECK_EQ(env.Cell(1, 1), 0x11000002u);
    OBL_CHECK_EQ(f.tiles.m_lastSurface, 0x10);
}

OBL_TEST_CASE(
        "path_tiles_rebuild.add_neighbour_bits sets bit i on the neighbour in direction (i+4)%8 and rebuilds the tiles that are not full "
        "paths")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.Cell(11, 10) = path_tiles::kFlagFullTile; // direction 0 (neighbour of i = 4): a full tile is not rebuilt
    path_tiles::AddNeighbourBits(env, f.tiles, 10, 10);
    // Bit i goes to the neighbour in direction (i + 4) % 8: the directions are visited in the order 4, 5, 6, 7, 0, 1, 2, 3.
    OBL_CHECK_EQ(env.trace.size(), 15u); // eight neighbour lookups and seven rebuilds
    OBL_CHECK_EQ(env.trace[0], "neighbour d4");
    OBL_CHECK_EQ(env.trace[1], "rebuild(10,11)");
    OBL_CHECK_EQ(env.Cell(10, 11), 0x00010000u); // i = 0
    OBL_CHECK_EQ(env.Cell(9, 11), 0x00020000u); // i = 1, direction 5
    OBL_CHECK_EQ(env.Cell(9, 10), 0x00040000u); // i = 2, direction 6
    OBL_CHECK_EQ(env.Cell(9, 9), 0x00080000u); // i = 3, direction 7
    OBL_CHECK_EQ(env.Cell(10, 9), 0x00100000u); // i = 4, direction 0
    OBL_CHECK_EQ(env.Cell(11, 9), 0x00200000u); // i = 5
    OBL_CHECK_EQ(env.Cell(11, 10), 0x10000000u | 0x00400000u); // i = 6: the full tile gets the bit but no rebuild
    OBL_CHECK_EQ(env.Cell(11, 11), 0x00800000u); // i = 7
    int rebuilds = 0;
    for (const std::string& line : env.trace) rebuilds += line.rfind("rebuild", 0) == 0;
    OBL_CHECK_EQ(rebuilds, 7);
}

OBL_TEST_CASE("path_tiles_rebuild.remove_neighbour_bits decides on the flags before the bit is cleared")
{
    Fixture f;
    FakeEnv env(f.tiles);
    for (uint32_t i = 0; i < 8; ++i) {
        const int dx[8] = {0, -1, -1, -1, 0, 1, 1, 1}, dy[8] = {1, 1, 0, -1, -1, -1, 0, 1};
        env.Cell(10 + dx[i], 10 + dy[i]) = 0x00010000u << i;
    }
    path_tiles::RemoveNeighbourBits(env, f.tiles, 10, 10);
    OBL_CHECK_EQ(env.Cell(10, 11), 0u);
    OBL_CHECK_EQ(env.Cell(11, 11), 0u);
    int rebuilds = 0;
    for (const std::string& line : env.trace) rebuilds += line.rfind("rebuild", 0) == 0;
    OBL_CHECK_EQ(rebuilds, 8); // the cleared bit was still in the flags the decision read
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_tile outside the map or off screen does nothing, not even the overlap")
{
    Fixture f;
    FakeEnv env(f.tiles);
    path_tiles::RebuildTile(env, f.tiles, 128, 5, 0); // x > 127
    path_tiles::RebuildTile(env, f.tiles, 5, 0, 0); // y < 1
    path_tiles::RebuildTile(env, f.tiles, 5, 128, 0); // y > 127
    OBL_CHECK(env.trace.empty());
    env.cameraX = 0;
    env.screenX = 0x1e; // 0 - 0x1f + 0x1e < 0
    path_tiles::RebuildTile(env, f.tiles, 5, 5, 0);
    OBL_CHECK_EQ(env.trace.size(), 1u); // only the world position that was needed to find out
    OBL_CHECK_EQ(env.trace[0], "world(5,5)");
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_tile negative x is accepted by the signed one-sided test")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.screenX = 1000;
    path_tiles::RebuildTile(env, f.tiles, static_cast<uint32_t>(-1), 5, 0);
    OBL_CHECK(!env.trace.empty());
    OBL_CHECK_EQ(env.trace[0], "world(-1,5)");
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_tile bare tile only restores the background and updates the overlap")
{
    Fixture f;
    FakeEnv env(f.tiles);
    path_tiles::RebuildTile(env, f.tiles, 5, 6, 0);
    OBL_REQUIRE(env.trace.size() == 3u);
    OBL_CHECK_EQ(env.trace[0], "world(5,6)");
    OBL_CHECK_EQ(env.trace[1], "restore 119,228 mask"); // 100 - 0x1f + 50, 200 - 0x20 + 60
    OBL_CHECK_EQ(env.trace[2], "overlap(5,6)");
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_tile draws grass, surface, then the lookup frame, in that order")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.Cell(5, 6) = path_tiles::kFlagGrass | (3u << path_tiles::kSurfaceShift) | 0x00010000u;
    path_tiles::RebuildTile(env, f.tiles, 5, 6, 0);
    std::vector<std::string> draws;
    for (const std::string& line : env.trace)
        if (line.rfind("draw", 0) == 0) draws.push_back(line);
    OBL_REQUIRE(draws.size() == 3u);
    OBL_CHECK_EQ(draws[0], "draw grass frame 0 at 5,6");
    OBL_CHECK_EQ(draws[1], "draw surface frame 0 at 5,6");
    OBL_CHECK_EQ(draws[2], "draw pavement frame 7 at 5,6"); // lookup[0x01]
    OBL_CHECK_EQ(f.tiles.m_lastSurface, 2);
    OBL_CHECK_EQ(env.trace.back(), "overlap(5,6)");
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_tile uses the beach edge sprite for the lookup frame when the beach flag is set")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.Cell(5, 6) = path_tiles::kFlagBeachEdge | 0x00030000u;
    path_tiles::RebuildTile(env, f.tiles, 5, 6, 1);
    bool drewEdge = false;
    for (const std::string& line : env.trace) drewEdge = drewEdge || line == "draw edge frame 9 at 5,6";
    OBL_CHECK(drewEdge);
    OBL_CHECK_EQ(env.trace[env.trace.size() - 2], "remove(5,6)");
    OBL_CHECK_EQ(env.trace.back(), "overlap(5,6)");
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_tile full tile uses frame 67 and tells the neighbours; the lookup is not consulted")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.Cell(5, 6) = path_tiles::kFlagFullTile | 0x00010000u;
    path_tiles::RebuildTile(env, f.tiles, 5, 6, 0);
    bool drew = false, added = false;
    for (const std::string& line : env.trace) {
        drew = drew || line == "draw pavement frame 67 at 5,6";
        added = added || line == "add(5,6)";
    }
    OBL_CHECK(drew);
    OBL_CHECK(added);
}

OBL_TEST_CASE("path_tiles_rebuild.full_frame_comes_from_tileset_metadata")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.fullFrame = 42;
    env.Cell(5, 6) = path_tiles::kFlagFullTile;
    path_tiles::RebuildTile(env, f.tiles, 5, 6, 0);
    OBL_CHECK_EQ(env.frames[f.tiles.m_pavement], 42);
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_tile skips a surface the object has no sprite for")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.Cell(5, 6) = 12u << path_tiles::kSurfaceShift;
    path_tiles::RebuildTile(env, f.tiles, 5, 6, 0);
    for (const std::string& line : env.trace) OBL_CHECK(line.rfind("draw", 0) != 0);
    OBL_CHECK_EQ(f.tiles.m_lastSurface, 11);
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_all walks x then y over 1..127, rebuilds set cells and reports every fourth column")
{
    Fixture f;
    FakeEnv env(f.tiles);
    env.Cell(2, 3) = 1;
    env.Cell(2, 9) = 1;
    env.Cell(127, 127) = 4;
    env.Cell(0, 5) = 1; // outside the loops
    env.Cell(5, 128) = 1; // y = 128 wraps to 0, outside too
    path_tiles::RebuildAll(env, f.tiles);
    const std::vector<std::string>& kept = env.trace;
    OBL_REQUIRE(kept.size() == 3u + 31u);
    OBL_CHECK_EQ(kept[0], "rebuild(2,3)");
    OBL_CHECK_EQ(kept[1], "rebuild(2,9)");
    OBL_CHECK_EQ(kept[2], "progress 0.515625"); // column 4: 4 / 256 + 0.5
    OBL_CHECK_EQ(kept[kept.size() - 2], "progress 0.984375"); // column 124, the last multiple of four
    OBL_CHECK_EQ(kept.back(), "rebuild(127,127)"); // column 127 comes after it and is not a multiple of four
}

OBL_TEST_CASE("path_tiles_rebuild.rebuild_rect walks the tile rectangle row by row")
{
    Fixture f;
    FakeEnv env(f.tiles);
    const int rect[4] = {0, 0, 10, 10};
    path_tiles::RebuildRect(env, f.tiles, rect);
    OBL_REQUIRE(env.trace.size() == 4u);
    OBL_CHECK_EQ(env.trace[0], "rebuild(2,3)");
    OBL_CHECK_EQ(env.trace[1], "rebuild(3,3)");
    OBL_CHECK_EQ(env.trace[2], "rebuild(2,4)");
    OBL_CHECK_EQ(env.trace[3], "rebuild(3,4)");
    env.trace.clear();
    env.tileRect[2] = 0; // an empty rectangle rebuilds nothing
    path_tiles::RebuildRect(env, f.tiles, rect);
    OBL_CHECK(env.trace.empty());
}

OBL_TEST_CASE("path_tiles_rebuild.overlap intersects the pavement frame rectangle with each scenery and bakes the non-empty overlaps")
{
    Fixture f;
    FakeEnv env(f.tiles);
    Scenery first{}, done{}, flat{};
    first.m_state = 1;
    first.m_WorldXYZ[0] = 12.0f;
    first.m_ScrXYRect = {0, 0, 0, 0};
    done.m_state = 3;
    flat.m_state = 1;
    env.list = {&first, &done, &flat};
    env.intersection = {120, 70, 150, 90};
    path_tiles::UpdateBuildingOverlap(env, f.tiles, 5, 6);
    // rect: left = 3 + 100 + 50, top = -4 + 200 + 60, right = left + 64, bottom = top + 32
    std::vector<std::string> out;
    for (const std::string& line : env.trace)
        if (line.rfind("intersect", 0) == 0 || line.rfind("bake", 0) == 0) out.push_back(line);
    OBL_REQUIRE(out.size() == 4u); // the state 3 object is skipped without an intersection
    OBL_CHECK_EQ(out[0], "intersect 153,256,217,288 with 0");
    OBL_CHECK_EQ(out[1], "bake 120,70 world 12 mask -33,-186");
    env.trace.clear();
    env.intersection = {120, 70, 120, 90}; // zero width: no bake
    path_tiles::UpdateBuildingOverlap(env, f.tiles, 5, 6);
    for (const std::string& line : env.trace) OBL_CHECK(line.rfind("bake", 0) != 0);
    env.trace.clear();
    env.intersects = false;
    path_tiles::UpdateBuildingOverlap(env, f.tiles, 5, 6);
    for (const std::string& line : env.trace) OBL_CHECK(line.rfind("bake", 0) != 0);
}
