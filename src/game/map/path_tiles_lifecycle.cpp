#include "game/map/path_tiles_lifecycle.h"

#include "core/ascii_case.h"
#include "core/call_counter.h"
#include "core/log.h"
#include "core/os.h"
#include "core/pack_path.h"
#include "game/game/game_accessors.h"
#include "game/map/path_queries.h"
#include "game/map/path_tiles_state.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_bank.h"
#include "formats/sprite_pack.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace obl::path {
namespace {

using ui::SpriteInstance;

// Path tiles made and destroyed by the world (`call-report map.path-tiles-create` / `map.path-tiles-destroy`).
test::CallCounter g_createCalls("map.path-tiles-create");
test::CallCounter g_destroyCalls("map.path-tiles-destroy");

void* Bank()
{
    return ui::GetSpriteBank();
}

// SpriteInstance::m_animType: the renderer draws the frame the tiles set.
constexpr int32_t kSpriteExternalFrame = 3;

// The game's sprite bank, converted tileset metadata and the path tiles pointer in game::Context.
struct GameEnv {
    SpriteHandle CreateSprite(const char* name) { return HandleOf((static_cast<ui::SpriteBank*>(Bank()))->CreateObservedInstance(name)); }
    void SetExternalFrame(SpriteHandle sprite) { SpriteOf(sprite)->m_animType = kSpriteExternalFrame; }
    const char* SpriteName(SpriteHandle sprite) { return SpriteOf(sprite)->m_name; }
    void ReleaseSprite(SpriteHandle sprite) { (static_cast<ui::SpriteBank*>(Bank()))->ReleaseInstance(SpriteOf(sprite)); }
    void ReadLookup(const char* name, uint8_t (&table)[256], int32_t& fullFrame)
    {
        std::string key = name;
        for (char& c : key) c = ascii::Lower(c);
        sprite_pack::Sprite sprite;
        std::string error;
        if (!sprite_pack::ReadSprite(os::ResolvePath((pack_path::Selected() / "sprites").generic_string()), key, false, sprite, error,
                    sprite_pack::ReadMode::Metadata) ||
                !sprite.autotile) {
            OBL_LOG("FatalError: path tiles %s: %s", name, error.empty() ? "missing autotile table" : error.c_str());
            std::_Exit(1);
        }
        std::memcpy(table, sprite.autotile->masks.data(), sizeof(table));
        fullFrame = sprite.autotile->full;
    }
    void SetGlobal(PathTiles* tiles) { game::SetPathTiles(tiles); }
};

} // namespace

PathTiles* CreatePathTiles(const char* pavement, const char* beachEdge)
{
    auto* tiles = new PathTiles;
    GameEnv env;
    path_tiles::Construct(*tiles, env, pavement, beachEdge);
    g_createCalls.Count();
    return tiles;
}

void DestroyPathTiles(PathTiles* tiles)
{
    GameEnv env;
    path_tiles::Destruct(*tiles, env);
    delete tiles;
    g_destroyCalls.Count();
}

int PathTiles::GetTilesetNumber() const
{
    return path_tiles::TilesetNumber(*this);
}

const char* PathTiles::GetGrassSpriteName() const
{
    return path_tiles::GrassSpriteName(*this);
}

const char* PathTiles::GetPavementSpriteName() const
{
    return path_tiles::PavementSpriteName(*this);
}

int PathTiles::GetFullTileFrame() const
{
    return path_tiles::FullTileFrame(*this);
}

} // namespace obl::path
