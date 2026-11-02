#include "core/text16.h"
#include "game/game/host_text.h"
#include "game/ui/input_types.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_trivial_slots.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/sound_api.h"
#include "game/ui/gui_controls.h"
#include "game/game/game_api.h"
#include "game/game/settings_store.h"
#include "game/weather/weather.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/text_table.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/ui/gui_action_entries.h"
#include "game/game/command_loop.h"
#include "game/ui/gui_definitions.h"
#include "game/ui/gui_canvas_live.h"
#include "game/ui/sound_types.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_types.h"
#include "game/objects/object_base.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_widgets_image_bar.h"
#include "game/ui/gui_containers.h"
#include "core/ascii_case.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <cwchar>
#include <random>
#include <string>
#include <vector>

namespace obl::gui {
namespace {
using Base = ui::GuiWidget;
using Button = ui::GuiButton;
using CheckBox = ui::GuiCheckBox;
using Radio = ui::GuiRadioButton;
using Group = ui::GuiRadioGroup;
using Node = ui::GuiContainerChildNode;
using Instance = ui::SpriteInstance;
using Gui = ui::GuiManager;
test::CallCounter g_counter("ui.gui-controls"); // call counter
ui::SpriteBank* Bank()
{
    return ui::GetSpriteBank();
}
Gui* Manager()
{
    return game::GetGuiManager();
}
float Now()
{
    return game::GetGameClock()->m_time;
}
Instance* Create(const char* name)
{
    return (static_cast<ui::SpriteBank*>(Bank()))->CreateObservedInstance(name);
}
const ui::SpriteAnim* Animation(const Instance* p)
{
    return p->m_sprite->m_anims[p->m_animIndex];
}
const Char16* Lookup(const char* key)
{
    return ui::text_table::Lookup(key);
}

void* Allocate(size_t size)
{
    return ::operator new(size);
}
void Delete(void* p)
{
    ::operator delete(p);
}
char* Copy(const char* text)
{
    const size_t size = std::strlen(text) + 1;
    auto* p = static_cast<char*>(Allocate(size));
    std::memcpy(p, text, size);
    return p;
}
void Release(ui::SpriteBank* bank, void*, Instance* p)
{
    (static_cast<ui::SpriteBank*>(bank))->ReleaseInstance(p);
}
bool HitSprite(Instance* p, void*, int x, int y)
{
    return (p)->HitTest(x, y);
}
void Track(Base* w, void*, int x, int y, int state)
{
    w->TrackTooltip(x, y, state);
}
void ClickTooltip(Base* w, void*, int x, int y, int state)
{
    w->HideTooltipOnClick(state);
}
void HideTooltip(Base* w, void*)
{
    w->HideTooltip();
}
void PlayClick()
{
    audio::CurrentSound()->Play("ButtonClick", 1, 0.0f);
}
void GameGet(game::Game*, void*, const char* name, Char16* out)
{
    game::game_api::GetVariable(name, out);
}
void GameSet(game::Game*, void*, const char* name, const int* value)
{
    game::game_api::SetVariable(name, value);
}
void* Provider()
{
    return static_cast<ui::GuiManager*>(Manager())->VariableProvider();
}
void Read(void* provider, const char* key, Char16* out)
{
    if (provider)
        provider_virtual::GetVariableText(provider, key, out);
    else
        GameGet(game::GetGame(), nullptr, key, out);
}
void Write(void* provider, const char* key, int value)
{
    if (provider) {
        if (const auto control = command::MakeObjectControl(static_cast<object::Base*>(provider)->m_id, key, static_cast<float>(value)))
            std::visit([](const auto& typed) { game::SubmitCommand(typed); }, *control);
    } else
        GameSet(game::GetGame(), nullptr, key, &value);
}
int ReadInt(void* provider, const char* key)
{
    Char16 text[32];
    Read(provider, key, text);
    return text16::ParseLong(text, nullptr, 10);
}

struct Action {
    const char* m_name;
    ui::GuiAction m_function;
};
constexpr Action kActions[] = {
        {"ClosePause", gui_action_entries::ClosePause},
        {"SetStaffShift", gui_action_entries::SetStaffShift},
        {"SetStaffWages", gui_action_entries::SetStaffWages},
        {"PlaySandBox", gui_action_entries::PlaySandBox},
        {"ResetProgress", gui_action_entries::ResetProgress},
        {"ResetProgressConfirm", gui_action_entries::ResetProgressConfirm},
        {"CreditsMenu", gui_action_entries::CreditsMenu},
        {"ExitGameConfirm", gui_action_entries::ExitGameConfirm},
        {"WonGame", gui_action_entries::WonGame},
        {"ResetGameConfirm", gui_action_entries::ResetGameConfirm},
        {"ResetGame", gui_action_entries::ResetGame},
        {"ToggleRadarSize", gui_action_entries::ToggleRadarSize},
        {"ToggleProgressSize", gui_action_entries::ToggleProgressSize},
        {"ExitSaveOK", gui_action_entries::ExitSaveOK},
        {"DeleteLoadGameConfirm", gui_action_entries::DeleteLoadGameConfirm},
        {"DeleteLoadGame", gui_action_entries::DeleteLoadGame},
        {"DeleteSaveGameConfirm", gui_action_entries::DeleteSaveGameConfirm},
        {"DeleteSaveGame", gui_action_entries::DeleteSaveGame},
        {"SaveGameConfirm", gui_action_entries::SaveGameConfirm},
        {"SaveGame", gui_action_entries::SaveGame},
        {"SaveGamePanel", gui_action_entries::SaveGamePanel},
        {"LoadGamePanel", gui_action_entries::LoadGamePanel},
        {"LoadGame", gui_action_entries::LoadGame},
        {"ExitLoadGame", gui_action_entries::ExitLoadGame},
        {"LoadGameMenu", gui_action_entries::LoadGameMenu},
        {"ExtrasMenu", gui_action_entries::ExtrasMenu},
        {"StaffSetAllWages", gui_action_entries::StaffSetAllWages},
        {"StaffSetAllShift", gui_action_entries::StaffSetAllShift},
        {"HireNewStaff", gui_action_entries::HireNewStaff},
        {"IncStaffHours", gui_action_entries::IncStaffHours},
        {"DecStaffHours", gui_action_entries::DecStaffHours},
        {"MechanicBrowser", gui_action_entries::MechanicBrowser},
        {"RepBrowser", gui_action_entries::RepBrowser},
        {"LifeguardBrowser", gui_action_entries::LifeguardBrowser},
        {"SecurityBrowser", gui_action_entries::SecurityBrowser},
        {"BuilderBrowser", gui_action_entries::BuilderBrowser},
        {"CleanerBrowser", gui_action_entries::CleanerBrowser},
        {"MoneyPanel", gui_action_entries::MoneyPanel},
        {"ResetAudioOptions", gui_action_entries::ResetAudioOptions},
        {"ResetVideoOptions", gui_action_entries::ResetVideoOptions},
        {"FerryPanel", gui_action_entries::FerryPanel},
        {"IslandStatus", gui_action_entries::IslandStatus},
        {"DoCameraGrab", gui_action_entries::DoCameraGrab},
        {"ShowTickerList800", gui_action_entries::ShowTickerList800},
        {"ShowTickerList640", gui_action_entries::ShowTickerList640},
        {"MinSceneryPanel", gui_action_entries::MinSceneryPanel},
        {"SceneryDozer", gui_action_entries::SceneryDozer},
        {"MP3_Sun", gui_action_entries::MP3Sun},
        {"MP3_Moon", gui_action_entries::MP3Moon},
        {"MP3_Repeat", gui_action_entries::MP3Repeat},
        {"MP3_Shuffle", gui_action_entries::MP3Shuffle},
        {"MP3_Delete", gui_action_entries::MP3Delete},
        {"MP3_MoveUp", gui_action_entries::MP3MoveUp},
        {"MP3_MoveDown", gui_action_entries::MP3MoveDown},
        {"MP3_VolUp", gui_action_entries::MP3VolUp},
        {"MP3_VolDown", gui_action_entries::MP3VolDown},
        {"MP3_Refresh", gui_action_entries::MP3Refresh},
        {"MP3_RefreshConfirm", gui_action_entries::MP3RefreshConfirm},
        {"MP3_Default", gui_action_entries::MP3Default},
        {"StaffHome", gui_action_entries::StaffHome},
        {"DismissStaff", gui_action_entries::DismissStaff},
        {"DismissConfirm", gui_action_entries::DismissConfirm},
        {"StaffFire", gui_action_entries::StaffFire},
        {"StaffSend", gui_action_entries::StaffSend},
        {"NextStaff", gui_action_entries::NextStaff},
        {"PrevStaff", gui_action_entries::PrevStaff},
        {"BuilderIcon", gui_action_entries::BuilderIcon},
        {"ExitSceneryPanel", gui_action_entries::ExitSceneryPanel},
        {"SceneryPanel", gui_action_entries::SceneryPanel},
        {"PowerPanel", gui_action_entries::PowerPanel},
        {"CallCleaner", gui_action_entries::CallCleaner},
        {"NextBuilding", gui_action_entries::NextBuilding},
        {"PrevBuilding", gui_action_entries::PrevBuilding},
        {"OpenBuilding", gui_action_entries::OpenBuilding},
        {"CloseBuilding", gui_action_entries::CloseBuilding},
        {"CallRepair", gui_action_entries::CallRepair},
        {"MP3Play", gui_action_entries::MP3Play},
        {"MP3_AmpPlay", gui_action_entries::MP3Play},
        {"MP3Next", gui_action_entries::MP3Next},
        {"MP3Previous", gui_action_entries::MP3Previous},
        {"MP3Stop", gui_action_entries::MP3Stop},
        {"MP3Pause", gui_action_entries::MP3Pause},
        {"MP3List", gui_action_entries::MP3List},
        {"ZoomIn", gui_action_entries::ZoomIn},
        {"ZoomOut", gui_action_entries::ZoomOut},
        {"ExitWindows", gui_action_entries::ExitWindows},
        {"ExitWindowsConfirm", gui_action_entries::ExitWindowsConfirm},
        {"ExitDialogMessage", gui_action_entries::ExitDialogMessage},
        {"SellButton", gui_action_entries::SellButton},
        {"ConfirmSell", gui_action_entries::ConfirmSell},
        {"LockCamera", gui_action_entries::LockCamera},
        {"CameraMode", gui_action_entries::CameraMode},
        {"CleanerIcon", gui_action_entries::CleanerIcon},
        {"RepIcon", gui_action_entries::RepIcon},
        {"LifeguardIcon", gui_action_entries::LifeguardIcon},
        {"SecurityIcon", gui_action_entries::SecurityIcon},
        {"MechanicIcon", gui_action_entries::MechanicIcon},
        {"ExitDialog", gui_action_entries::ExitDialog},
        {"ExitDialogResume", gui_action_entries::ExitDialogResume},
        {"BuildList", gui_action_entries::BuildList},
        {"PlaceBuilding", gui_action_entries::PlaceBuilding},
        {"ExitBuildMode", gui_action_entries::ExitBuildMode},
        {"ObjectBrowser", gui_action_entries::ObjectBrowser},
        {"GuestBrowser", gui_action_entries::GuestBrowser},
        {"ExitGame", gui_action_entries::ExitGame},
        {"StartNewGame", gui_action_entries::StartNewGame},
        {"ShowScenSel", gui_action_entries::ShowScenSel},
        {"ShowMainMenu", gui_action_entries::ShowMainMenu},
        {"ShowGoal", gui_action_entries::ShowGoal},
        {"SaveGridMap", gui_action_entries::SaveGridMap},
        {"PaintMap", gui_action_entries::PaintMap},
        {"StopPaint", gui_action_entries::ExitBuildMode},
        {"InGameOptions", gui_action_entries::InGameOptions},
        {"ExitOptions", gui_action_entries::ExitOptions},
        {"IncMaxDist", gui_action_entries::IncMaxDist},
        {"DecMaxDist", gui_action_entries::DecMaxDist},
        {"IncMinDist", gui_action_entries::IncMaxDist},
        {"DecMinDist", gui_action_entries::DecMaxDist},
        {"BreakObject", gui_action_entries::BreakObject},
        {"NONE", gui_action_entries::None},
};
static_assert(std::size(kActions) == 120);
ui::GuiAction FindAction(const char* name)
{
    ui::GuiAction handler = nullptr;
    for (const Action& action : kActions)
        if (ascii::EqualsNoCase(action.m_name, name)) handler = action.m_function;
    return handler;
}
bool Matches(const Button* w, const char* name)
{
    for (const Action& action : kActions)
        if (std::strcmp(action.m_name, name) == 0 && w->m_leftAction == action.m_function) return true;
    return false;
}
// What the constructor cores in gui_controls.h ask of the game.
struct ControlEnv {
    ui::GuiAction FindAction(const char* name) { return gui::FindAction(name); }
    char* CopyString(const char* text) { return Copy(text); }
    Instance* CreateSprite(const char* name) { return Create(name); }
    Instance* CreateUnobservedSprite(const char* name) { return Bank()->CreateInstance(name); }
    void ReleaseSprite(Instance* sprite) { Release(Bank(), nullptr, sprite); }
    int SpriteWidth(const Instance* sprite) { return Animation(sprite)->m_width; }
    int SpriteHeight(const Instance* sprite) { return Animation(sprite)->m_height; }
    void SetAnimationType(Instance* sprite, int type) { sprite->m_animType = type; }
    Node* AllocateNode() { return static_cast<Node*>(::operator new(sizeof(Node))); }
};
void ButtonInit(Button* w, const char* graphic, const char* text, const char* left, const char* right)
{
    ControlEnv env;
    gui_controls::ConstructButton(*w, graphic, text, left, right, env);
}
void ButtonDestruct(Button* w)
{
    if (w->m_graphic && w->m_instance) {
        Release(Bank(), nullptr, w->m_instance);
        w->m_instance = nullptr;
    }
    w->m_state = 0;
    w->SetVisibleBase(0);
    if (w->m_text) Delete(w->m_text);
    if (w->m_graphic) Delete(w->m_graphic);
}
void ButtonVisible(Button* w, uint32_t visible)
{
    if (!static_cast<uint8_t>(visible)) {
        if (w->m_graphic && w->m_instance) {
            Release(Bank(), nullptr, w->m_instance);
            w->m_instance = nullptr;
        }
        w->m_state = 0;
    } else {
        if (w->m_graphic && !w->m_instance) w->m_instance = Create(w->m_graphic);
        w->Invalidate();
    }
    w->SetVisibleBase(visible);
}
bool SpriteHit(Base* w, Instance* p, int x, int y)
{
    const int dx = gui_widgets::Sub(x, w->m_x0), dy = gui_widgets::Sub(y, w->m_y0);
    const bool hit = p && dx > 0 && dx < Animation(p)->m_width && dy > 0 && dy < Animation(p)->m_height && HitSprite(p, nullptr, dx, dy);
    w->m_hovered = hit;
    return hit;
}
bool ButtonHit(Button* w, int x, int y)
{
    bool hit = false;
    if (w->m_state != 3 && w->m_visible) {
        if (w->m_graphic) return SpriteHit(w, w->m_instance, x, y);
        hit = w->HitTestBase(x, y);
    }
    w->m_hovered = hit;
    return hit;
}
// The button's virtual calls, actions and clock as the gui_controls state machines see them.
struct ButtonHost {
    Button* w;
    bool Hovered() { return w->IsHovered(); }
    bool Busy() { return w->IsHeld(); }
    void Invalidate() { w->Invalidate(); }
    float Now() { return gui::Now(); }
    void Fire(bool right) { (right ? w->m_rightAction : w->m_leftAction)(w->m_id); }
    void Click(bool right)
    {
        PlayClick();
        Fire(right);
    }
};
bool RunButtonMouse(Button* w, int x, int y, int state)
{
    if (!w->m_visible) return false;
    Track(w, nullptr, x, y, state);
    w->m_held = 0;
    if (state == 3)
        w->m_hovered = 0;
    else
        w->HitTest(x, y);
    ButtonHost host{w};
    return gui_controls::ButtonMouse(*w, state, host);
}
bool ButtonRight(Button* w, int x, int y, int state)
{
    if (!w->m_visible) return false;
    ClickTooltip(w, nullptr, x, y, state);
    ButtonHost host{w};
    return gui_controls::ButtonRightMouse(*w, state, host);
}
void ButtonUpdate(Button* w, float dt)
{
    if (!w->m_visible) return;
    if (w->m_stateVariable > 0 && w->m_stateVariable < 3) {
        char key[128];
        std::sprintf(key, "%s_STATE", w->m_text);
        if (!ReadInt(Provider(), key))
            w->m_state = 3;
        else if (w->m_state == 3)
            w->m_state = 0;
    }
    if (w->m_state == 3) return;
    ButtonHost host{w};
    gui_controls::ButtonTick(*w, dt, host);
}
void ButtonDraw(Button* w, gui_canvas::Canvas& canvas, gui_canvas::SpriteId sprite)
{
    if (!w->m_visible || (w->m_state == w->m_drawnState && !w->m_dirty)) return;
    if (w->m_graphic) {
        canvas.SetDirection(sprite, gui_controls::ButtonDirection(*w, Animation(w->m_instance)->m_directions));
        canvas.DrawClippedAt(sprite, w->m_x0, w->m_y0, 0, 99999.0f);
    }
    if (w->m_text && w->m_stateVariable != 2) {
        const Char16* text = nullptr;
        Char16 value[128];
        if (w->m_textSource == 0)
            text = Lookup(w->m_text);
        else if (w->m_textSource == 1) {
            Read(Provider(), w->m_text, value);
            text = value;
        }
        // Setters run even for an unknown source, but there is no text draw.
        canvas.SetTextAlign(0, 2);
        canvas.SetTextOutlined(0, false);
        canvas.SetTextColor(0, w->m_graphic ? 0xffffffff : 0);
        canvas.SetTextRect(0, w->m_x0, gui_widgets::Add(w->m_y0, 3), gui_widgets::Sub(w->m_x1, w->m_x0),
                gui_widgets::Sub(gui_widgets::Sub(w->m_y1, w->m_y0), 2));
        if (text) canvas.DrawWrapped(0, 0, 2, text);
    }
    w->m_dirty = 0;
    w->m_drawnState = w->m_state;
}
void ButtonDisable(Button* w, const char* name)
{
    if (Matches(w, name)) w->m_state = 3;
}
void ButtonReset(Button* w, const char* name)
{
    if (Matches(w, name)) w->m_state = 0;
}
void ButtonBlink(Button* w, const char* name, uint32_t value)
{
    if (Matches(w, name)) {
        w->m_blink = static_cast<uint8_t>(value);
        w->Invalidate();
    }
}
void ButtonFlashing(Button* w, const char* name, uint32_t value)
{
    if (Matches(w, name)) {
        w->m_flashing = static_cast<uint8_t>(value);
        if (w->m_flashing) w->m_state = 1;
    }
}
void ButtonVisibleAction(Button* w, const char* name, uint32_t value)
{
    if (Matches(w, name)) w->SetVisible(value);
}
void ButtonGraphic(Button* w, const char* name, const char* graphic)
{
    if (!Matches(w, name) || !w->m_graphic) return;
    Delete(w->m_graphic);
    w->m_graphic = Copy(graphic);
    if (w->m_visible) {
        Release(Bank(), nullptr, w->m_instance);
        w->m_instance = Create(w->m_graphic);
    }
}
void CheckInit(CheckBox* w, const char* graphic, const char* key)
{
    ControlEnv env;
    gui_controls::ConstructCheckBox(*w, graphic, key, env);
}
void CheckDestruct(CheckBox* w)
{
    if (w->m_instance) Release(Bank(), nullptr, w->m_instance); // a default-constructed record owns nothing
    Delete(w->m_variable);
}
void CheckUpdate(CheckBox* w, float)
{
    const int value = ReadInt(Provider(), w->m_variable) != 0;
    if (w->m_value != value) {
        w->m_value = value;
        w->Invalidate();
    }
}
bool CheckMouse(CheckBox* w, int x, int y, int state)
{
    w->m_hovered = w->HitTest(x, y);
    Track(w, nullptr, x, y, state);
    if (state != 1 || !w->IsHovered()) return false;
    w->m_value = w->m_value == 1 ? 0 : 1;
    Write(Provider(), w->m_variable, w->m_value);
    w->Invalidate();
    return true;
}
void RadioInit(Radio* w, const char* graphic, const char* text, int font, int value)
{
    ControlEnv env;
    gui_controls::ConstructRadio(*w, graphic, text, font, value, env);
}
void RadioDestruct(Radio* w)
{
    if (w->m_text) Delete(w->m_text);
    if (w->m_instance) Release(Bank(), nullptr, w->m_instance);
}
void RadioUpdate(Radio* w, float)
{
    if (w->m_flags & 0x14) {
        w->m_flags &= ~8u;
        HideTooltip(w, nullptr);
    } else {
        const uint32_t flags = gui_controls::RadioHoverFlags(w->m_flags, w->IsHovered());
        if (flags != w->m_flags) {
            w->m_flags = flags;
            w->Invalidate();
        }
    }
}
bool RadioMouse(Radio* w, int x, int y, int state)
{
    if (w->m_flags & 0x14) {
        w->m_hovered = 0;
        return false;
    }
    Track(w, nullptr, x, y, state);
    if (state == 0 || state == 1) {
        w->HitTest(x, y);
        if (state == 1 && w->IsHovered() && (w->m_flags & 8)) {
            w->m_clicked = 1;
            PlayClick();
            return true;
        }
    } else if (state == 3)
        w->m_hovered = 0;
    return false;
}
void RadioOverlay(Radio* w, gui_canvas::Canvas& canvas)
{
    if (w->m_text) gui_controls::TextRect(*w, w->m_font, 0xffffffff, Lookup(w->m_text), canvas);
}
Node* AllocateNode()
{
    return ControlEnv().AllocateNode();
}
void FreeNode(Node* p)
{
    ::operator delete(p);
}
void GroupInit(Group* w, const char* key, const char* graphic)
{
    ControlEnv env;
    gui_controls::ConstructRadioGroup(*w, key, graphic, env);
}
void GroupDestruct(Group* w)
{
    auto* n = w->m_children->m_next;
    while (n != w->m_children) {
        auto* next = n->m_next;
        FreeNode(n);
        n = next;
    }
    w->m_children->m_next = w->m_children->m_prev = w->m_children;
    Delete(w->m_variable);
    if (w->m_instance) Release(Bank(), nullptr, w->m_instance);
    FreeNode(w->m_children);
}
void GroupAdd(Group* w, Base* child)
{
    auto* n = AllocateNode();
    n->m_widget = child;
    n->m_next = w->m_children;
    n->m_prev = w->m_children->m_prev;
    n->m_prev->m_next = n;
    w->m_children->m_prev = n;
}
template<class F> void Children(Group* w, F callback)
{
    for (auto* n = w->m_children->m_next; n != w->m_children; n = n->m_next) callback(n->m_widget);
}
void GroupInvalidate(Group* w)
{
    w->InvalidateBase();
    Children(w, [](Base* c) { c->Invalidate(); });
}
void GroupOrigin(Group* w, int dx, int dy)
{
    w->SetOriginBase(dx, dy);
    Children(w, [&](Base* c) { c->SetOrigin(dx, dy); });
}
void GroupTranslate(Group* w, int dx, int dy)
{
    w->TranslateBase(dx, dy);
    Children(w, [&](Base* c) { c->Translate(dx, dy); });
}
void GroupDraw(Group* w, gui_canvas::Canvas& canvas, gui_canvas::SpriteId sprite)
{
    if (w->m_dirty) {
        if (w->m_instance) canvas.DrawClippedAt(sprite, w->m_x0, w->m_y0, 0, 99999.0f);
        w->m_dirty = 0;
    }
    Children(w, [](Base* c) { c->Draw(); });
}
struct GroupTreeEnv {
    bool OnMouseLeft(Base& w, int x, int y, int state) { return w.MouseLeft(x, y, state) != 0; }
    void DrawOverlay(Base& w) { w.DrawOverlay(); }
};
void GroupUpdate(Group* w, float dt)
{
    void* provider = Provider();
    const int selected = ReadInt(provider, w->m_variable);
    Children(w, [&](Base* c) {
        auto* radio = static_cast<Radio*>(c);
        const uint32_t flags = gui_controls::RadioSelectFlags(radio->m_flags, radio->m_value == selected);
        if (flags != radio->m_flags) {
            radio->m_flags = flags;
            c->Invalidate();
        }
    });
    Children(w, [&](Base* c) {
        auto* radio = static_cast<Radio*>(c);
        char key[128];
        std::sprintf(key, "%s_STATUS_%d", w->m_variable, radio->m_value);
        const uint32_t flags = gui_controls::RadioDisabledFlags(radio->m_flags, !ReadInt(provider, key));
        if (flags != radio->m_flags) {
            radio->m_flags = flags;
            c->Invalidate();
        }
    });
    Children(w, [&](Base* c) { c->Update(dt); });
    Children(w, [&](Base* c) {
        if (c->WasClicked()) {
            c->m_clicked = 0;
            Write(provider, w->m_variable, static_cast<Radio*>(c)->m_value);
        }
    });
}

// ---- entry points ----

void LiveButtonDraw(Button* w)
{
    LiveCanvas c;
    ButtonDraw(w, c, c.BorrowSprite(w->m_instance));
}
void LiveCheckDraw(CheckBox* w)
{
    if (!w->m_dirty) return;
    LiveCanvas c;
    gui_controls::DrawCheckBox(*w, c.BorrowSprite(w->m_instance), Animation(w->m_instance)->m_directions, c);
}
void LiveRadioDraw(Radio* w)
{
    LiveCanvas c;
    gui_controls::DrawRadio(*w, c.BorrowSprite(w->m_instance), c);
}
void LiveGroupDraw(Group* w)
{
    LiveCanvas c;
    GroupDraw(w, c, c.BorrowSprite(w->m_instance));
}
void LiveRadioOverlay(Radio* w)
{
    LiveCanvas c;
    RadioOverlay(w, c);
}
} // namespace

namespace {
#include "game/ui/gui_controls_range.inl"
#include "game/ui/gui_controls_check.inl"

} // namespace

namespace slider_facade {
ui::GuiSlider* Construct(
        ui::GuiSlider* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* variable, float param0, float param1, int32_t frame)
{
    g_counter.Count();
    return new (static_cast<void*>(w))
            ui::GuiSlider(x0, y0, x1, y1, variable, std::bit_cast<uint32_t>(param0), std::bit_cast<uint32_t>(param1), frame);
}
} // namespace slider_facade

namespace scrollbar_facade {
ui::GuiScrollBar* Construct(
        ui::GuiScrollBar* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t top, uint32_t count, uint32_t rows, uint32_t id)
{
    g_counter.Count();
    return new (static_cast<void*>(w)) ui::GuiScrollBar(x0, y0, x1, y1, top, count, rows, id);
}
void SetItemCount(ui::GuiScrollBar* w, uint32_t count)
{
    g_counter.Count();
    RunScrollSetCount(w, count);
}
void SetTopRow(ui::GuiScrollBar* w, uint32_t top)
{
    g_counter.Count();
    ScrollTopRow(w, top);
}
} // namespace scrollbar_facade

namespace number_adjust_facade {
ui::GuiNumberAdjust* Construct(ui::GuiNumberAdjust* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t kind, int32_t font,
        const char* variable, uint8_t wrap, uint8_t tips)
{
    g_counter.Count();
    return new (static_cast<void*>(w)) ui::GuiNumberAdjust(x0, y0, x1, y1, kind, font, variable, wrap, tips);
}
} // namespace number_adjust_facade

namespace check_box_facade {
ui::GuiCheckBox* Construct(ui::GuiCheckBox* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* graphic, const char* key)
{
    g_counter.Count();
    return new (static_cast<void*>(w)) ui::GuiCheckBox(x0, y0, x1, y1, graphic, key);
}
} // namespace check_box_facade

namespace radio_button_facade {
ui::GuiRadioButton* Construct(ui::GuiRadioButton* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* graphic, const char* text,
        int32_t font, int32_t value)
{
    g_counter.Count();
    return new (static_cast<void*>(w)) ui::GuiRadioButton(x0, y0, x1, y1, graphic, text, font, value);
}
} // namespace radio_button_facade

namespace radio_group_facade {
ui::GuiRadioGroup* Construct(ui::GuiRadioGroup* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* key, const char* graphic)
{
    g_counter.Count();
    return new (static_cast<void*>(w)) ui::GuiRadioGroup(x0, y0, x1, y1, key, graphic);
}
} // namespace radio_group_facade

namespace button_action_facade {
void DisableIfAction(ui::GuiButton* w, const char* name)
{
    g_counter.Count();
    ButtonDisable(w, name);
}
void ResetStateByAction(ui::GuiButton* w, const char* name)
{
    g_counter.Count();
    ButtonReset(w, name);
}
void SetBlinkIfAction(ui::GuiButton* w, const char* name, uint32_t value)
{
    g_counter.Count();
    ButtonBlink(w, name, value);
}
void SetFlashingByAction(ui::GuiButton* w, const char* name, uint32_t value)
{
    g_counter.Count();
    ButtonFlashing(w, name, value);
}
void SetVisibleIfAction(ui::GuiButton* w, const char* name, uint32_t value)
{
    g_counter.Count();
    ButtonVisibleAction(w, name, value);
}
void SetGraphicByAction(ui::GuiButton* w, const char* name, const char* graphic)
{
    g_counter.Count();
    ButtonGraphic(w, name, graphic);
}
} // namespace button_action_facade

namespace button_facade {
ui::GuiButton* Construct(ui::GuiButton* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* graphic, const char* text,
        const char* left, const char* right)
{
    g_counter.Count();
    return new (static_cast<void*>(w)) ui::GuiButton(x0, y0, x1, y1, graphic, text, left, right);
}
} // namespace button_facade

bool RegisterGuiControls()
{
    g_wageCommand.SetDriver(DriveCommandControl<0>);
    g_priceCommand.SetDriver(DriveCommandControl<1>);
    g_eventPriceCommand.SetDriver(DriveCommandControl<2>);
    g_toggleCommand.SetDriver(DriveCommandControl<3>);
    g_variableCommand.SetDriver(DriveCommandControl<4>);
    g_intervalCommand.SetDriver(DriveCommandControl<5>);
    g_speedCommand.SetDriver(DriveCommandSpeed);
    g_weatherSlider.SetDriver(DriveCommandControl<6>);
    g_volumeMusic.SetDriver(&InspectVolume<true>);
    g_volumeEffects.SetDriver(&InspectVolume<false>);
    g_seaOn.SetDriver(&InspectSettings<false, true>);
    g_seaOff.SetDriver(&InspectSettings<false, false>);
    g_listOn.SetDriver(&InspectSettings<true, true>);
    g_listOff.SetDriver(&InspectSettings<true, false>);
    g_musicPanel.SetDriver(&OpenMusicPanel);
    return true;
}
} // namespace obl::gui

namespace obl::ui {
using namespace gui;

GuiButton::GuiButton(int x0, int y0, int x1, int y1, const char* graphic, const char* text, const char* left, const char* right)
    : GuiLeaf(x0, y0, x1, y1)
{
    ButtonInit(this, graphic, text, left, right);
}
GuiButton::~GuiButton()
{
    g_counter.Count();
    ButtonDestruct(this);
}
void GuiButton::Update(float dt)
{
    g_counter.Count();
    ButtonUpdate(this, dt);
}
void GuiButton::Draw()
{
    g_counter.Count();
    LiveButtonDraw(this);
}
bool GuiButton::HitTest(int x, int y)
{
    g_counter.Count();
    return ButtonHit(this, x, y);
}
bool GuiButton::MouseLeft(int x, int y, int state)
{
    g_counter.Count();
    return RunButtonMouse(this, x, y, state);
}
bool GuiButton::MouseRight(int x, int y, int state)
{
    g_counter.Count();
    return ButtonRight(this, x, y, state);
}
void GuiButton::SetVisible(uint32_t visible)
{
    g_counter.Count();
    ButtonVisible(this, visible);
}

GuiCheckBox::GuiCheckBox(int x0, int y0, int x1, int y1, const char* graphic, const char* key) : GuiLeaf(x0, y0, x1, y1)
{
    CheckInit(this, graphic, key);
}
GuiCheckBox::~GuiCheckBox()
{
    g_counter.Count();
    CheckDestruct(this);
}
void GuiCheckBox::Update(float dt)
{
    g_counter.Count();
    CheckUpdate(this, dt);
}
void GuiCheckBox::Draw()
{
    g_counter.Count();
    LiveCheckDraw(this);
}
bool GuiCheckBox::HitTest(int x, int y)
{
    g_counter.Count();
    return SpriteHit(this, m_instance, x, y);
}
bool GuiCheckBox::MouseLeft(int x, int y, int state)
{
    g_counter.Count();
    return CheckMouse(this, x, y, state);
}

GuiRadioButton::GuiRadioButton(int x0, int y0, int x1, int y1, const char* graphic, const char* text, int font, int value)
    : GuiLeaf(x0, y0, x1, y1)
{
    RadioInit(this, graphic, text, font, value);
}
GuiRadioButton::~GuiRadioButton()
{
    g_counter.Count();
    RadioDestruct(this);
}
void GuiRadioButton::Update(float dt)
{
    g_counter.Count();
    RadioUpdate(this, dt);
}
void GuiRadioButton::Draw()
{
    g_counter.Count();
    LiveRadioDraw(this);
}
void GuiRadioButton::DrawOverlay()
{
    g_counter.Count();
    LiveRadioOverlay(this);
}
bool GuiRadioButton::HitTest(int x, int y)
{
    g_counter.Count();
    return SpriteHit(this, m_instance, x, y);
}
bool GuiRadioButton::MouseLeft(int x, int y, int state)
{
    g_counter.Count();
    return RadioMouse(this, x, y, state);
}

GuiRadioGroup::GuiRadioGroup(int x0, int y0, int x1, int y1, const char* key, const char* graphic) : GuiLeaf(x0, y0, x1, y1)
{
    GroupInit(this, key, graphic);
}
GuiRadioGroup::~GuiRadioGroup()
{
    g_counter.Count();
    if (m_children) GroupDestruct(this); // a default-constructed record owns nothing
}
void GuiRadioGroup::Update(float dt)
{
    g_counter.Count();
    GroupUpdate(this, dt);
}
void GuiRadioGroup::Draw()
{
    g_counter.Count();
    LiveGroupDraw(this);
}
void GuiRadioGroup::DrawOverlay()
{
    g_counter.Count();
    GroupTreeEnv env;
    gui_widgets::ContainerDrawOverlay(*this, env);
}
bool GuiRadioGroup::MouseLeft(int x, int y, int state)
{
    g_counter.Count();
    GroupTreeEnv env;
    return gui_widgets::ContainerOnMouseLeft(*this, x, y, state, env);
}
void GuiRadioGroup::Invalidate()
{
    g_counter.Count();
    GroupInvalidate(this);
}
void GuiRadioGroup::SetOrigin(int dx, int dy)
{
    g_counter.Count();
    GroupOrigin(this, dx, dy);
}
void GuiRadioGroup::Translate(int dx, int dy)
{
    g_counter.Count();
    GroupTranslate(this, dx, dy);
}
void GuiRadioGroup::AddChild(GuiWidget* child)
{
    g_counter.Count();
    GroupAdd(this, child);
}

GuiScrollBar::GuiScrollBar(int x0, int y0, int x1, int y1, uint32_t top, uint32_t count, uint32_t rows, uint32_t id)
    : GuiLeaf(x0, y0, x1, y1)
{
    ScrollInit(this, top, count, rows, id);
}
GuiScrollBar::~GuiScrollBar()
{
    g_counter.Count();
    if (m_up) ScrollDestruct(this); // a default-constructed record owns nothing
}
void GuiScrollBar::SetVisible(uint32_t visible)
{
    g_counter.Count();
    ScrollVisible(this, visible);
}
bool GuiScrollBar::MouseWheel(int x, int y, int delta)
{
    g_counter.Count();
    return ScrollWheel(this, x, y, delta);
}
bool GuiScrollBar::MouseLeft(int x, int y, int state)
{
    g_counter.Count();
    return ScrollMouse(this, x, y, state);
}
void GuiScrollBar::Update(float dt)
{
    g_counter.Count();
    ScrollUpdate(this, dt);
}
void GuiScrollBar::Translate(int dx, int dy)
{
    g_counter.Count();
    ScrollTranslate(this, dx, dy);
}
void GuiScrollBar::DrawOverlay()
{
    g_counter.Count();
    ScrollOverlay(this);
}

GuiSlider::GuiSlider(int x0, int y0, int x1, int y1, const char* variable, uint32_t param0, uint32_t param1, int frame)
    : GuiLeaf(x0, y0, x1, y1)
{
    SliderInit(this, variable, param0, param1, frame);
}
GuiSlider::~GuiSlider()
{
    g_counter.Count();
    SliderDestruct(this);
}
bool GuiSlider::MouseLeft(int x, int y, int state)
{
    g_counter.Count();
    return SliderMouse(this, x, y, state);
}
void GuiSlider::SetOrigin(int dx, int dy)
{
    g_counter.Count();
    SliderOrigin(this, dx, dy);
}
void GuiSlider::Update(float dt)
{
    g_counter.Count();
    SliderUpdate(this, dt);
}
void GuiSlider::DrawOverlay()
{
    g_counter.Count();
    SliderOverlay(this);
}
void GuiSlider::Translate(int dx, int dy)
{
    g_counter.Count();
    SliderTranslate(this, dx, dy);
}

GuiNumberAdjust::GuiNumberAdjust(int x0, int y0, int x1, int y1, int kind, int font, const char* variable, uint8_t wrap, uint8_t tips)
    : GuiLeaf(x0, y0, x1, y1)
{
    AdjustInit(this, kind, font, variable, wrap, tips);
}
GuiNumberAdjust::~GuiNumberAdjust()
{
    g_counter.Count();
    AdjustDestruct(this);
}
void GuiNumberAdjust::SetTooltip(const char* text)
{
    g_counter.Count();
    AdjustTooltip(this, text);
}
void GuiNumberAdjust::SetVisible(uint32_t visible)
{
    g_counter.Count();
    AdjustVisible(this, visible);
}
void GuiNumberAdjust::Update(float dt)
{
    g_counter.Count();
    AdjustUpdate(this, dt);
}
bool GuiNumberAdjust::MouseLeft(int x, int y, int state)
{
    g_counter.Count();
    return AdjustMouse(this, x, y, state);
}
void GuiNumberAdjust::Draw()
{
    g_counter.Count();
    AdjustDraw(this);
}
void GuiNumberAdjust::DrawOverlay()
{
    g_counter.Count();
    LiveAdjustOverlay(this);
}
void GuiNumberAdjust::Invalidate()
{
    g_counter.Count();
    AdjustInvalidate(this);
}
void GuiNumberAdjust::Translate(int dx, int dy)
{
    g_counter.Count();
    AdjustTranslate(this, dx, dy);
}
void GuiNumberAdjust::SetOrigin(int dx, int dy)
{
    g_counter.Count();
    RunAdjustOrigin(this, dx, dy);
}
} // namespace obl::ui
