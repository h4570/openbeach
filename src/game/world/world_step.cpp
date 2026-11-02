#include "game/ui/renderer_api.h"
#include "game/ui/gui_trivial_slots.h"
#include "game/game/game_api.h"
#include "game/ui/input.h"
#include "game/world/world_script.h"
#include "game/world/world_step.h"
#include "game/events/event_manager.h"
#include "game/weather/weather.h"
#include "game/feedback/feedback_manager.h"

#include "game/game/game_accessors.h"
#include "game/world/world_control.h"
#include "game/world/world_objects.h"
#include "game/world/world_update.h"
#include "game/world/world_view.h"
#include "game/ui/renderer_types.h"
#include "game/world/world_state.h"
#include "core/call_counter.h"
#include "game/world/world_update_logic.h"
#include "game/messages/message_handler.h"
#include "game/objects/commodity_providers.h"

#include <cstdint>
#include "game/ui/map_view.h"

namespace obl::world {
namespace {

using layout::World;

// Frames run (`call-report world.update`).
test::CallCounter g_updateCalls("world.update");

// ---- the game side of the Env contract in src/game/world/world_update_logic.h ---------------------------------------------------------

class GameEnv {
public:
    explicit GameEnv(World& world) : m_world(&world) {}

    void ApplyZoomRequest() { world::ApplyZoomRequest(*m_world); }
    void ResetViewport() { m_world->m_mapView->ResetViewport(); }
    float FrameDelta() { return game::GetGameClock()->m_deltaTime; }
    int DayLengthSeconds()
    {
        int32_t seconds = 0; // left unset when the key is missing; the key always exists
        game::game_parameters::GetInt(game::game_parameters::Current(), &seconds, "iDAY_LENGTH_IN_SECONDS");
        return seconds;
    }
    void UpdateBuildingTotals() { world::UpdateBuildingTotals(*m_world); }
    void UpdateWeather() { weather::runtime::UpdateSchedule(m_world->m_weather); }
    void UpdateEvents() { disaster::event_facade::Update(m_world->m_eventManager); }
    void RebuildProviders() { object::commodity_providers::Rebuild(m_world->m_commodityProviders); }
    void UpdateObjects() { world::UpdateObjects(*m_world); }
    void CheckMissingStaffFeedback() { world::CheckMissingStaffFeedback(*m_world); }
    void UpdateFeedback() { feedback::Feedback(m_world->m_feedbackManager).Update(); }
    void UpdateDayCycle() { world::UpdateDayCycle(*m_world); }
    void UpdateMessages() { messages::message_facade::Update(m_world->m_messageHandler); }
    void ProcessDeleted() { world::ProcessDeletedObjects(*m_world); }
    // The "id" argument is 0: the no-op never reads it.
    void CallGridFunction() { gui::EmptyCallFunction(m_world->m_grid.get(), 0); }
    void UpdateCameraInput() { world::UpdateCameraInput(*m_world); }
    void UpdateHotkeys() { world::UpdateHotkeys(*m_world); }
    bool KeyHeld(int code) { return ui::InputKeyHeld(*game::GetContext().view.m_input, code); }
    bool KeyPressed(int code) { return ui::InputKeyPressed(*game::GetContext().view.m_input, code); }
    void UpdateMapCamera() { m_world->m_mapView->UpdateCamera(); }
    void RunScriptUpdateList() { WorldScriptRunOnUpdate(*m_world->m_worldScript); }
    void SetFullViewport()
    {
        const ui::Renderer* renderer = game::GetRenderer();
        const int rect[4] = {0, 0, renderer->m_width, renderer->m_height};
        obl::ui::renderer::SetViewport(const_cast<ui::Renderer*>(renderer), rect);
    }

private:
    World* m_world;
};

void UpdateEntry(World* world)
{
    g_updateCalls.Count();
    GameEnv env(*world);
    world_update::Update(*world, game::GetContext().view, env);
}

} // namespace

void Update(World& world)
{
    UpdateEntry(&world);
}

} // namespace obl::world
