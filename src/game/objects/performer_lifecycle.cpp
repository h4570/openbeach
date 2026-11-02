#include "game/ui/sprite_bank.h"
#include "game/objects/object_moving.h"
#include "game/objects/performer_lifecycle.h"
#include "game/ui/sprite.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_performers.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/call_counter.h"

#include <cstring>
#include <vector>

// The attached-sprite fixture of the weightlifters (the performer classes have no behavior of their own here).
namespace obl::object {
using game::GetObjectManager;

namespace {
using namespace layout;
using ui::SpriteInstance;
using namespace object;
// ---- fixture -----------------------------------------------------------------------------------------------------------------------

std::vector<object::Base*> ObjectsOfClass(int objectClass)
{
    object::Manager* manager = GetObjectManager();
    if (!manager) return {};
    return object_manager::GetListByClass(manager, objectClass);
}

// A weightlifter keeps its weights only while the script holds them: give each one a sprite on odd counts so a real world save has both flags.
test::CallCounter g_fixture("PerformerLifecycleAttachedFixture");
void DriveAttachedFixture(int count)
{
    constexpr int kWeightLifterClass = 16; // ObjectType value
    for (object::Base* member : ObjectsOfClass(kWeightLifterClass)) {
        auto* lifter = static_cast<WeightLifter*>(member);
        if (lifter->m_attachedSprite) (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(lifter->m_attachedSprite);
        lifter->m_attachedSprite = nullptr;
        if (count % 2) {
            auto* sprite = static_cast<SpriteInstance*>(lifter->m_sprite);
            lifter->m_attachedSprite = (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(sprite->m_name);
        }
        g_fixture.Count();
    }
}
} // namespace

bool RegisterPerformerTestDrivers()
{
    g_fixture.SetDriver(&DriveAttachedFixture);
    return true;
}
} // namespace obl::object

namespace obl::object {} // namespace obl::object
