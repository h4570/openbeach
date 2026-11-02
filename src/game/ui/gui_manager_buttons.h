#pragma once
#include "game/tables/building_behavior.h"
#include "game/ui/gui_manager_types.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include <cstdint>

namespace obl::gui_manager_buttons {
using Manager = ui::GuiManager;
using Widget = ui::GuiWidget;

inline ui::GuiManager& View(Manager* manager)
{
    return *manager;
}

inline constexpr int kButtonWidgetType = 0;
inline constexpr int kCancelBuildMode = 5;
inline constexpr int kStaffSelectedMessage = 0x65;

// Every action-button function walks the whole widget list once per call and acts on the buttons only.
template<class Callback> void ForEachButton(Manager* manager, Callback&& callback)
{
    for (auto* node = View(manager).m_widgets; node; node = node->m_next)
        if (node->m_widget->m_type == kButtonWidgetType) callback(node->m_widget);
}

template<class Env> void DisableActionButtons(Env& env, Manager* manager, const char* action)
{
    ForEachButton(manager, [&](Widget* button) { env.DisableIfAction(button, action); });
}
template<class Env> void ResetActionButtons(Env& env, Manager* manager, const char* action)
{
    ForEachButton(manager, [&](Widget* button) { env.ResetState(button, action); });
}
template<class Env> void SetActionButtonsBlink(Env& env, Manager* manager, const char* action, bool blink)
{
    ForEachButton(manager, [&](Widget* button) { env.SetBlink(button, action, blink); });
}
template<class Env> void SetButtonGraphic(Env& env, Manager* manager, const char* action, const char* sprite)
{
    ForEachButton(manager, [&](Widget* button) { env.SetGraphic(button, action, sprite); });
}
template<class Env> void FlashButtonsByAction(Env& env, Manager* manager, const char* action, bool flash)
{
    ForEachButton(manager, [&](Widget* button) { env.SetFlashing(button, action, flash); });
}

// The three names each staff type (7..12) answers to: the "browser" ghost button shown while nobody is hired, the "icon" button that cycles through
// the hired staff, and the building the type is hired from.
struct StaffButtonNames {
    const char* browser;
    const char* icon;
    const char* building;
};

inline const StaffButtonNames* StaffNames(uint8_t staffType)
{
    static constexpr StaffButtonNames kNames[] = {
            {"LifeguardBrowser", "LifeguardIcon", "BAYWATCH"},
            {"MechanicBrowser", "MechanicIcon", "HANDYMAN"},
            {"RepBrowser", "RepIcon", "REP"},
            {"BuilderBrowser", "BuilderIcon", "BUILDERYARD"},
            {"CleanerBrowser", "CleanerIcon", "CLEANERSHACK"},
            {"SecurityBrowser", "SecurityIcon", "SECURITY_POST"},
    };
    return staffType >= 7 && staffType <= 12 ? &kNames[staffType - 7] : nullptr;
}

template<class Env> void UpdateStaffButtons(Env& env, Manager* manager, uint8_t staffType, bool hasStaff)
{
    const StaffButtonNames* names = StaffNames(staffType);
    if (!names) return;
    auto visible = [&](const char* action, bool show) {
        ForEachButton(manager, [&](Widget* button) { env.SetVisible(button, action, show); });
    };
    // Hired staff shows the icon, nobody hired the browser button.
    visible(hasStaff ? names->icon : names->browser, true);
    visible(hasStaff ? names->browser : names->icon, false);
    if (!env.BuildingInScenario(names->building)) {
        DisableActionButtons(env, manager, names->icon);
        DisableActionButtons(env, manager, names->browser);
    }
}

// The ferry panel button shows a coach while no WHARF exists and a ship once it does; the 800-pixel threshold picks the UI2 or UI3 sprite set.
template<class Env> void UpdateFerryButtonGraphic(Env& env, Manager* manager)
{
    if (!env.WorldLoaded()) return;
    const bool ship = env.CountBuildings(BuildingBehavior::ShipStop) != 0;
    const bool narrow = env.ScreenWidth() < 800;
    const char* sprite =
            ship ? (narrow ? "UI2_bar_blob_ship" : "UI3_bar_blob_ship") : (narrow ? "UI2_bar_blob_coach" : "UI3_bar_blob_coach");
    SetButtonGraphic(env, manager, "FerryPanel", sprite);
}

template<class Env> void ShowCancelBuildMode(Env& env, Manager* manager)
{
    if (env.BuildingHasFootprint(View(manager).m_context[0])) View(manager).m_requestedMode = kCancelBuildMode;
    env.ShowDialog(manager, "CancelBuildMode");
}

inline void ToggleSelection(Manager* manager, int objectId)
{
    int32_t& selection = View(manager).m_selection;
    selection = selection == objectId ? -1 : objectId;
}

// SelectStaffByClass: the staff icon cycles through the staff of its class. A staff member that needs attention wins (the last one in the list);
// without one the rotating `index` moves to the next member and wraps to 0 at the end.
inline object::Base* PickStaff(object::Base* const* list, uint32_t count, uint32_t& index)
{
    bool attention = false;
    for (uint32_t i = 0; i < count; ++i)
        if (list[i]->m_requiresAttention) {
            attention = true;
            index = i;
        }
    if (!attention && ++index >= count) index = 0;
    return list[index];
}

template<class Env> uint32_t SelectStaffByClass(Env& env, uint8_t objectClass, uint32_t* index)
{
    env.ClearStaffSelection();
    uint32_t count = 0;
    object::Base* const* list = env.List(static_cast<char>(objectClass), count);
    if (count == 0) return 0;
    object::Base* staff = PickStaff(list, count, *index);
    env.Select(staff);

    messages::Message message{};
    message.sender = message.receiver = static_cast<int32_t>(staff->m_id);
    message.type = kStaffSelectedMessage;
    env.Post(message);
    return staff->m_id;
}
} // namespace obl::gui_manager_buttons
