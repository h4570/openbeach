#include "game/ui/gui_manager_dialogs.h"
#include "gui_manager_buttons.h"
#include "gui_controls.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/ui/gui_controls_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/tables/world_params_tables.h"
#include "game/game/game_accessors.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/world/world_params.h"

namespace obl::gui {
namespace {
using namespace obl::gui_manager_buttons;
// Call counters (`call-report ui.gui-manager-buttons`).
test::CallCounter g_counter("ui.gui-manager-buttons");

struct GameEnv {
    void DisableIfAction(Widget* button, const char* action)
    {
        button_action_facade::DisableIfAction(static_cast<ui::GuiButton*>(button), action);
    }
    void ResetState(Widget* button, const char* action)
    {
        button_action_facade::ResetStateByAction(static_cast<ui::GuiButton*>(button), action);
    }
    void SetBlink(Widget* button, const char* action, bool blink)
    {
        button_action_facade::SetBlinkIfAction(static_cast<ui::GuiButton*>(button), action, blink);
    }
    void SetGraphic(Widget* button, const char* action, const char* sprite)
    {
        button_action_facade::SetGraphicByAction(static_cast<ui::GuiButton*>(button), action, sprite);
    }
    void SetFlashing(Widget* button, const char* action, bool flash)
    {
        button_action_facade::SetFlashingByAction(static_cast<ui::GuiButton*>(button), action, flash);
    }
    void SetVisible(Widget* button, const char* action, bool visible)
    {
        button_action_facade::SetVisibleIfAction(static_cast<ui::GuiButton*>(button), action, visible);
    }
    bool BuildingInScenario(const char* key)
    {
        int id = world::world_params::GetBuildingId(game::GetWorldParams(), key);
        return game::GetWorld()->BuildingInScenario(id);
    }
    bool WorldLoaded() { return game::GetWorld() != nullptr; }
    int CountBuildings(BuildingBehavior behavior)
    {
        return static_cast<int>(obl::object_manager::CountBuildingsByBehavior(*game::GetObjectManager(), behavior));
    }
    uint32_t ScreenWidth() { return static_cast<uint32_t>(game::GetRenderer()->m_width); }
    bool BuildingHasFootprint(int id)
    {
        auto* def = world::world_params::GetBuildingDef(game::GetWorldParams(), id);
        return def->m_footprint != nullptr;
    }
    void ShowDialog(Manager* manager, const char* name) { manager->ShowDialog(name, nullptr, false); }
    void ClearStaffSelection() { object::object_manager::ClearStaffSelection(game::GetObjectManager()); }
    object::Base* const* List(char objectClass, uint32_t& count)
    {
        m_list = object::object_manager::GetListByClass(game::GetObjectManager(), static_cast<uint8_t>(objectClass));
        count = static_cast<uint32_t>(m_list.size());
        return m_list.data();
    }
    void Select(object::Base* object) { object->OnSelect(0, 1); }
    void Post(messages::Message& message) { messages::message_facade::Post(messages::message_facade::Current(), &message); }

private:
    object::object_manager::ObjectList m_list; // the last List() result, read through the pointer it returned
};

} // namespace

} // namespace obl::gui

namespace obl::ui {
void GuiManager::DisableActionButtons(const char* action)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_buttons::DisableActionButtons(env, this, action);
}
void GuiManager::ResetActionButtons(const char* action)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_buttons::ResetActionButtons(env, this, action);
}
void GuiManager::FlashActionButtons(const char* action, bool flash)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_buttons::FlashButtonsByAction(env, this, action, flash);
}
void GuiManager::UpdateStaffButtons(uint8_t staffType, bool hasStaff)
{
    // Outside 7..12 the buttons are left alone.
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_buttons::UpdateStaffButtons(env, this, staffType, hasStaff);
}
void GuiManager::UpdateFerryButtonGraphic()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_buttons::UpdateFerryButtonGraphic(env, this);
}
void GuiManager::SetActionButtonGraphic(const char* action, const char* sprite)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_buttons::SetButtonGraphic(env, this, action, sprite);
}
void GuiManager::BlinkActionButtons(const char* action, bool blink)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_buttons::SetActionButtonsBlink(env, this, action, blink);
}
uint32_t GuiManager::SelectStaff(int objectClass, uint32_t* index)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui_manager_buttons::SelectStaffByClass(env, static_cast<uint8_t>(objectClass), index);
}
} // namespace obl::ui
