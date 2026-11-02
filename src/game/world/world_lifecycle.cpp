#include "game/ui/scripted_sound_runtime.h"
#include "game/map/grid_state.h"
#include "game/world/world_script.h"
#include "game/weather/weather.h"
#include "game/player/player_state.h"
#include "game/world/world_params.h"
#include "game/world/world_lifecycle.h"
#include "game/game/save_load.h"
#include "game/objects/commodity_providers.h"
#include "game/events/event_manager.h"
#include "game/objects/object_manager_state.h"
#include "game/construction/construction_manager.h"
#include "game/messages/message_handler.h"
#include "game/feedback/feedback_manager.h"
#include "game/ui/gui_tooltip.h"
#include "game/ferry/ferry.h"
#include "game/repair/repair_manager.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/map/grid_lifecycle.h"
#include "game/map/path_queries.h"

#include "game/game/game_context.h"
#include "game/map/path_state.h"
#include "game/map/path_tiles_state.h"
#include "game/world/world_script_host_game.h"
#include "game/world/world_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/log.h"
#include "core/watchdog.h"
#include "core/call_counter.h"
#include "game/world/world_save.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "game/ui/map_view.h"

namespace obl::world {
namespace {

using world_data::World;
namespace save = world_save;

// Call counters (`call-report <name>`).
test::CallCounter g_loadCalls("world.load"), g_destructCalls("world.destruct");

// LoadWorld's subsystems from the held obl.save document (obl_load), in the order of the file.
void LoadSubsystems(World& world)
{
    game::obl_load::ReadMap(*world.m_grid);
    world.m_pathTiles->RebuildAll();
    // The path section held only a header that nothing used, so the document has no section for it.
    world.m_objectManager->LoadSaved();
    game::obl_load::ReadPlayer(*world.m_player);
    for (const char* kind : {"FerryManager", "EventManager", "RepairManager", "FeedbackManager", "MessageHandler"})
        game::obl_load::ReadManager(kind, world);
    game::obl_load::ReadView(world.m_mapView);
    for (const char* kind : {"CleaningManager", "ConstructionManager", "Weather"}) game::obl_load::ReadManager(kind, world);
    gui::LoadingBarSetProgress(1.0f);
}

// world::World's subsystem teardown (world_state.h): each module's own Destroy, or the native destructor and delete for the game-heap ones.
void DestroySubsystem(save::Subsystem which, void* pointer)
{
    if (which == save::Subsystem::ScriptedSound) {
        static_cast<audio::ScriptedSound*>(pointer)->Destruct();
        ::operator delete(pointer);
        return;
    }
    if (which == save::Subsystem::Weather) {
        weather::runtime::Destroy(static_cast<weather::Weather*>(pointer));
        return;
    }
    if (which == save::Subsystem::Player) { // native, not on the game heap
        delete static_cast<player::Player*>(pointer);
        return;
    }
    if (which == save::Subsystem::WorldScriptHost) { // native, not on the game heap
        world_script_host::WorldScriptHost::Release(static_cast<world_script_host::WorldScriptHost*>(pointer));
        return;
    }
    if (which == save::Subsystem::FeedbackManager) { // native, not on the game heap
        feedback::Feedback::Destroy(static_cast<feedback::Manager*>(pointer));
        return;
    }
    if (which == save::Subsystem::RepairManager) { // native, not on the game heap
        repair::repair_facade::Destroy(static_cast<repair::Manager*>(pointer));
        return;
    }
    if (which == save::Subsystem::CleaningManager) { // native, not on the game heap
        cleaning::cleaning_facade::Destroy(static_cast<cleaning::Manager*>(pointer));
        return;
    }
    if (which == save::Subsystem::EventManager) { // native, not on the game heap
        disaster::event_facade::Destroy(static_cast<disaster::Manager*>(pointer));
        return;
    }
    if (which == save::Subsystem::MessageHandler) { // native, not on the game heap
        messages::message_facade::Destroy(static_cast<messages::Handler*>(pointer));
        return;
    }
    if (which == save::Subsystem::ObjectManager) { // native, not on the game heap; destroys its objects
        object::DestroyManager(static_cast<object::Manager*>(pointer));
        return;
    }
    if (which == save::Subsystem::ConstructionManager) { // native, not on the game heap
        construction::runtime::Destroy(static_cast<construction::Manager*>(pointer));
        return;
    }
    if (which == save::Subsystem::WorldParams) { // native, not on the game heap
        world_params::Destroy(static_cast<tables::WorldParams*>(pointer));
        return;
    }
    if (which == save::Subsystem::PathTiles) { // native, not on the game heap
        path::DestroyPathTiles(static_cast<path::PathTiles*>(pointer));
        return;
    }
    if (which == save::Subsystem::Path) { // native, not on the game heap
        path::DestroyPath(static_cast<path::Path*>(pointer));
        return;
    }
    if (which == save::Subsystem::Grid) { // native, not on the game heap
        map::DestroyGrid(static_cast<map::Grid*>(pointer));
        return;
    }
    if (which == save::Subsystem::CommodityProviders) { // native, not on the game heap
        object::commodity_providers::Destroy(static_cast<commodity::Providers*>(pointer));
        return;
    }
    if (which == save::Subsystem::FerryManager) { // native, not on the game heap
        ferry::ferry_facade::Destroy(static_cast<ferry::Manager*>(pointer));
        return;
    }
    if (which == save::Subsystem::MapView) { // native destructor, then the record (std::deque member)
        auto* const view = static_cast<ui::MapView*>(pointer);
        view->Destruct();
        delete view;
        return;
    }
    // Every subsystem has its own teardown above.
}

[[maybe_unused]] const bool g_teardownInstalled = (obl::world::SetSubsystemTeardown(&DestroySubsystem), true);

} // namespace

void LoadFromSave(World& world)
{
    // A synchronous world reload can legitimately take over 20 s under parallel test load.
    WatchdogLongCommand loading(true);
    g_loadCalls.Count();
    WorldScriptSetState(*world.m_worldScript, game::obl_load::ReadWorld(world));
    LoadSubsystems(world);
}
void Destroy(World* world)
{
    g_destructCalls.Count();
    delete world;
    game::GetContext().world = nullptr; // last step, also when another world was current
}

} // namespace obl::world
