#include "core/char16.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_tooltip.h"
#include "game/ui/sound_api.h"
#include "game/ui/renderer_api.h"
#include "game/ui/text_renderer.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/save_slots.h"
#include "game/game/settings_store.h"
#include "game/ui/gui_controls.h"
#include "game/ui/gui_lists.h"
#include "game/ui/gui_containers.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/gui_manager_messages.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "game/feedback/feedback_manager.h"
#include "game/game/game_api.h"
#include "game/ui/text_table.h"
#include "game/ui/music_player.h"
#include "game/ui/sound_types.h"
#include "game/game/game_accessors.h"
#include "core/call_counter.h"
#include <bit>
#include <iterator>

namespace obl::gui {
namespace {
using namespace gui_manager_dialogs;
using Dialog = ui::GuiDialogData;
// Call counters (`call-report ui.gui-manager-dialogs`), and the two drivers the tests call.
test::CallCounter g_counter("ui.gui-manager-dialogs");
test::CallCounter g_live("GuiManagerDialogsLive");
test::CallCounter g_panelDrive("GuiManagerPanelsDrive");
struct GameEnv {
    void ClearSurface(Manager* manager, int surface) { obl::ui::renderer::ClearSurface(View(manager).m_renderer, surface); }
    void Refresh(Dialog* dialog) { dialog->Refresh(); }
    void Invalidate(Dialog* dialog) { dialog->Invalidate(); }
    void InvalidateByName(Manager* manager, const char* name, uint8_t clear) { manager->InvalidateDialog(name, clear); }
    void StorePosition(int x, int y, int key) { game::settings_store::StoreDialogPosition(game::settings_store::Current(), x, y, key); }
    bool FindPosition(int key, int* x, int* y)
    {
        return game::settings_store::FindDialogPosition(game::settings_store::Current(), key, x, y);
    }
    void ProviderClosed(void* provider) { provider_virtual::Closed(provider); }
    ui::GuiManagerDialogEntry* NewEntry() { return NewGuiRecord<ui::GuiManagerDialogEntry>(); }
    void ScreenSize(uint32_t& width, uint32_t& height)
    {
        width = static_cast<uint32_t>(game::GetRenderer()->m_width);
        height = static_cast<uint32_t>(game::GetRenderer()->m_height);
    }
    void Translate(Dialog* dialog, int dx, int dy) { (static_cast<ui::GuiWidget*>(dialog))->Translate(dx, dy); }
    int CreateSurface(Manager* manager, int width, int height)
    {
        return obl::ui::renderer::CreateSurface(View(manager).m_renderer, width, height, 0);
    }
    void SetSurfaceOrigin(Manager* manager, int surface, int x, int y)
    {
        obl::ui::renderer::SetSurfaceOrigin(View(manager).m_renderer, surface, x, y);
    }
    // The node is an operator new block: UpdateDialogs frees it with operator delete.
    void PrependNode(Manager* manager, ui::GuiManagerDialogEntry* entry)
    {
        auto* node = GuiAllocDialogNode(&entry);
        Node* head = View(manager).m_dialogs;
        Node* first = head->m_next;
        node->m_next = first;
        node->m_previous = head;
        head->m_next = node;
        first->m_previous = node;
    }
    void PlayRefusal() { audio::CurrentSound()->Play("click_bad_01", 1, 0.0f); }
    bool CloseDialog(Manager* manager, const char* name) { return manager->CloseDialog(name); }
    void ShowDialog(Manager* manager, const char* name, void* provider, bool modal) { manager->ShowDialog(name, provider, modal); }
    void ScanSaveSlots() { ui::save_panel::ScanSaveSlots(); }
    void FillLoadList() { ui::save_panel::FillLoadList(); }
    void FillSaveList() { ui::save_panel::FillSaveList(); }
    void MusicPanelVisibilityChanged(void* player, bool visible)
    {
        static_cast<audio::MusicPlayer*>(player)->OnPanelVisibilityChanged(visible ? 1 : 0);
    }
    void SetButtonGraphic(ui::GuiWidget* button, const char* action, const char* sprite)
    {
        button_action_facade::SetGraphicByAction(static_cast<ui::GuiButton*>(button), action, sprite);
    }
    void PrepareOpen(Dialog* dialog) { (static_cast<ui::GuiWidget*>(dialog))->SetOrigin(0, 0); }
    void SelectTab(Dialog* dialog, int tab) { (static_cast<ui::GuiWidget*>(dialog))->SelectTab(tab); }
    void Animate(Dialog* dialog, int type, float speed, int delay)
    {
        container::DialogStartAnimation(dialog, type, std::bit_cast<uint32_t>(speed), delay);
    }
    void SetDialogOpen(Dialog* dialog, bool open) { (static_cast<ui::GuiWidget*>(dialog))->SetVisible(open ? 1 : 0); }
    void UpdateDialog(Dialog* dialog, float dt) { static_cast<ui::GuiWidget*>(dialog)->Update(dt); }
    void ReleaseSurface(int surface) { obl::ui::renderer::ReleaseSurface(game::GetRenderer(), surface); }
    void DeleteEntry(ui::GuiManagerDialogEntry* entry) { delete entry; }
    // Unlinks the node and frees it (AllocDialogNode allocated it).
    void EraseNode(Node* node)
    {
        node->m_previous->m_next = node->m_next;
        node->m_next->m_previous = node->m_previous;
        ::operator delete(node);
    }
};

void DriveLive(int expectedLength)
{
    auto* manager = game::GetGuiManager();
    int length = 0;
    bool match = manager->VariableProvider() == manager->m_variableProvider;
    for (Node* node = View(manager).m_dialogs->m_next; node != View(manager).m_dialogs; node = node->m_next) {
        ++length;
        const char* name = node->m_entry->m_dialog->m_name;
        match = match && manager->IsDialogOpen(name) == gui::IsDialogOpen(manager, name) &&
                manager->GetDialogMinimized(name) == gui::GetDialogMinimized(manager, name);
    }
    match = match && length == expectedLength && !IsDialogOpen(manager, "__missing_dialog__") &&
            !manager->IsDialogOpen("__missing_dialog__");
    g_live.Record(match, "entries=" + std::to_string(length));
}
// Calls one panel function of the live manager through its entry.
void DrivePanel(int which)
{
    static void (Manager::* const kPanels[])() = {&Manager::ToggleLoadGameMenu, &Manager::ToggleSaveGameMenu, &Manager::ToggleInGameOptions,
            &Manager::ToggleMusicPanel, &Manager::ToggleBuildPanel, &Manager::ToggleSceneryPanel, &Manager::ShowPauseUnlessScenarioEnded,
            &Manager::ClosePauseDialog};
    if (which < 0 || which >= static_cast<int>(std::size(kPanels))) {
        g_panelDrive.Record(false, "index");
        return;
    }
    (game::GetGuiManager()->*kPanels[which])();
    g_panelDrive.Count();
}
// ---- Messages and ticker ----
namespace msg = gui_manager_messages;
struct MessageEnv {
    void DeleteBalloon(void* balloon) { gui::DestroyWidget(*static_cast<ui::GuiWidget*>(balloon)); }
    int MeasureText(const void* renderer, const Char16* text) { return ui::font_text::MeasureWidth(renderer, text); }
    int ScreenWidth() { return static_cast<int>(game::GetRenderer()->m_width); }
    void* CreateBalloon(int x0, int y0, int x1, int y1, const Char16* text, int font, uint32_t color, int flags)
    {
        return GuiTextConstructLiteral(NewGuiRecord<ui::GuiText>(), x0, y0, x1, y1, text, font, color, static_cast<uint32_t>(flags));
    }
    void PlayNewMessage() { audio::CurrentSound()->PlayScaled("NewMessage", 0.5f, 1, 0.0f); }
    bool GamePaused() { return game::GetWorld()->m_paused != 0; }
    float GameSpeed() { return game::GetWorld()->m_gameSpeed; }
    void Translate(void* widget, int dx, int dy) { (static_cast<ui::GuiWidget*>(widget))->Translate(dx, dy); }
    msg::Rect Bounds(void* widget)
    {
        int32_t out[4];
        (static_cast<ui::GuiWidget*>(widget))->GetBounds(out);
        return {out[0], out[1], out[2], out[3]};
    }
    // TickerList
    const Char16* DayLabel() { return ui::text_table::Lookup("TICKER_DAY"); }
    const Char16* BeginHistory(float* hour, uint32_t* day, uint32_t* objectId)
    {
        return feedback::Feedback::OfWorld().BeginHistory(hour, day, objectId);
    }
    const Char16* NextHistory(float* hour, uint32_t* day, uint32_t* objectId)
    {
        return feedback::Feedback::OfWorld().NextHistory(hour, day, objectId);
    }
    int WholeHour(float hour) { return game::GameTimeWholeHour(hour); }
    int QuarterMinutes(float hour) { return game::GameTimeQuarterMinutes(hour); }
    void ClearRows(void* list) { listbox::ClearRows(static_cast<ui::GuiListBox*>(list)); }
    void AddText(void* list, int row, int offset, int width, uint32_t unused, uint32_t flags, int font, uint32_t color, const Char16* text,
            uint32_t newRow)
    {
        listbox::AddText(static_cast<ui::GuiListBox*>(list), row, offset, width, unused, flags, font, color, text, newRow);
    }
    void SetRowButton(void* list, int row, int offset, const char* graphic, int value)
    {
        listbox::SetRowButton(static_cast<ui::GuiListBox*>(list), row, offset, graphic, value);
    }
    bool PollRowButton(void* list, uint32_t row, int* value)
    {
        return listbox::PollRowButton(static_cast<ui::GuiListBox*>(list), int32_t(row), value) != 0;
    }
    void* FindListBox(const char* name) { return game::GetGuiManager()->FindListBox(name); }
    void DisableListBox(void* list) { static_cast<ui::GuiListBox*>(list)->m_enabled = 0; }
    void SelectObject(int objectId)
    {
        void* object = object::object_manager::ObjectById(game::GetObjectManager(), objectId);
        if (object) provider_virtual::Select(object, 1, 1);
    }
};

} // namespace
} // namespace obl::gui
namespace obl::ui {
void GuiManager::RememberDialogPosition(const int32_t* rect, int32_t key)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::RememberDialogPosition(env, this, rect[0], rect[1], key);
}
bool GuiManager::IsDialogOpen(const char* name)
{
    gui::g_counter.Count();
    return gui::IsDialogOpen(this, name);
}
int GuiManager::GetDialogMinimized(const char* name)
{
    gui::g_counter.Count();
    return gui::GetDialogMinimized(this, name);
}
void* GuiManager::VariableProvider()
{
    gui::g_counter.Count();
    return m_variableProvider;
}
void GuiManager::RefreshAllDialogs()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::RefreshAllDialogs(env, this);
}
void GuiManager::InvalidateAllDialogs(uint8_t clear)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::InvalidateAllDialogs(env, this, clear);
}
void GuiManager::InvalidateDialog(const char* name, uint8_t clear)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::InvalidateDialog(env, this, name, clear);
}
bool GuiManager::CloseDialog(const char* name)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui::CloseDialog(env, this, name);
}
void GuiManager::SetDialogMinimized(const char* name, int value)
{
    gui::g_counter.Count();
    gui::SetDialogMinimized(this, name, value);
}
void GuiManager::SetContext(int index, int value)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::SetContext(env, this, index, value);
}
void GuiManager::AnimateDialog(const char* name, int type, float speed, int delay)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::AnimateDialog(env, this, name, type, speed, delay);
}
bool GuiManager::ToggleDialog(const char* name, void* provider, bool modal)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui::ToggleDialog(env, this, name, provider, modal);
}
void GuiManager::ShowDialogWithText(const char* name, const char* textKey, void* provider, bool modal)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ShowDialogWithText(env, this, name, textKey, provider, modal);
}
bool GuiManager::CloseDialogById(int id)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui::CloseDialogById(env, this, id);
}
void GuiManager::CloseAllDialogs()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::CloseAllDialogs(env, this);
}
void GuiManager::CloseFirstDialog()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::CloseFirstDialog(env, this);
}
void GuiManager::SelectDialogTab(const char* name, int tab)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::SelectDialogTab(env, this, name, tab);
}
bool GuiManager::IsBlockingDialogOpen()
{
    gui::g_counter.Count();
    return gui::IsBlockingDialogOpen(this);
}
int GuiManager::GetDialogCount()
{
    gui::g_counter.Count();
    return gui::GetDialogCount(this);
}
void GuiManager::ToggleLoadGameMenu()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ToggleLoadGameMenu(env, this);
}
void GuiManager::ToggleSaveGameMenu()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ToggleSaveGameMenu(env, this);
}
void GuiManager::ToggleInGameOptions()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ToggleInGameOptions(env, this);
}
void GuiManager::ToggleMusicPanel()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ToggleMusicPanel(env, this);
}
void GuiManager::ToggleBuildPanel()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ToggleBuildPanel(env, this);
}
void GuiManager::ToggleSceneryPanel()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ToggleSceneryPanel(env, this);
}
void GuiManager::ShowPauseUnlessScenarioEnded()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ShowPauseUnlessScenarioEnded(env, this);
}
void GuiManager::ClosePauseDialog()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ClosePauseDialog(env, this);
}
void GuiManager::UpdateDialogs(float dt)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::UpdateDialogs(env, this, dt);
}
void* GuiManager::FindListBox(const char* name)
{
    gui::g_counter.Count();
    return gui::FindListBox(this, name);
}
void GuiManager::ShowMessageBox(const char* name, const char* messageKey, void* provider, bool modal)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui::ShowMessageBox(env, this, name, messageKey, provider, modal);
}
void GuiManager::ShowMessage(const Char16* text, int mode)
{
    gui::g_counter.Count();
    gui::MessageEnv env;
    gui::msg::ShowMessage(env, this, text, mode);
}
void GuiManager::ShowMessageKey(const char* key, int mode)
{
    gui::g_counter.Count();
    ShowMessage(text_table::Lookup(key), mode);
}
bool GuiManager::ShowDialog(const char* name, void* provider, bool modal)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui::ShowDialog(env, this, name, provider, modal);
}
bool GuiManager::CloseDialogByProvider(void* provider)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui::CloseDialogByProvider(env, this, provider);
}
bool GuiManager::CanShowMessage()
{
    gui::g_counter.Count();
    return gui::msg::CanShowMessage(this);
}
void GuiManager::UpdateTickerAnimation(float dt)
{
    gui::g_counter.Count();
    gui::MessageEnv env;
    gui::msg::UpdateTickerAnimation(env, this, dt);
}
void GuiManager::SetTickerBounds(int left, int top, int right, int bottom)
{
    gui::g_counter.Count();
    gui::MessageEnv env;
    gui::msg::SetTickerBounds(env, this, left, top, right, bottom);
}
void TickerList::Update()
{
    gui::g_counter.Count();
    gui::MessageEnv env;
    gui::msg::TickerUpdate(env, this);
}
void TickerList::SetVisible(uint8_t visible)
{
    gui::g_counter.Count();
    gui::MessageEnv env;
    gui::msg::TickerSetVisible(env, this, visible);
}
} // namespace obl::ui
namespace obl::gui {
void DropGuiMessage()
{
    Manager* manager = game::GetGuiManager();
    if (!manager) return;
    MessageEnv env;
    msg::DropMessage(env, manager);
}
bool RegisterGuiDialogDrivers()
{
    g_live.SetDriver(DriveLive);
    g_panelDrive.SetDriver(DrivePanel);
    return true;
}
} // namespace obl::gui
