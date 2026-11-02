#include "game/ui/sprite_bank.h"
#include "game/objects/object_static.h"
#include "game/objects/scenery_lifecycle.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/gui_definitions.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/scenery_construction.h"
#include "core/call_counter.h"

#include <cstddef>
#include <cstdint>
#include "game/world/world_params.h"
#include "game/objects/scenery_draw.h"

// Scenery's constructor and destructor (docs/architecture/objects.md). The logic is src/game/objects/scenery_construction.h; this is its game Env.
namespace obl::object {
using game::GetWorldParams;
using game::Rand;

namespace {

using object::Scenery;
using ui::SpriteInstance;

test::CallCounter g_construct("objects.scenery-construct");
test::CallCounter g_destroy("objects.scenery-destroy");

class GameEnv {
public:
    const tables::SceneryDef* Definition(const char* key) const
    {
        tables::WorldParams* params = GetWorldParams();
        const int id = world::world_params::GetSceneryId(params, key);
        return world::world_params::GetSceneryDef(params, id);
    }
    void SetGridSize(Scenery& s, uint8_t w, uint8_t h) const
    {
        s.SetGridWidth(w);
        s.SetGridHeight(h);
    }
    SpriteInstance* CreateSprite(const char* name) const
    {
        return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name);
    }
    void SetAnim(SpriteInstance* sprite, const char* name) const
    {
        // Nobody needs the float result; it is discarded.
        (sprite)->SetAnimByName(name, 0.0f, nullptr);
    }
    int Rand() const { return game::Rand(); }
    Scenery::DrawFunc LightFunc() const { return &SceneryDrawLight; }
    Scenery::DrawFunc NoOpFunc() const { return &SceneryDrawNothing; }
    const char* NoMenuText() const { return gui_data::kButtonNoText; }
    void ReleaseSprite(SpriteInstance* sprite) const { static_cast<ui::SpriteBank*>(ui::GetSpriteBank())->ReleaseInstance(sprite); }
};

} // namespace

Scenery::Scenery(const char* key) : Prop(key)
{
    g_construct.Count();
    GameEnv env;
    scenery_construction::Construct(*this, key, env);
}

Scenery::~Scenery()
{
    g_destroy.Count();
    GameEnv env;
    scenery_construction::Destruct(*this, env);
}
} // namespace obl::object
