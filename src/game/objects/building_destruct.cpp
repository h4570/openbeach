#include "game/ui/sprite_bank.h"
#include "game/objects/object_static.h"
#include "game/objects/building_events_state.h"
#include "game/objects/building_queue.h"
#include "game/objects/building_events.h"
#include "game/ui/scripted_sound_runtime.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_teardown.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <cstdint>

// The building's destructor (docs/architecture/objects.md): the logic is src/game/objects/building_teardown.h, this is its game Env.
namespace obl::object {

namespace {

using object::Building;

test::CallCounter g_destroy("objects.building-destroy");

class GameEnv {
public:
    void ReleaseSprite(void* sprite) const { (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(sprite); }
    void DeleteQueue(void* queue) const { BuildingQueueDelete(queue); }
    void DeleteEvents(object::BuildingEvents* events) const { object::DestroyBuildingEvents(events); }
    // Both are free functions of the sound module; neither needs the building.
    void StopSound(uint32_t id) const { audio::ScriptedSound::StopBuildingSound(static_cast<int>(id)); }
};

} // namespace

Building::~Building()
{
    g_destroy.Count();
    GameEnv env;
    building_teardown::Destruct(*this, env);
}
} // namespace obl::object
