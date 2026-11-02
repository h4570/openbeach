#include "game/world/world_objects.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/objects/object_manager_state.h"
#include "game/feedback/feedback_manager.h"
#include "game/ferry/ferry.h"

#include "game/game/game_accessors.h"
#include "core/call_counter.h"
#include "game/objects/object_building.h"
#include "game/world/world_objects_logic.h"
#include "game/objects/building_methods.h"

#include <cstdint>

namespace obl::world {
namespace {

using object::Building;
using object::ObjectType;
using world_data::World;

// Call counters (`call-report <name>`); `.deleted` counts the objects world::ProcessDeletedObjects removed.
test::CallCounter g_updateCalls("world.update-objects");
test::CallCounter g_processCalls("world.process-deleted-objects");
test::CallCounter g_deletedCalls("world.process-deleted-objects.deleted");
test::CallCounter g_deletingCalls("world.on-object-deleting");

// ---- the game side of the Env contracts in src/game/world/world_objects_logic.h --------------------------------------------------------

void NotifyObjectDeletingEntry(World* world, object::Base* object);

class GameEnv {
public:
    explicit GameEnv(World& world) : m_world(world) {}

    void Update(object::Base& object) { object.Update(); }
    void OnObjectDeleting(object::Base& object) { NotifyObjectDeletingEntry(&m_world, &object); }
    void OnExitState(object::Base& object) { object.OnExitState(); }
    void DeleteObject(uint32_t id) { m_world.m_objectManager->DeleteObject(id); }
    void RefreshDialogs() { game::GetGuiManager()->RefreshAllDialogs(); }

    void CloseDialog(object::Base& object) { game::GetGuiManager()->CloseDialogByProvider(&object); }
    void RecordGuestLeaving(object::Base& guest)
    {
        ferry::ferry_facade::RecordGuestLeaving(m_world.m_ferryManager, static_cast<object::Guest*>(&guest));
    }
    void RemoveThoughts(object::Base& building, uint32_t objectId)
    {
        object::BuildingRemoveThoughtsOf(static_cast<Building*>(&building), static_cast<int32_t>(objectId));
    }
    void InvalidateFeedback(uint32_t objectId)
    {
        feedback::Feedback(m_world.m_feedbackManager).InvalidateObject(static_cast<int32_t>(objectId));
    }

private:
    World& m_world;
};

// ---- entry points ---------------------------------------------------------------------------------------------------------------------

void UpdateObjectsEntry(World* world)
{
    g_updateCalls.Count();
    GameEnv env(*world);
    world_objects::UpdateObjects(*world, env);
}

void NotifyObjectDeletingEntry(World* world, object::Base* object)
{
    g_deletingCalls.Count();
    GameEnv env(*world);
    world_objects::OnObjectDeleting(*world, *object, env);
}

// ---- world::ProcessDeletedObjects -------------------------------------------------------------------------------------------------

class ProcessEnv : public GameEnv {
public:
    using GameEnv::GameEnv;
    void OnObjectDeleting(object::Base& object)
    {
        g_deletedCalls.Count();
        GameEnv::OnObjectDeleting(object);
    }
};

void ProcessDeletedObjectsEntry(World* world)
{
    g_processCalls.Count();
    ProcessEnv env(*world);
    world_objects::ProcessDeletedObjects(*world, env);
}

} // namespace

void UpdateObjects(World& world)
{
    UpdateObjectsEntry(&world);
}
void ProcessDeletedObjects(World& world)
{
    ProcessDeletedObjectsEntry(&world);
}

} // namespace obl::world
