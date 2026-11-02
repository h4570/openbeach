#include "core/pointer_width.h"
#include "game/ui/gui_widgets_leaves.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/renderer_api.h"
#include "game/ui/sound_api.h"
#include "game/game/game_api.h"
#include "game/game/settings_store.h"
#include "game/objects/object_base.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/input_types.h"
#include "game/game/world_test_state.h"
#include "game/ui/gui_layouts_runtime.h"
#include "game/ui/media_files.h"
#include "game/game/game_states.h"
#include "game/ui/gui_content_runtime.h"
#include "game/ui/sound_types.h"
#include "game/ui/media_pack.h"
#include "core/pack_path.h"
#include "core/call_counter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Live effects for the six portable state templates; tests/game_states_live.txt covers their game integration.
namespace obl::game {
namespace {

// Call counters (`call-report <name>`).
test::CallCounter g_calls("GameStates.calls");

// ---- the live Env ----

void* Gui()
{
    return GetGuiManager();
}
audio::Sound* SoundObject()
{
    return audio::CurrentSound();
}
void CloseSoundStream()
{
    SoundObject()->CloseStream();
}
int Language()
{
    return settings_store::Current().m_shownLanguage;
}

constexpr size_t kGuiTextBytes = sizeof(ui::GuiText);
static_assert(!kPointer32 || kGuiTextBytes == 0x5c);
// A GuiText widget of a GameWon page (game heap; deleted through its virtual destructor).
using Widget = void**;
using Widgets = std::vector<Widget>;

class LiveEnv {
public:
    using Widgets = game::Widgets;
    static void Count() { g_calls.Count(); }
    static const char* Script(const char* layout) { return layout ? gui::gui_layouts::FileFor(layout, Language()) : nullptr; }
    static auto CreditRows() { return gui::gui_content::CreditRows(); }
    static int CreditPageCount() { return game::CreditPageCount(CreditRows().size()); }
    void RenderPages()
    {
        ui::Renderer* renderer = GetRenderer();
        ui::renderer::SetActiveSurface(renderer, 0);
        renderer->m_renderVisibleObjects = 1;
        renderer->m_directMode = 1;
        for (Widget widget : *m_widgets) {
            (reinterpret_cast<ui::GuiWidget*>(widget))->Invalidate();
            (reinterpret_cast<ui::GuiWidget*>(widget))->Draw();
        }
        renderer->m_directMode = 0;
        renderer->m_renderVisibleObjects = 0;
    }
    // `widgets`: GameWon's page widgets, null for the other states.
    explicit LiveEnv(GameState& state, Widgets* widgets = nullptr) : m_state(state), m_widgets(widgets) {}

    float FrameStep() { return GetGameClock()->m_deltaTime; }
    int LeftButton() { return GetInput()->m_leftButton; }
    void SetNextState(game::GameStateId id) { game_api::SetNextState(static_cast<int>(id)); }
    void ShowDialog(const char* name) { static_cast<ui::GuiManager*>(Gui())->ShowDialog(name, nullptr, false); }
    void ChangeResolution(int width, int height) { game_api::ChangeResolution(width, height); }
    void LoadOwnGuiScript() { game_api::LoadGuiScript(m_state.GetGuiScript()); }
    void SetGuiMode(int mode) { GetGuiManager()->SetMode(mode); }
    int PlaySound(const char* name, int mode, float fade) { return SoundObject()->Play(name, static_cast<uint8_t>(mode), fade); }
    void SetChannelVolume(int channel, float volume) { SoundObject()->SetChannelVolume(channel, volume); }
    int Rand() { return game::Rand(); }
    int CursorX() { return GetInput()->m_x; }
    int CursorY() { return GetInput()->m_y; }
    bool ReturnToMainMenu() { return game_api::ReturnToMainMenu(); }
    void RequestReturnToMainMenu() { game_api::RequestReturnToMainMenu(); }
    void CancelReturnToMainMenu() { game_api::CancelReturnToMainMenu(); }
    void LoadScenarioProgress() { game_api::LoadScenarioProgress(); }
    void StopAllSamples() { SoundObject()->StopAllSamples(); }
    // The night stream is the only pack track opened here (kNightStream): it is opened and checked like the playlist's tracks.
    void PlayStream(const char* file)
    {
        const std::string path = media_pack::TrackPath(pack_path::Selected(), media_pack::kTrackDirectory, file);
        ui::media_files::RequireWindowedFile(path);
        SoundObject()->PlayStream(path.c_str());
    }
    void CloseStream() { CloseSoundStream(); }
    // The entries only: DestroyPageWidgets deleted the widgets before (Enter leaves the old page's widgets alive).
    void ClearPages() { m_widgets->clear(); }
    void ShowPage();
    void CloseAllDialogs() { GetGuiManager()->CloseAllDialogs(); }
    // Every entry is deleted through its widget (virtual destructor with the delete flag; null entries are skipped).
    void DestroyPageWidgets()
    {
        for (Widget widget : *m_widgets)
            if (widget) gui::DestroyWidget(*reinterpret_cast<ui::GuiWidget*>(widget));
    }
    void AddText(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color)
    {
        void* widget = ::operator new(kGuiTextBytes);
        if (widget) gui::TextConstruct(static_cast<ui::GuiText*>(widget), x0, y0, x1, y1, key, font, color, 4);
        m_widgets->push_back(static_cast<Widget>(widget));
    }

private:
    GameState& m_state;
    Widgets* m_widgets;
};

void LiveEnv::ShowPage()
{
    static_cast<GameWonState<LiveEnv>&>(m_state).ShowPage();
}

} // namespace

void CreateGameStates(game::Game& game)
{
    using Id = game::GameStateId;
    auto state = [&game](Id id) -> std::unique_ptr<GameState>& { return game.m_states[static_cast<size_t>(id)]; };
    state(Id::WorldTest) = std::make_unique<WorldTestState>();
    state(Id::MainMenu) = std::make_unique<MainMenuState<LiveEnv>>();
    state(Id::Intro) = std::make_unique<IntroState<LiveEnv>>();
    state(Id::ExitGame) = std::make_unique<ExitGameState<LiveEnv>>();
    state(Id::ResetGame) = std::make_unique<ResetGameState<LiveEnv>>();
    state(Id::LoadGame) = std::make_unique<LoadGameState<LiveEnv>>();
    state(Id::GameWon) = std::make_unique<GameWonState<LiveEnv>>();
}

} // namespace obl::game

namespace obl::object {
int Base::GetOverlayOffset()
{
    // The folded LoadScript entry returns null and counts the same call.
    game::g_calls.Count();
    return 0;
}
} // namespace obl::object
