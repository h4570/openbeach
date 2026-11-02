
#include "unit_test.h"

#include "game/map/path_tiles_lifecycle.h"

#include <string>
#include <vector>

using namespace obl;
using path::PathTiles;
using path::SpriteHandle;

namespace {

// Handle n is the n-th created sprite (1-based).
struct FakeEnv {
    std::vector<std::string> created;
    std::vector<SpriteHandle> external;
    std::vector<SpriteHandle> released;
    std::string file;
    bool hasFile = true;
    int32_t fullFrame = 0x50;
    std::string readPath;
    PathTiles* global = nullptr;

    SpriteHandle CreateSprite(const char* name)
    {
        created.push_back(name);
        return static_cast<SpriteHandle>(created.size());
    }
    void SetExternalFrame(SpriteHandle s) { external.push_back(s); }
    const char* SpriteName(SpriteHandle s) { return created.at(static_cast<size_t>(s) - 1).c_str(); }
    void ReleaseSprite(SpriteHandle s) { released.push_back(s); }
    void ReadLookup(const char* path, uint8_t (&table)[256], int32_t& full)
    {
        readPath = path;
        if (!hasFile) return;
        path_tiles::ParseLookup((file + ".").c_str(), static_cast<uint32_t>(file.size()), table);
        full = fullFrame;
    }
    void SetGlobal(PathTiles* p) { global = p; }
};

} // namespace

OBL_TEST_CASE("path_tiles.parse_lookup records, comments and duplicates")
{
    uint8_t table[256] = {};
    const std::string text = "// header\n// more\n\n"
                             "1 0 0 0 0 0 0 0 5 // north\n"
                             "1 1 0 0 0 0 0 0 7\n"
                             "1 0 0 0 0 0 0 0 9\n" // duplicate of mask 1, later wins
                             "0 0 0 0 0 0 0 0 -1\n"; // stored as the low byte, 255
    path_tiles::ParseLookup((text + ".").c_str(), static_cast<uint32_t>(text.size()), table);
    OBL_CHECK_EQ(table[1], 9);
    OBL_CHECK_EQ(table[3], 7);
    OBL_CHECK_EQ(table[0], 255);
    OBL_CHECK_EQ(table[2], 0);
}

OBL_TEST_CASE("path_tiles.parse_lookup without a header and with semicolon tokens")
{
    uint8_t table[256] = {};
    const std::string text = "0 1 0 0 0 0 0 0 12\r\n0 0 0 0 0 0 0 1 3\r\n";
    path_tiles::ParseLookup((text + ".").c_str(), static_cast<uint32_t>(text.size()), table);
    OBL_CHECK_EQ(table[2], 12);
    OBL_CHECK_EQ(table[128], 3);
}

OBL_TEST_CASE("path_tiles.parse_lookup drops out-of-range masks")
{
    uint8_t table[256] = {};
    const std::string text = "2 0 0 0 0 0 0 2 1\n1 0 0 0 0 0 0 0 4\n";
    path_tiles::ParseLookup((text + ".").c_str(), static_cast<uint32_t>(text.size()), table);
    OBL_CHECK_EQ(table[1], 4);
    OBL_CHECK_EQ(table[2], 0);
}

OBL_TEST_CASE("path_tiles.construct order, defaults and fields")
{
    FakeEnv env;
    env.file = "0 0 0 0 0 0 0 1 8\n";
    PathTiles m{};
    m.m_lastSurface = 77;
    path_tiles::Construct(m, env, nullptr, nullptr);
    OBL_CHECK(env.global == &m);
    OBL_CHECK(env.readPath == "PathTile_10");
    const std::vector<std::string> expected = {"PathTile_10", "Grass08", "PathTile_10b", "DarkSquares", "PaleSquares", "Tarmac",
            "PaleHerring", "DarkHerring", "BlueHerring", "Grass08", "QuadSand", "QuadBase"};
    OBL_CHECK(env.created == expected);
    const std::vector<SpriteHandle> external = {m.m_pavement, m.m_grass, m.m_beachEdge};
    OBL_CHECK(env.external == external);
    OBL_CHECK_EQ(m.m_fullTileFrame, 0x50);
    OBL_CHECK_EQ(m.m_surfaceCount, 9);
    OBL_CHECK_EQ(m.m_grassSurfaceNumber, 7);
    OBL_CHECK_EQ(m.m_lastSurface, 0);
    OBL_CHECK_EQ(m.m_lookup[128], 8);
    OBL_CHECK_EQ(m.m_lookup[1], 0);
    OBL_CHECK(m.m_surfaces[6] != m.m_grass); // a separate instance of the same sprite
}

OBL_TEST_CASE("path_tiles.construct with explicit tilesets and a missing file")
{
    FakeEnv env;
    env.hasFile = false;
    PathTiles m{};
    path_tiles::Construct(m, env, "PathTile_20", "PathTile_20b");
    OBL_CHECK(env.created[0] == "PathTile_20");
    OBL_CHECK(env.created[2] == "PathTile_20b");
    OBL_CHECK_EQ(m.m_lookup[5], 0);
    OBL_CHECK_EQ(m.m_fullTileFrame, 0x43); // no metadata: the default
    OBL_CHECK_EQ(path_tiles::TilesetNumber(m), 20);
}

OBL_TEST_CASE("path_tiles.destruct releases header sprites then surfaces and clears the global")
{
    FakeEnv env;
    PathTiles m{};
    path_tiles::Construct(m, env, nullptr, nullptr);
    env.released.clear();
    path_tiles::Destruct(m, env);
    OBL_CHECK(env.global == nullptr);
    OBL_CHECK_EQ(env.released.size(), size_t(12));
    OBL_CHECK(env.released[0] == m.m_pavement);
    OBL_CHECK(env.released[1] == m.m_grass);
    OBL_CHECK(env.released[2] == m.m_beachEdge);
    for (int i = 0; i < 9; ++i) OBL_CHECK(env.released[3 + i] == m.m_surfaces[i]);
}

OBL_TEST_CASE("path_tiles.queries")
{
    FakeEnv env;
    PathTiles m{};
    path_tiles::Construct(m, env, nullptr, nullptr);
    OBL_CHECK_EQ(path_tiles::TilesetNumber(m), 10);
    OBL_CHECK(std::string(path_tiles::GrassSpriteName(m)) == "Grass08");
    OBL_CHECK(std::string(path_tiles::PavementSpriteName(m)) == "PathTile_10");
    OBL_CHECK_EQ(path_tiles::FullTileFrame(m), 0x50);
    m.m_pavementName = "PathTile_"; // shorter than the two digits: atoi of nothing
    OBL_CHECK_EQ(path_tiles::TilesetNumber(m), 0);
}
