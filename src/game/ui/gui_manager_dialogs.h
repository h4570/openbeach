#pragma once
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/gui_lists_types.h"
#include <cstring>
#include "core/char16.h"
#include <cstdint>

namespace obl::gui_manager_dialogs {
using Manager = ui::GuiManager;
using Node = ui::GuiManagerDialogNode;
inline ui::GuiManager& View(Manager* manager)
{
    return *manager;
}
inline Node* Find(Manager* manager, const char* name)
{
    Node* head = View(manager).m_dialogs;
    for (Node* node = head->m_next; node != head; node = node->m_next)
        if (std::strcmp(node->m_entry->m_dialog->m_name, name) == 0) return node;
    return head;
}
inline bool IsDialogOpen(Manager* manager, const char* name)
{
    return Find(manager, name) != View(manager).m_dialogs;
}
inline int GetDialogMinimized(Manager* manager, const char* name)
{
    Node* node = Find(manager, name);
    return node == View(manager).m_dialogs ? 0 : node->m_entry->m_dialog->m_minimized;
}
inline void SetDialogMinimized(Manager* manager, const char* name, int value)
{
    Node* node = Find(manager, name);
    if (node != View(manager).m_dialogs) node->m_entry->m_dialog->m_minimized = value;
}
template<class Env> void InvalidateDialog(Env& env, Manager* manager, const char* name, uint8_t clear)
{
    Node* node = Find(manager, name);
    if (node == View(manager).m_dialogs || node->m_entry->m_state != 1) return;
    auto* entry = node->m_entry;
    if (clear) env.ClearSurface(manager, entry->m_surface);
    env.Invalidate(entry->m_dialog);
}
template<class Env> void InvalidateAllDialogs(Env& env, Manager* manager, uint8_t clear)
{
    for (Node* node = View(manager).m_dialogs->m_next; node != View(manager).m_dialogs; node = node->m_next) {
        auto* entry = node->m_entry;
        if (clear) env.ClearSurface(manager, entry->m_surface);
        env.Invalidate(entry->m_dialog);
    }
}
template<class Env> void RefreshAllDialogs(Env& env, Manager* manager)
{
    void* provider = manager->m_variableProvider;
    for (Node* node = View(manager).m_dialogs->m_next; node != View(manager).m_dialogs; node = node->m_next) {
        auto* entry = node->m_entry;
        if (entry->m_state == 1) {
            manager->m_variableProvider = entry->m_provider;
            env.Refresh(entry->m_dialog);
            env.InvalidateByName(manager, entry->m_dialog->m_name, 1);
        }
    }
    manager->m_variableProvider = provider;
}
template<class Env> void SetContext(Env& env, Manager* manager, int index, int value)
{
    int previous = manager->m_context[index];
    manager->m_context[index] = value;
    switch (index) {
    case 1:
    case 2:
    case 10:
        if (previous != value) RefreshAllDialogs(env, manager);
        break;
    case 6:
        if (previous != value) env.InvalidateByName(manager, "ObjectBrowser", 0);
        break;
    case 7:
    case 9:
    case 12: break;
    default:
        for (Node* node = View(manager).m_dialogs->m_next; node != View(manager).m_dialogs; node = node->m_next)
            env.Invalidate(node->m_entry->m_dialog);
    }
}
template<class Env> void AnimateDialog(Env& env, Manager* manager, const char* name, int type, float speed, int delay)
{
    Node* node = Find(manager, name);
    if (node != View(manager).m_dialogs) env.Animate(node->m_entry->m_dialog, type, speed, delay);
}
// ---- Opening, closing and toggling ----
using Dialog = ui::GuiDialogData;
using Entry = ui::GuiManagerDialogEntry;
inline constexpr int kPositionlessMode = 6; // GuiManager modes 6 (GuiMode "camera") never remember or restore window positions

inline bool PositionsRemembered(Manager* manager)
{
    return View(manager).m_mode != kPositionlessMode && View(manager).m_requestedMode != kPositionlessMode;
}
template<class Env> void RememberDialogPosition(Env& env, Manager* manager, int x, int y, int key)
{
    if (PositionsRemembered(manager)) env.StorePosition(x, y, key);
}

template<class Env> void MarkClosing(Env& env, Manager* manager, Entry* entry)
{
    entry->m_state = 2;
    Dialog* dialog = entry->m_dialog;
    RememberDialogPosition(env, manager, dialog->m_x0, dialog->m_y0, dialog->m_positionKey);
    if (entry->m_provider) env.ProviderClosed(entry->m_provider);
}
// True when an active dialog is modal (blocks the panel toggles and the HUD): the first modal active entry decides.
inline bool IsBlockingDialogOpen(Manager* manager)
{
    Node* head = View(manager).m_dialogs;
    for (Node* node = head->m_next; node != head; node = node->m_next)
        if (node->m_entry->m_state == 1 && node->m_entry->m_dialog->m_modal) return true;
    return false;
}
inline int GetDialogCount(Manager* manager)
{
    int count = 0;
    Node* head = View(manager).m_dialogs;
    for (Node* node = head->m_next; node != head; node = node->m_next) ++count;
    return count;
}
// A linear search of the open-dialog list: the first node in [first, last) whose dialog is `name`, else `last`.
inline Node* FindDialogNode(Node* first, Node* last, const char* name)
{
    for (; first != last; first = first->m_next)
        if (std::strcmp(first->m_entry->m_dialog->m_name, name) == 0) break;
    return first;
}

inline constexpr int kListBoxType = 0x16;
inline ui::GuiWidget* FindListBox(Manager* manager, const char* name)
{
    for (auto* node = View(manager).m_widgets; node; node = node->m_next) {
        auto* widget = node->m_widget;
        if (widget->m_type == kListBoxType && std::strcmp(static_cast<const ui::GuiListBoxData*>(widget)->m_name, name) == 0) return widget;
    }
    return nullptr;
}
// GuiManager::ShowMessageBox: the key is read by the dialog's MessageText while it opens.
template<class Env> void ShowMessageBox(Env& env, Manager* manager, const char* name, const char* messageKey, void* provider, bool modal)
{
    manager->m_messageTextKey = messageKey;
    env.ShowDialog(manager, name, provider, modal);
}

template<class Env> void UpdateDialogs(Env& env, Manager* manager, float dt)
{
    Node* head = View(manager).m_dialogs;
    for (Node* node = head->m_next; node != head;) {
        Entry* entry = node->m_entry;
        bool closing = entry->m_state == 2;
        if (closing) {
            env.SetDialogOpen(entry->m_dialog, false);
            env.SetDialogOpen(entry->m_dialog, false);
            env.ReleaseSurface(entry->m_surface);
            env.DeleteEntry(entry);
        }
        Node* next = node->m_next;
        if (closing) env.EraseNode(node);
        node = next;
    }
    for (Node* node = head->m_next; node != head; node = node->m_next) {
        Entry* entry = node->m_entry;
        if (entry->m_state == 0) {
            manager->m_variableProvider = entry->m_provider;
            env.SetDialogOpen(entry->m_dialog, true);
            entry->m_state = 1;
        }
    }
    for (Node* node = head->m_next; node != head; node = node->m_next) {
        Entry* entry = node->m_entry;
        if (entry->m_state == 1) {
            manager->m_variableProvider = entry->m_provider;
            env.UpdateDialog(entry->m_dialog, dt);
        }
    }
}
template<class Env> bool CloseDialog(Env& env, Manager* manager, const char* name)
{
    Node* node = Find(manager, name);
    if (node == View(manager).m_dialogs) return false;
    MarkClosing(env, manager, node->m_entry);
    return true;
}
template<class Env> void CloseAllDialogs(Env& env, Manager* manager)
{
    Node* head = View(manager).m_dialogs;
    for (Node* node = head->m_next; node != head; node = node->m_next) MarkClosing(env, manager, node->m_entry);
}
// An unknown id closes every entry and still answers false: callers rely on that to dismiss whatever is open.
template<class Env> bool CloseDialogById(Env& env, Manager* manager, int id)
{
    Node* head = View(manager).m_dialogs;
    for (Node* node = head->m_next; node != head; node = node->m_next) {
        if (node->m_entry->m_dialog->m_id == id) {
            MarkClosing(env, manager, node->m_entry);
            return true;
        }
    }
    CloseAllDialogs(env, manager);
    return false;
}
template<class Env> bool CloseDialogByProvider(Env& env, Manager* manager, void* provider)
{
    bool closed = false;
    Node* head = View(manager).m_dialogs;
    for (Node* node = head->m_next; node != head; node = node->m_next) {
        if (node->m_entry->m_provider == provider) {
            MarkClosing(env, manager, node->m_entry);
            closed = true;
        }
    }
    return closed;
}
// Escape: only the first entry, only when it is active, not animating and not modal; no position is remembered.
template<class Env> void CloseFirstDialog(Env& env, Manager* manager)
{
    Node* head = View(manager).m_dialogs;
    if (head->m_next == head) return;
    Entry* entry = head->m_next->m_entry;
    if (entry->m_state != 1 || entry->m_dialog->m_animating || entry->m_dialog->m_modal) return;
    entry->m_state = 2;
    if (entry->m_provider) env.ProviderClosed(entry->m_provider);
}
template<class Env> void SelectDialogTab(Env& env, Manager* manager, const char* name, int tab)
{
    Node* node = Find(manager, name);
    if (node != View(manager).m_dialogs) env.SelectTab(node->m_entry->m_dialog, tab);
}
// Opens the named DialogItem: false when the script has no such dialog, true otherwise (also when it is already open for this
// provider and nothing happens). A new window replaces any open one with the same position key.
template<class Env> bool ShowDialog(Env& env, Manager* manager, const char* name, void* provider, bool modal)
{
    auto& view = View(manager);
    ui::GuiManagerDefinitionNode* definition = view.m_definitions;
    while (definition && std::strcmp(definition->m_dialog->m_name, name) != 0) definition = definition->m_next;
    if (!definition) return false;
    Dialog* dialog = definition->m_dialog;
    Node* head = view.m_dialogs;
    if (provider) {
        for (Node* node = head->m_next; node != head; node = node->m_next)
            if (node->m_entry->m_dialog == dialog && node->m_entry->m_provider == provider) return true;
    }
    for (Node* node = head->m_next; node != head; node = node->m_next) {
        Entry* open = node->m_entry;
        if (open->m_dialog->m_positionKey == dialog->m_positionKey) {
            open->m_state = 2;
            if (open->m_provider) env.ProviderClosed(open->m_provider);
            break;
        }
    }
    Entry* entry = env.NewEntry();
    entry->m_dialog = dialog;
    entry->m_state = 0;
    int width = dialog->m_x1 - dialog->m_x0, height = dialog->m_y1 - dialog->m_y0;
    int x, y;
    if (!modal && dialog->m_movable && PositionsRemembered(manager) && env.FindPosition(dialog->m_positionKey, &x, &y)) {
        uint32_t screenWidth, screenHeight;
        env.ScreenSize(screenWidth, screenHeight);

        if (static_cast<uint32_t>(x) + static_cast<uint32_t>(width) < screenWidth &&
                static_cast<uint32_t>(y) + static_cast<uint32_t>(height) < screenHeight)
            env.Translate(dialog, x - dialog->m_x0, y - dialog->m_y0);
    }
    int surface = env.CreateSurface(manager, width, height);
    env.ClearSurface(manager, surface);
    entry->m_surface = surface;
    env.SetSurfaceOrigin(manager, surface, dialog->m_x0, dialog->m_y0);
    env.PrependNode(manager, entry); // newest first: Escape (CloseFirstDialog) closes the latest window
    entry->m_provider = provider;
    env.Invalidate(dialog);
    env.PrepareOpen(dialog);
    dialog->m_modal = modal;
    return true;
}
template<class Env> bool ToggleDialog(Env& env, Manager* manager, const char* name, void* provider, bool modal)
{
    bool closed = CloseDialog(env, manager, name);
    if (!closed) ShowDialog(env, manager, name, provider, modal);
    return !closed;
}
template<class Env> void ShowDialogWithText(Env& env, Manager* manager, const char* name, const char* textKey, void* provider, bool modal)
{
    manager->m_scriptTextKey = textKey;
    ShowDialog(env, manager, name, provider, modal);
}

inline constexpr int kBlockedMode = 2;
template<class Env> bool PanelRefused(Env& env, Manager* manager)
{
    if (View(manager).m_mode == kBlockedMode || IsBlockingDialogOpen(manager)) {
        env.PlayRefusal();
        return true;
    }
    return false;
}
template<class Env> void ToggleLoadGameMenu(Env& env, Manager* manager)
{
    if (PanelRefused(env, manager)) return;
    if (IsDialogOpen(manager, "LoadGameMenu")) {
        env.CloseDialog(manager, "LoadGameMenu");
        return;
    }
    env.CloseDialog(manager, "SaveGameMenu");
    env.ScanSaveSlots();
    env.ShowDialog(manager, "LoadGameMenu", nullptr, false);
    env.FillLoadList();
}
template<class Env> void ToggleSaveGameMenu(Env& env, Manager* manager)
{
    if (PanelRefused(env, manager)) return;
    if (IsDialogOpen(manager, "SaveGameMenu")) {
        env.CloseDialog(manager, "SaveGameMenu");
        return;
    }
    env.CloseDialog(manager, "LoadGameMenu");
    env.ScanSaveSlots();
    env.ShowDialog(manager, "SaveGameMenu", nullptr, false);
    env.FillSaveList();
}
template<class Env> void ToggleInGameOptions(Env& env, Manager* manager)
{
    if (PanelRefused(env, manager)) return;
    if (IsDialogOpen(manager, "InGameOptions")) {
        env.CloseDialog(manager, "InGameOptions");
        return;
    }
    env.ShowDialog(manager, "InGameOptions", nullptr, false);
    SelectDialogTab(env, manager, "InGameOptions", 0);
}
template<class Env> void ToggleMusicPanel(Env& env, Manager* manager)
{
    if (PanelRefused(env, manager)) return;
    bool closed = env.CloseDialog(manager, "MP3Panel");
    if (!closed) env.ShowDialog(manager, "MP3Panel", nullptr, false);
    env.MusicPanelVisibilityChanged(View(manager).m_musicPlayer, !closed);
}

template<class Env> void ToggleBuildPanel(Env& env, Manager* manager)
{
    if (PanelRefused(env, manager)) return;
    View(manager).m_requestedMode = 4;
    CloseAllDialogs(env, manager);
    if (!env.CloseDialog(manager, "BuildPanel")) env.ShowDialog(manager, "BuildPanel", nullptr, false);
}
template<class Env> void ToggleSceneryPanel(Env& env, Manager* manager)
{
    if (PanelRefused(env, manager)) return;
    if (IsDialogOpen(manager, "SceneryPanel")) {
        Node* node = Find(manager, "SceneryPanel");
        if (node != View(manager).m_dialogs) env.Animate(node->m_entry->m_dialog, 2, 1.0f, 0);
        View(manager).m_requestedMode = 4;
        return;
    }
    env.CloseDialog(manager, "BuildPanel");
    for (auto* button = View(manager).m_widgets; button; button = button->m_next)
        if (button->m_widget->m_type == 0) env.SetButtonGraphic(button->m_widget, "MinSceneryPanel", "UI3_sc_in");
    env.ShowDialog(manager, "SceneryPanel", nullptr, false);
    Node* node = Find(manager, "SceneryPanel");
    if (node != View(manager).m_dialogs) env.Animate(node->m_entry->m_dialog, 1, 1.0f, 0);
}
template<class Env> void ShowPauseUnlessScenarioEnded(Env& env, Manager* manager)
{
    if (!IsDialogOpen(manager, "ScenarioWon") && !IsDialogOpen(manager, "ScenarioLost")) env.ShowDialog(manager, "PauseBox", nullptr, true);
}
template<class Env> void ClosePauseDialog(Env& env, Manager* manager)
{
    env.CloseDialog(manager, "PauseBox");
}
} // namespace obl::gui_manager_dialogs

namespace obl::gui {
bool RegisterGuiDialogDrivers();
// Removes the message balloon of the GUI manager when the world is left (gui_manager_messages::DropMessage).
void DropGuiMessage();
} // namespace obl::gui
