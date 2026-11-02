#include "core/text16.h"
#include "core/pointer_width.h"
#include "game/ui/day_night_present_runtime.h"
#include "game/game/startup_tables.h"
#include "game/ui/gui_tooltip.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_modes_types.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/gui_radar.h"
#include "game/ui/palette_modifiers.h"
#include "game/ui/music_player.h"
#include "game/ui/renderer_api.h"
#include "game/ui/cursor.h"
#include "game/game/diagnostics.h"
#include "game/game/game_host.h"
#include "game/ui/image.h"
#include "game/ui/input.h"
#include "core/key_code.h"
#include "game/ui/sound_api.h"
#include "game/ui/loop_sound_control.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/media_files.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/save_slots.h"
#include "game/ui/font_construct.h"
#include "game/ui/font.h"

#include "game/game/game_accessors.h"
#include "game/game/settings_store.h"
#include "game/game/game_api.h"
#include "game/game/command_loop.h"
#include "core/config.h"
#include "game/game/game_core.h"
#include "game/game/game_parameters_construct.h"
#include "game/ui/media_pack.h"
#include "core/pack_path.h"
#include "core/rect.h"
#include "core/log.h"
#include "game/game/progress_file.h"
#include "game/ui/sound_types.h"
#include "game/game/game_parameters.h"
#include "game/game/game_states.h"
#include "game/game/world_test_state.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/gui_controls_types.h"
#include "game/ui/input_types.h"
#include "game/ui/sprite.h"
#include "game/ui/cursor_types.h"
#include "core/os.h"
#include "core/call_counter.h"
#include "game/tables/text_load.h"
#include "game/world/world_control_logic.h"

#include <intrin.h>
#include <algorithm>
#include <optional>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "game/game/game_logic.h"
#include "game/ui/text_table.h"

// The host's module handle, opaque here (game_api::Create takes it as void*).
struct HINSTANCE__;

namespace obl::game {
namespace {
using Game = game::Game;
// Calls of the Game's operations (`call-report GameCoreCalls`).
test::CallCounter g_calls("GameCoreCalls");

// The file and campaign side of the progress rules (game_logic.h).
struct ProgressEnv {
    bool LoadCampaign(scenario_load::Campaign& campaign) const
    {
        LoadCampaignOrFatal(campaign);
        return true;
    }
    // Reads a whole file of the game directory; false when it is absent.
    bool ReadFile(const char* name, std::string& out) const
    {
        FILE* file = os::OpenFile(name, "rb");
        if (!file) return false;
        char buffer[4096];
        out.clear();
        for (size_t got; (got = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) out.append(buffer, got);
        std::fclose(file);
        return true;
    }
    void WriteFile(const char* name, const std::string& text) const
    {
        if (FILE* file = os::OpenFile(name, "wb")) {
            std::fwrite(text.data(), 1, text.size(), file);
            std::fclose(file);
        }
    }
};
void Reset(Game* game)
{
    ProgressEnv env;
    game_logic::ResetProgress(*game, env);
}
int32_t CampaignCount()
{
    ProgressEnv env;
    return static_cast<int32_t>(game_logic::Campaign(env).campaign.size());
}
void Load(Game* game)
{
    ProgressEnv env;
    game_logic::LoadProgress(*game, env);
}
void Save(Game* game)
{
    ProgressEnv env;
    game_logic::SaveProgress(*game, env);
}
void Complete(Game* game)
{
    ProgressEnv env;
    game_logic::CompleteScenario(*game, env);
}
void Sandbox(Game* game, uint8_t sandbox)
{
    ProgressEnv env;
    game_logic::SetSandbox(*game, sandbox, env);
}
void Scan(Game* game)
{
    scenario_load::Campaign campaign;
    LoadCampaignOrFatal(campaign);
    game_logic::ScanScenarios(*game, campaign);
}
uint8_t ReturnToMenu(Game* game)
{
    return game->m_returnToMainMenu;
}
void ClearReturn(Game* game)
{
    game->m_returnToMainMenu = 0;
}
void SetReturn(Game* game, uint8_t value)
{
    game->m_returnToMainMenu = value;
}
int Count(Game* game)
{
    return static_cast<int>(game->m_scenarios.size());
}
const char16_t* Scenario(Game* game, int index)
{
    return game->m_scenarios[index].c_str();
}
uint8_t Always(Game* game, int index)
{
    return (game->m_scenarioProgress[index] & 4) != 0;
}
uint8_t Completed(Game* game, int index)
{
    return (game->m_scenarioProgress[index] & 8) != 0;
}
void NextState(Game* game, int state)
{
    if (static_cast<unsigned>(state) >= static_cast<unsigned>(game::GameStateId::Count)) return;
    game->m_nextState = game->m_states[state].get();
    if (state == 0) static_cast<WorldTestState*>(game->m_nextState)->m_loadingSave = 0;
}
// The activation flag is set at the end of the Game's construction, so early focus messages are ignored.
bool Constructed()
{
    return GetContext().gameConstructed;
}
void Deactivate(Game* game)
{
    if (!Constructed() || game->m_suspended) return;
    if (auto* world = GetWorld()) world->m_paused = 1;
    audio::CurrentSound()->PauseStream();
    audio::CurrentSound()->Mute();
    game->m_suspended = 1;
}
void Activate(Game* game)
{
    if (!Constructed() || !game->m_suspended) return;
    if (GetWorld()) game->m_guiManager.get()->ShowPauseUnlessScenarioEnded();
    audio::CurrentSound()->ResumeStream();
    audio::CurrentSound()->Unmute();
    InputClearButtonState(*game->m_input);
    InputRestoreCursorShape(*game->m_input);
    game->m_suspended = 0;
}
void PaletteModifiersClear()
{
    ui::SetPaletteModifiers(nullptr);
}
float Version()
{
    return 1.1f;
}

ui::GuiListBox* List(Game* game, const char* name)
{
    return static_cast<ui::GuiListBox*>(game->m_guiManager.get()->FindListBox(name));
}
// The GUI, world and music side of the variable table (game_logic.h).
struct VariableEnv {
    Game* game;
    game::Settings& Settings() const { return *game->m_settings; }
    bool StaffSelected() const { return game->m_guiManager->m_context[12] != -1; }
    size_t Selected(const char* name) const
    {
        const auto* list = List(game, name);
        return list->m_selBegin ? static_cast<size_t>(list->m_selEnd - list->m_selBegin) : 0;
    }
    int FirstSelected(const char* name) const
    {
        auto* list = List(game, name);
        list->m_selCursor = 0;
        return *list->m_selBegin;
    }
    std::optional<int> GameSpeed() const
    {
        if (auto* world = GetWorld()) return world->m_gameSpeedLevel;
        return std::nullopt;
    }
    static void* MusicPlayer() { return GetGuiManager()->m_musicPlayer; }
    int PlaylistManual() const { return static_cast<audio::MusicPlayer*>(MusicPlayer())->manual; }
    void SetSeaEffects(uint8_t enabled) const { ui::renderer::SetSeaEffects(GetRenderer(), enabled); }
    void SetKeyline(uint8_t enabled) const { object::object_manager::SetKeyline(GetObjectManager(), enabled); }
    void SetGameSpeed(int level) const { SubmitCommand(command::SetGameSpeed{level}); }
    void PlaylistCommand(int command) const { static_cast<audio::MusicPlayer*>(MusicPlayer())->Command(command); }
};
void GetVariable(Game* game, const char* name, Char16* out)
{
    struct Env : VariableEnv {
        void Unrecognized(const char* key) const { FatalError("A script read unrecognized game variable \"%s\"", key); }
    } env{{game}};
    // Values are decimal integers; the buffer holds 12 characters.
    if (const std::optional<int> value = game_logic::GetVariable(*game, name, env)) text16::Format(out, 12, u"%d", *value);
}
void SetVariable(Game* game, const char* name, const int* input)
{
    struct Env : VariableEnv {
        void Unrecognized(const char*) const { FatalError("A script set an unrecognized game variable"); }
    } env{{game}};
    game_logic::SetVariable(*game, name, *input, env);
}

// ---- sprite bank lifetime ---------------------------------------------------------------------------------------------------------------
using ui::SpriteBank;

void FatalErrorWith(const char* format, const char* argument)
{
    FatalError(format, argument);
}

// The bank object's constructor on its fresh block. The loader reads the converted sprites (SpriteBank::Load, sprite_bank.cpp) and fails
// only with a message box.
SpriteBank* LoadBank(SpriteBank* bank)
{
    ui::SetSpriteBank(bank);
    // Every name starts empty.
    for (auto& buffer : bank->m_names) buffer[0] = 0;
    bank->m_spriteCount = 0;
    bank->m_headerFile = nullptr;
    bank->m_datFile = -1;
    if ((static_cast<ui::SpriteBank*>(bank))->Load() != 0) {
        ShowNotice(nullptr, "Unable to start the sprite renderer.", "OpenBeach", false);
    }
    void* modifiers = ::operator new(0x680);
    if (modifiers) modifiers = ui::ConstructPaletteModifiers(modifiers);
    bank->m_paletteModifiers = modifiers;
    return bank;
}

void UnloadBank(SpriteBank* bank)
{
    // Converted sprite banks keep no header stream or descriptor open.
    for (int i = 0; i < bank->m_spriteCount; ++i) {
        ui::Sprite* sprite = bank->m_sprites[i];
        if (sprite->m_refCount != 0) {
            FatalErrorWith("Sprite bank shutdown found an active reference: %s", sprite->m_name);
        } else if (sprite) {
            (sprite)->Destruct();
            ::operator delete(sprite);
        }
    }
    delete[] bank->m_sprites;
    if (void* modifiers = bank->m_paletteModifiers) {
        PaletteModifiersClear();
        ::operator delete(modifiers);
    }
    ui::SetSpriteBank(nullptr);
}

// ---- Game destruction -------------------------------------------------------------------------------------------------------------------
// Members end in a fixed order. Each is a separate game object with its own destructor, so only the
// Runs the deleter while the member still points at the object, then empties it: GetRenderer()/GetInput()/GetCursor() still
// answer during the object's own destructor, as the image globals they replaced did until the destructor cleared them.
// The same for an object whose destructor is a plain function.
template<class T, void (*Destructor)(T*)> void DestroyBlock(T* object)
{
    Destructor(object);
    ::operator delete(object);
}
void DestructGuiManager(ui::GuiManager* manager)
{
    manager->Destruct();
}
void DestructSound(audio::Sound* sound)
{
    sound->Destruct();
}

template<class T, void (*Destructor)(T*)> game::Member<T> Owned(void* object)
{
    return game::Member<T>(static_cast<T*>(object), {&DestroyBlock<T, Destructor>});
}
// The same for a record with std:: members (`new T{}`): its destructor runs after the destruct function.
template<class T, void (*Destructor)(T*)> void DestroyNew(T* object)
{
    Destructor(object);
    delete object;
}
template<class T> void End(game::Member<T>& member)
{
    if (T* object = member.get()) {
        member.get_deleter()(object);
        member.release();
    }
}

void Destruct(Game* game)
{
    Save(game);
    // The final save, then the settings object.
    settings_store::Save(*game->m_settings);
    End(game->m_settings);
    End(game->m_textTable);
    for (auto& state : game->m_states) state.reset();
    End(game->m_guiManager);
    End(game->m_sound);
    End(game->m_loopSounds);
    End(game->m_debugFont);
    End(game->m_cursor);
    End(game->m_input);
    End(game->m_renderer);
    End(game->m_spriteBank);
    // The object itself (scenario names, save-slot records) ends with the context's unique_ptr (game_api::Destroy).
}

// ---- the fullscreen and resolution helpers of the Game ----------------------------------------------------------------------------------
// The picture at the origin, 800x600 source rectangle, not stretched.
void DrawFullscreenTga(Game*, const char* path)
{
    void* image = ui::images::LoadTga(path);
    ui::images::BuildPalette16(image, GetRenderer()->m_rgb555 == 1);
    int position[2] = {0, 0};
    int rect[4] = {0, 0, 800, 600};
    ui::renderer::DrawImage(GetRenderer(), position, rect, image, 0, 0, 0);
    if (image) {
        ui::images::Destruct(image);
        ::operator delete(image);
    }
}

// The GuiManager loads the script, the answer is always true.
uint8_t LoadGuiScript(Game* game, const char* script)
{
    game->m_guiManager.get()->LoadScript(script);
    return 1;
}

// The localized ERROR_DIRECTX box; ShowErrorAndExit does not return.
void ShowDirectXError(Game*)
{
    const void* text = ui::text_table::Lookup("ERROR_DIRECTX");
    ShowErrorAndExit(GetContext().errorWindow, static_cast<const Char16*>(text));
}

// Only a real change reaches the renderer, the input and the current state.
void ChangeResolution(Game* game, int width, int height)
{
    ui::Renderer* renderer = GetRenderer();
    if (renderer->m_width == width && renderer->m_height == height) return;
    if (!ui::renderer::ChangeResolution(renderer, width, height)) ShowDirectXError(game);
    InputSetScreenSize(*GetInput(), width, height);
    game->m_currentState->ChangeResolution(width, height);
}

// ---- Game construction and its small helpers --------------------------------------------------------------------------------------------
// The window later error boxes belong to (the Game keeps no window handle of its own).
void SetErrorWindow(void* window)
{
    GetContext().errorWindow = window;
}

// The debug font's first word is its image slot (the slot released, then the block itself).
void ReleaseFontImage(void* font)
{
    ui::images::ReleaseSlot(static_cast<void**>(font));
}
void DestructInput(ui::Input* input)
{
    InputDestruct(*input);
}

// A game heap block plus its constructor; a null block leaves the member null.
template<class Constructor> void* Create(size_t bytes, Constructor construct)
{
    void* block = ::operator new(bytes);
    return block ? construct(block) : nullptr;
}
// A plain file of the same name keeps the directory from being created.
void EnsureDirectory(const char* name)
{
    os::CreateDirectoryIfMissing(name);
}

// Builds every member of the context's fresh Game (game_api::Create) in a fixed order: the members' constructors read the ones
// built before them.
game::GameClock* ClockConstruct(game::GameClock* clock, uint32_t tick);
game::ProfileTimer* TimerConstruct(game::ProfileTimer* timer);
void RunClockUpdate(game::GameClock* clock, uint32_t tick);

// The members before the intro movie; true when the movie section runs (the host then calls IntroMovieFrame until it
// returns false, and ConstructAfterMovie closes it).
bool ConstructUntilMovie(Game* game, void* window)
{
    game->m_suspended = 0;
    game->m_guiReloadPending = 0;
    game->m_loadSlot = -1;
    game->m_loadListFilled = 0;
    game->m_sandbox = 0;
    game->m_selectedSaveSlot = nullptr;
    game->m_settings = game::Member<Settings>(settings_store::Create().release(), {[](Settings* object) { delete object; }});
    Settings* settings = game->m_settings.get();
    auto language = static_cast<unsigned>(settings->m_language);
    static text_load::LanguageSelection languageSelection;
    std::string languageNotice;
    std::string languageError;
    if (!languageSelection.Select(
                pack_path::Selected(), language, languageNotice, languageError, text_load::PackLanguage(pack_path::Selected()))) {
        FatalError("%s", languageError.c_str());
        std::abort();
    }
    // One language on screen: the layouts and images follow the texts. The INI keeps the player's choice, which another
    // pack may have.
    settings->m_shownLanguage = static_cast<int>(language);
    if (!languageNotice.empty()) {
        OBL_LOG("Texts: %s", languageNotice.c_str());
        // Only for a player: headless and scripted test runs (the L0 pack has only English) would stop on the desktop.
        if (!config::Get().headless && !os::HasEnv("OBL_SCRIPT")) ShowNotice(window, languageNotice.c_str(), "OpenBeach", true);
    }
    game->m_textTable = game::Member<text::Table>(ui::text_table::Create(text_load::LanguageCode(language)), {&ui::text_table::Destroy});
    SetErrorWindow(window);

    const auto* parameters = game_parameters::Current();
    uint8_t playMovie = 0; // stays off when the key is missing
    game_parameters::GetBool(parameters, &playMovie, "bPLAY_MOVIE");
    if (playMovie) {
        // The system cursor is hidden while the movie plays (the local cursor, cursor.cpp, answers both calls).
        ui::InputHideSystemCursor();
        // The converted intro (media_pack.h): a missing file stops the windowed game, as the other pack files do.
        const std::string introMovie = media_pack::IntroPath(pack_path::Selected());
        ui::media_files::RequireWindowedFile(introMovie);
        platform::IntroMovieStart(window, introMovie.c_str());
    }
    return playMovie != 0;
}

Game* ConstructAfterMovie(Game* game, HINSTANCE__* instance, void* window, bool movie)
{
    if (movie) {
        platform::IntroMovieFinish();
        ui::InputShowSystemCursor();
    }
    const auto* parameters = game_parameters::Current();
    game->m_renderer = game::Member<ui::Renderer>(
            ui::ConstructRenderer(new ui::Renderer{}, window, 800, 600, false), {&DestroyNew<ui::Renderer, &ui::DestructRenderer>});
    if (!game->m_renderer->m_displayOk) ShowDirectXError(game);

    const char* fontName = nullptr;
    int fontSize = 0;
    game_parameters::GetString(parameters, &fontName, "sDEBUGFONT_NAME");
    game_parameters::GetInt(parameters, &fontSize, "iDEBUGFONT_SIZE");
    game->m_debugFont = Owned<void, &ReleaseFontImage>(
            Create(sizeof(ui::GdiFont), [&](void* block) { return ui::ConstructGdiFont(block, fontName, fontSize, 0u); }));

    ui::Renderer* renderer = GetRenderer();
    game->m_input = Owned<ui::Input, &DestructInput>(Create(sizeof(ui::Input), [&](void* block) {
        return static_cast<void*>(InputConstruct(
                *static_cast<ui::Input*>(block), renderer->m_width, renderer->m_height, static_cast<bool>(renderer->m_windowed)));
    }));
    ui::Input* input = game->m_input.get();
    if (!InputInit(*input, instance, window)) ShowDirectXError(game);
    if (!game->m_settings->m_systemCursor) InputSetSystemCursorEnabled(*GetInput(), 0);

    game->m_cursor = Owned<ui::SoftwareCursor, &ui::CursorDestruct>(Create(sizeof(ui::SoftwareCursor), [&](void* block) {
        return static_cast<void*>(CursorConstruct(static_cast<ui::SoftwareCursor*>(block), "data/cursor.tga", static_cast<void*>(window)));
    }));
    ClockConstruct(&game->m_clock, os::TickMs());
    TimerConstruct(&game->m_profileTimer);
    game->m_spriteBank = Owned<SpriteBank, &UnloadBank>(
            Create(sizeof(SpriteBank), [](void* block) { return static_cast<void*>(LoadBank(static_cast<SpriteBank*>(block))); }));

    uint8_t soundEnabled = 0; // stays off when the key is missing
    game_parameters::GetBool(parameters, &soundEnabled, "bSOUND_ENABLED");
    if (config::Get().headless) soundEnabled = 0; // no audio device: Sound and LoopSounds are built disabled
    game->m_sound = game::Member<audio::Sound>((new audio::Sound{})->Construct(soundEnabled), {&DestroyNew<audio::Sound, &DestructSound>});
    game->m_sound.get()->SetMusicVolume(game->m_settings->m_musicVolume);
    game->m_sound.get()->SetEffectsVolume(game->m_settings->m_effectsVolume);
    game->m_loopSounds = game::Member<audio::LoopSounds>(
            (new audio::LoopSounds{})->Construct(static_cast<bool>(soundEnabled)), {[](audio::LoopSounds* sounds) {
                sounds->Destruct();
                delete sounds;
            }});
    // The Game owns the block before the constructor runs: its helpers read GetGuiManager() before it returns.
    if (void* block = ::operator new(sizeof(ui::GuiManager))) {
        game->m_guiManager = Owned<ui::GuiManager, &DestructGuiManager>(block);
        game->m_guiManager.get()->Construct();
    }

    CreateGameStates(*game);

    Scan(game);
    Load(game);
    game->m_currentState = game->m_nextState = game->m_states[static_cast<int>(game::GameStateId::Intro)].get();
    GetRenderer()->m_directMode = 0;
    EnsureDirectory("photos");
    EnsureDirectory("SaveGames");
    GetContext().gameConstructed = true;
    game->m_currentState->Enter();
    return game;
}

// ---- the frame: update and render ------------------------------------------------------------------------------------------------------
void UpdateClock(game::GameClock* clock)
{
    RunClockUpdate(clock, os::TickMs());
}

// A pending state switch runs Exit/Enter and restarts the clock and the buttons before the frame; a save picked in the main menu is
// loaded first. The frame then updates clock, input, state, cursor and the GUI/sound/loop sounds with the clock's delta.
// True while LoadGame runs after WorldTest: the gameplay GUI script stays loaded although its world is gone, so its modes and HUD
// must not run. A LoadGame entered from the menus keeps its GUI running (the scenario's first GUI frames happen there).
bool g_staleGameplayGui;
PresentObserver g_renderPresentObserver;

void Update(Game* game)
{
    const bool switched = game->m_currentState != game->m_nextState;
    // Entering or leaving a world, or loading a save, runs in this one frame (game_host.h).
    const auto* worldTest = game->m_states[static_cast<int>(game::GameStateId::WorldTest)].get();
    const bool longFrame = (switched && (game->m_currentState == worldTest || game->m_nextState == worldTest)) ||
            (game->m_nextState == game->m_states[static_cast<int>(game::GameStateId::MainMenu)].get() && ui::LoadPendingFor(*game));
    if (longFrame) platform::SuspendAudioOutput();
    if (switched) {
        g_staleGameplayGui = game->m_currentState == game->m_states[static_cast<int>(game::GameStateId::WorldTest)].get() &&
                game->m_nextState == game->m_states[static_cast<int>(game::GameStateId::LoadGame)].get();
        game->m_currentState->Exit();
        game->m_currentState = game->m_nextState;
        game->m_currentState->Enter();
        UpdateClock(&game->m_clock);
        InputClearButtonState(*GetInput());
    }
    // Bug fix: a save loads inside this one frame (main menu -> WorldTest), and the frame then ran the WorldTest update too, a
    // frame of simulation that no tick counted (a save and reload was not a no-op; found by the chaos bot's save round trip). The
    // loaded world's first update is the next frame. A new game still updates in the frame that builds its world.
    const bool loading =
            game->m_currentState == game->m_states[static_cast<int>(game::GameStateId::MainMenu)].get() && ui::LoadPendingFor(*game);
    if (loading) ui::save_panel::LoadPendingSave(game);
    if (longFrame) platform::ResumeAudioOutput();
    ui::save_panel::UpdateLoadInfoBox(game);
    UpdateClock(&game->m_clock);
    InputUpdate(*GetInput());
    // The player's commands of this tick run before the world update (docs/architecture/commands.md).
    RunCommandTick(game, switched);
    if (!loading) game->m_currentState->Update();
    CursorUpdate(GetCursor());
    const float dt = game->m_clock.m_deltaTime;
    if (!g_staleGameplayGui) game->m_guiManager.get()->Update(dt);
    game->m_sound.get()->Update(dt);
    game->m_loopSounds->UpdateFrame(dt);
    // Left Shift held, U pressed, left Alt held (physical keys), only outside a world.
    const ui::Input& input = *GetInput();
    if (InputKeyHeld(input, obl::input::KeyCode::LeftShift) && InputKeyPressed(input, obl::input::KeyCode::U) &&
            InputKeyHeld(input, obl::input::KeyCode::LeftAlt) && !GetWorld()) {
        game_core::UnlockAll(game->m_scenarioProgress, game->m_sandbox != 0, CampaignCount());
        Save(game);
    }
}

// A resolution change reloads the state's GUI script here, outside the update; then the state, the GUI surfaces, the world present
// (day/night tint with a world and bNIGHT_DAY_ENABLED), the state's post-render pass, the GUI overlay and the final present.
void DrawOverlay(ui::GuiManager* gui);

void Render(Game* game)
{
    if (game->m_guiReloadPending) {
        const char* script = game->m_currentState->GetGuiScript();
        game->m_guiManager.get()->LoadScript(script);
        game->m_guiManager.get()->OpenVideoOptions();
        game->m_guiReloadPending = 0;
    }
    game->m_currentState->Render();
    ui::Renderer* renderer = GetRenderer();
    int viewport[4] = {0, 0, renderer->m_width, renderer->m_height};
    ui::renderer::SetViewport(renderer, viewport);
    if (!g_staleGameplayGui) game->m_guiManager.get()->DrawSurfaces();
    uint8_t nightDay = 0; // stays off when the key is missing
    game_parameters::GetBool(game_parameters::Current(), &nightDay, "bNIGHT_DAY_ENABLED");
    if (nightDay && GetWorld())
        // The day time is pushed as a float; no variant of the present reads it (day_night_present_runtime.cpp).
        PresentDayNight(renderer);
    else
        ui::renderer::PresentWorld(renderer);
    ui::renderer::RestoreLostSurfaces(renderer);
    game->m_currentState->PostRender();
    if (!g_staleGameplayGui) DrawOverlay(game->m_guiManager.get());
    ui::renderer::Present(renderer);
    // Only this present: the loading bar's presents are not captured.
    if (g_renderPresentObserver) g_renderPresentObserver(renderer);
}

// ---- the GUI overlay and its blit clipping ----------------------------------------------------------------------------------------------
// ClipBlitRect (only caller DrawOverlay): the destination {l, t, r, b} is clipped to the renderer, and the source
// rectangle's edges move by the same amounts.
void __cdecl ClipBlitRect(int* dst, int* src)
{
    const ui::Renderer* renderer = GetRenderer();
    const int width = renderer->m_width, height = renderer->m_height;
    if (dst[0] < 0) {
        src[0] -= dst[0];
        dst[0] = 0;
    }
    if (width < dst[2]) {
        src[2] += width - dst[2];
        dst[2] = width;
    }
    if (dst[1] < 0) {
        src[1] -= dst[1];
        dst[1] = 0;
    }
    if (height < dst[3]) {
        src[3] += height - dst[3];
        dst[3] = height;
    }
}

// Everything the GUI draws over the presented world, in direct mode on the back buffer: an optional background capture, the mode's
// overlay, the HUD bar with its widgets and the message balloon, radar, progress bar, the open dialogs from the last to the first, the
// software cursor (clipped at the right and bottom edges), credits and tooltip. Mode 6 keeps only the cursor, credits and tooltip.
void DrawOverlay(ui::GuiManager* gui)
{
    using ui::Renderer;
    const int width = gui->m_renderer->m_width, height = gui->m_renderer->m_height;
    if (gui->m_cameraGrabRequested) {
        screen::Rect rect{0x43, 0x3f, width - 0x43, height - 0x71};
        gui->m_cameraGrabSurface = ui::renderer::CaptureRect(GetRenderer(), &rect);
        gui->m_cameraGrabRequested = 0;
    }
    ui::renderer::SetActiveSurface(gui->m_renderer, 0);
    gui->m_renderer->m_directMode = 1;
    if (gui->m_modeObject) gui->m_modeObject->DrawScreen();
    if (gui->m_mode != 6) {
        if (gui->m_mode != 2) {
            ui::renderer::BlitSurface(gui->m_renderer, 0, height - gui->m_barHeight, gui->m_barSurface);
            if (gui->m_mainItem) (static_cast<ui::GuiWidget*>(gui->m_mainItem))->DrawOverlay();
            if (gui->m_balloon) {
                int ticker[4] = {gui->m_tickerLeft, gui->m_tickerTop, gui->m_tickerRight - gui->m_tickerLeft,
                        gui->m_tickerBottom - gui->m_tickerTop};
                ui::renderer::SetViewport(gui->m_renderer, ticker);
                (static_cast<ui::GuiWidget*>(gui->m_balloon))->Invalidate();
                (static_cast<ui::GuiWidget*>(gui->m_balloon))->Draw();
                int full[4] = {0, 0, gui->m_renderer->m_width, gui->m_renderer->m_height};
                ui::renderer::SetViewport(gui->m_renderer, full);
            }
        }
        if (gui->m_radarEnabled) {
            ui::renderer::BlitSurface(gui->m_renderer, gui->m_panelX0, gui->m_panelY0, gui->m_radarSurface);
            gui->DrawRadar();
        }
        if (gui->m_progressVisible) {
            ui::renderer::BlitSurface(gui->m_renderer, gui->m_progressX0, gui->m_progressY0, gui->m_progressSurface);
            if (gui->m_progressVisible && gui->m_progressBig && gui->m_progressDialog)
                (static_cast<ui::GuiWidget*>(gui->m_progressDialog))->DrawOverlay();
        }
        // A reverse walk: the sentinel is read once, the first node again before every step (a draw may open or close a dialog).
        for (ui::GuiManagerDialogNode* it = gui->m_dialogs; it != gui->m_dialogs->m_next; it = it->m_previous) {
            ui::GuiManagerDialogEntry* entry = it->m_previous->m_entry;
            if (entry->m_state != 1) continue;
            int scratch[4];
            // The dialog starts with its widget base, so the widget facade takes the dialog as is.
            const int* bounds = (reinterpret_cast<ui::GuiWidget*>(entry->m_dialog))->GetBounds(scratch);
            int dst[4] = {bounds[0], bounds[1], bounds[2], bounds[3]};
            int src[4] = {0, 0, bounds[2] - bounds[0], bounds[3] - bounds[1]};
            ClipBlitRect(dst, src);
            ui::renderer::BlitSurfaceRect(gui->m_renderer, dst, src, entry->m_surface);
            gui->m_variableProvider = entry->m_provider;
            (reinterpret_cast<ui::GuiWidget*>(entry->m_dialog))->DrawOverlay();
        }
    }
    const ui::SoftwareCursor* cursor = GetCursor();
    if (!settings_store::Current().m_systemCursor && !cursor->m_systemCursorHidden) {
        const int x = GetInput()->m_x;
        const int y = GetInput()->m_y;
        if (x < width - 32 && y < height - 32) {
            ui::renderer::BlitSurface(gui->m_renderer, x, y, gui->m_cursorSurface);
        } else {
            int dst[4] = {x, y, x + 31, y + 31};
            int src[4] = {0, 0, 31, 31};
            if (x >= width - 32) {
                dst[2] = width - 1;
                src[2] = width - x - 1;
            }
            if (y >= height - 32) {
                dst[3] = height - 1;
                src[3] = height - y - 1;
            }
            ui::renderer::BlitSurfaceRect(gui->m_renderer, dst, src, gui->m_cursorSurface);
        }
    }
    if (gui->m_credits) gui::CreditsDraw(reinterpret_cast<ui::GuiCredits*>(gui->m_credits));
    static_cast<ui::GuiTooltip*>(gui->m_tooltip)->Draw();
    gui->m_renderer->m_directMode = 0;
}

game::GameClock* ClockConstruct(game::GameClock* clock, uint32_t tick)
{
    clock->m_startTick = tick;
    clock->m_lastSecond = 0.0f;
    clock->m_frames = clock->m_framesAtSecond = 0;
    clock->m_time = clock->m_deltaTime = clock->m_previousTime = 0.0f;
    clock->m_updateMs = clock->m_renderMs = 0;
    return clock;
}

// The time is the signed tick difference times 0.001f; the FPS sample point
// advances by one second when the unrounded time is more than a second past it (NaN never is).
void ClockUpdate(game::GameClock* clock, uint32_t tick)
{
    const int frames = ++clock->m_frames;
    clock->m_previousTime = clock->m_time;
    const double time = static_cast<double>(static_cast<int32_t>(tick - clock->m_startTick)) * static_cast<double>(0.001f);
    clock->m_time = static_cast<float>(time);
    clock->m_deltaTime = static_cast<float>(time - clock->m_previousTime);
    if (time - clock->m_lastSecond > 1.0) {
        clock->m_lastSecond = static_cast<float>(static_cast<double>(clock->m_lastSecond) + 1.0);
        clock->m_fps = frames - clock->m_framesAtSecond;
        clock->m_framesAtSecond = frames;
    }
}

// A double to int: 64-bit truncation, the caller keeps the low 32 bits; out of the int64 range the result is
// 0x8000000000000000, whose low 32 bits are 0.
int32_t DoubleToInt(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

// The whole hour and the quarter of a day time. The quarter re-reads the whole hour as an unsigned 32-bit value
// (read as unsigned 64-bit), and a NaN minute
// compares as "below" every threshold.
int WholeHour(void*, float hour)
{
    return DoubleToInt(std::floor(static_cast<double>(hour)));
}
int QuarterMinutes(void*, float hour)
{
    const uint32_t whole = static_cast<uint32_t>(DoubleToInt(std::floor(static_cast<double>(hour))));
    const double minutes = (static_cast<double>(hour) - static_cast<double>(whole)) * 60.0;
    int quarter = 0;
    if (minutes >= 15.0) quarter = 15;
    if (minutes >= 30.0) quarter = 30;
    return minutes >= 45.0 ? 45 : quarter;
}

// The profile timer: the cycle counter around a measured block, minus the overhead Construct measured.
void TimerStart(game::ProfileTimer* timer)
{
    timer->m_start = __rdtsc();
}
void TimerStop(game::ProfileTimer* timer)
{
    const uint64_t now = __rdtsc();
    timer->m_elapsed = static_cast<int64_t>(now - timer->m_start) - static_cast<int64_t>(timer->m_overhead);
}
void TimerStartStop(game::ProfileTimer* timer)
{
    TimerStart(timer);
    TimerStop(timer);
}
// The first pair runs with whatever overhead the block held and is discarded; m_start keeps the last pair's start.
game::ProfileTimer* TimerConstruct(game::ProfileTimer* timer)
{
    TimerStartStop(timer);
    timer->m_elapsed = 0;
    timer->m_overhead = 0;
    int64_t sum = 0;
    for (int i = 0; i < 10; ++i) {
        TimerStartStop(timer);
        sum += timer->m_elapsed;
    }
    timer->m_elapsed = 0;
    timer->m_overhead = static_cast<int32_t>(sum / 10);
    return timer;
}

// The time multipliers of the three game speeds.
void InitSpeedTable()
{
    float* table = GameSpeedTable();
    std::copy(std::begin(world_control::kGameSpeedDefaults), std::end(world_control::kGameSpeedDefaults), table);
}

// The Game is built once, before any script: the boot itself is the test. The finished object is checked
// against what construction must leave (member types, the globals each constructor registers, the scalar fields, the directories).
test::CallCounter g_construct("GameCoreConstruct"), g_constructCalls("GameCoreConstructCalls");
void CheckConstructed(const Game& game, const Game* result, HINSTANCE__*, void* window)
{
    auto check = [](bool match, const char* what) { g_construct.Record(match, what); };
    check(result == &game && GetGame() == &game && GetContext().gameConstructed, "game context");
    check(GetContext().errorWindow == window, "error window");
    const char* const kNames[] = {"WorldTest", "MainMenu", "Intro", "ExitGame", "ResetGame", "LoadGame", "GameWon"};
    for (int i = 0; i < static_cast<int>(game::GameStateId::Count); ++i)
        check(game.m_states[i] && game.m_states[i]->Name() == kNames[i], "state by name");
    check(game::CurrentStateIndex(game) == static_cast<int>(game::GameStateId::Intro), "current state is Intro");
    // Constructors register in Context; its accessors must agree with Game ownership.
    check(game.m_settings && game.m_textTable && game.m_textTable.get() == ui::text_table::Current(), "settings and text table");
    check(game.m_renderer && game.m_renderer.get() == GetRenderer() && !game.m_renderer->m_directMode && game.m_renderer->m_displayOk,
            "renderer");
    check(game.m_debugFont != nullptr, "debug font");
    check(game.m_input && game.m_input.get() == GetInput() && game.m_input->m_window == window, "input");
    check(game.m_settings->m_systemCursor || !game.m_input->m_systemCursorEnabled, "system cursor switch");
    check(game.m_cursor && game.m_cursor.get() == GetCursor(), "cursor");
    check(&game.m_clock == GetGameClock() && game.m_clock.m_time == 0.0f, "clock");
    check(game.m_spriteBank && game.m_spriteBank.get() == ui::GetSpriteBank(), "sprite bank");
    check(game.m_sound && game.m_sound.get() == audio::CurrentSound() && game.m_sound->musicVolume == game.m_settings->m_musicVolume,
            "sound");
    check(game.m_loopSounds && game.m_loopSounds.get() == audio::CurrentLoopSounds(), "loop sounds");
    check(game.m_guiManager && game.m_guiManager.get() == GetGuiManager(), "gui manager");
    check(!game.m_guiReloadPending && game.m_loadSlot == -1 && !game.m_loadListFilled && !game.m_sandbox && !game.m_selectedSaveSlot &&
                    !game.m_suspended && game.m_saveSlots.empty(),
            "scalar fields");
    scenario_load::Campaign campaign;
    LoadCampaignOrFatal(campaign);
    const size_t scenarios = campaign.campaign.size() + campaign.sandboxes.size();
    std::error_code error;
    check(game.m_scenarios.size() == scenarios && scenarios > 0, "scenario count");
    check(game.m_lastUnlockedScenario == game_core::SelectAfterLoad(game.m_scenarioProgress, false, CampaignCount()), "scenario selection");
    check(std::filesystem::is_directory(os::ResolvePath("photos"), error) &&
                    std::filesystem::is_directory(os::ResolvePath("SaveGames"), error),
            "directories");
}

// The construction a host started (game_api::BeginCreate) until FinishCreate completes it.
struct PendingConstruction {
    HINSTANCE__* instance = nullptr;
    void* window = nullptr;
    bool movie = false;
};
PendingConstruction g_pending;

Game* FinishChecked(Game* game)
{
    const PendingConstruction pending = g_pending;
    g_pending = {};
    Game* result = ConstructAfterMovie(game, pending.instance, pending.window, pending.movie);
    CheckConstructed(*game, result, pending.instance, pending.window);
    return result;
}

// The frame update and render: the registered outer wrapper (WrapGameUpdate/WrapGameRender) runs first and calls Body.
template<void (*Frame)(Game*)> struct FrameMethod {
    static inline GameFrame wrapper{};
    static void Body(void* game)
    {
        g_calls.Count();
        Frame(static_cast<Game*>(game));
    }
    static void Run(void* game) { (wrapper ? wrapper : Body)(game); }
    static bool Wrap(GameFrame outer, GameFrame* body)
    {
        if (wrapper) return false;
        wrapper = outer;
        *body = Body;
        return true;
    }
};
using UpdateGame = FrameMethod<Update>;
using RenderGame = FrameMethod<Render>;
// The clock update: like the frame methods, an outer wrapper (determinism.cpp's fixed frame time) may run first.
struct ClockUpdateMethod {
    static inline GameClockStep wrapper{};
    static void Body(game::GameClock* clock, uint32_t tick)
    {
        g_calls.Count();
        ClockUpdate(clock, tick);
    }
};
void RunClockUpdate(game::GameClock* clock, uint32_t tick)
{
    (ClockUpdateMethod::wrapper ? ClockUpdateMethod::wrapper : ClockUpdateMethod::Body)(clock, tick);
}

// `call-drive GameCoreClocks N`: the clock, game time and profile timer helpers against known values, on local objects.
test::CallCounter g_clocks("GameCoreClocks");
test::CallCounter g_speed("GameCoreSpeed");
void DriveSpeed(int count)
{
    auto* const world = GetWorld();
    for (int i = 0; i < count; ++i) {
        Char16 text[32]{};
        GetVariable(GetGame(), "GAME_GAME_SPEED", text);
        g_speed.Record(text16::ParseLong(text, nullptr, 10) == (world ? world->m_gameSpeedLevel : 0), "current speed level");
        GetContext().world = nullptr;
        GetVariable(GetGame(), "GAME_GAME_SPEED", text);
        GetContext().world = world;
        g_speed.Record(!text16::Compare(text, u"0"), "speed level without a world");
    }
}
void DriveClocks(int count)
{
    for (int i = 0; i < count; ++i) {
        game::GameClock clock{};
        constexpr uint32_t start = 0xffffff00u;
        g_clocks.Record(ClockConstruct(&clock, start) == &clock && clock.m_frames == 0 && clock.m_deltaTime == 0, "clock reset");
        ClockUpdate(&clock, start + 500u);
        g_clocks.Record(std::abs(clock.m_time - 0.5f) < 0.00001f && std::abs(clock.m_deltaTime - 0.5f) < 0.00001f && clock.m_frames == 1,
                "tick wrap keeps half a second");
        ClockUpdate(&clock, start + 750u);
        g_clocks.Record(
                std::abs(clock.m_deltaTime - 0.25f) < 0.00001f && std::abs(clock.m_previousTime - 0.5f) < 0.00001f, "quarter-second delta");
        ClockUpdate(&clock, start + 1500u);
        g_clocks.Record(clock.m_fps == 3 && clock.m_framesAtSecond == 3 && clock.m_lastSecond == 1.0f, "FPS sample after a second");
        ClockUpdate(&clock, start - 500u);
        g_clocks.Record(std::abs(clock.m_time + 0.5f) < 0.00001f && std::abs(clock.m_deltaTime + 2.0f) < 0.00001f, "signed backwards tick");
        g_clocks.Record(WholeHour(nullptr, 12.75f) == 12 && QuarterMinutes(nullptr, 12.75f) == 45 &&
                        QuarterMinutes(nullptr, 12.249f) == 0 && QuarterMinutes(nullptr, 12.5f) == 30,
                "hour and quarter boundaries");
        game::ProfileTimer timer{};
        TimerConstruct(&timer);
        g_clocks.Record(timer.m_elapsed == 0 && timer.m_overhead >= 0 && timer.m_start != 0, "timer reset");
        TimerStart(&timer);
        timer.m_overhead = 0;
        TimerStop(&timer);
        g_clocks.Record(timer.m_elapsed >= 0, "timer elapsed ticks");
    }
}

} // namespace

// The counted entries: the callers of the day-time helpers (building variable text, the gui dialogs) call these
// directly.
int GameTimeWholeHour(float hour)
{
    g_calls.Count();
    return WholeHour(nullptr, hour);
}
int GameTimeQuarterMinutes(float hour)
{
    g_calls.Count();
    return QuarterMinutes(nullptr, hour);
}

namespace game_api {
namespace {
// The facade counts every call in `call-report GameCoreCalls`.
Game* Current()
{
    g_calls.Count();
    return GetGame();
}
} // namespace
void BeginCreate(void* instance, void* window)
{
    g_constructCalls.Count();
    // The context holds the Game before its construction runs: its members reach it through GetGame() while they are built.
    Context& context = GetContext();
    context.game = std::make_unique<Game>();
    g_pending = {static_cast<HINSTANCE__*>(instance), window, false};
    g_pending.movie = ConstructUntilMovie(context.game.get(), window);
}
Game* FinishCreate()
{
    return FinishChecked(GetContext().game.get());
}
Game* Create(void* instance, void* window)
{
    BeginCreate(instance, window);
    while (platform::IntroMovieFrame()) {}
    return FinishCreate();
}
void Destroy()
{
    Context& context = GetContext();
    if (!context.game) return;
    g_calls.Count();
    game::Destruct(context.game.get());
    context.gameConstructed = false;
    context.game.reset();
}
void Update(Game* game)
{
    UpdateGame::Run(game);
}
void Render(Game* game)
{
    RenderGame::Run(game);
}
void OnActivate()
{
    Activate(Current());
}
void OnDeactivate()
{
    Deactivate(Current());
}
void SetNextState(int state)
{
    NextState(Current(), state);
}
void ChangeResolution(int width, int height)
{
    game::ChangeResolution(Current(), width, height);
}
bool LoadGuiScript(const char* script)
{
    return game::LoadGuiScript(Current(), script) != 0;
}
void DrawFullscreenTga(const char* path)
{
    game::DrawFullscreenTga(Current(), path);
}
void GetVariable(const char* name, Char16* out)
{
    game::GetVariable(Current(), name, out);
}
void SetVariable(const char* name, const int* value)
{
    game::SetVariable(Current(), name, value);
}
float Version()
{
    g_calls.Count();
    return game::Version();
}
bool ReturnToMainMenu()
{
    return ReturnToMenu(Current()) != 0;
}
void RequestReturnToMainMenu()
{
    SetReturn(Current(), 1);
}
void CancelReturnToMainMenu()
{
    ClearReturn(Current());
}
void SetSandboxMode(bool sandbox)
{
    Sandbox(Current(), sandbox);
}
void LoadScenarioProgress()
{
    Load(Current());
}
void ResetScenarioProgress()
{
    Reset(Current());
}
void SaveScenarioProgress()
{
    Save(Current());
}
void CompleteCurrentScenario()
{
    Complete(Current());
}
int ScenarioCount()
{
    return Count(Current());
}
const Char16* Scenario(int index)
{
    return reinterpret_cast<const Char16*>(game::Scenario(Current(), index));
}
bool IsScenarioCompleted(int index)
{
    return Completed(Current(), index) != 0;
}
bool IsScenarioAlwaysAvailable(int index)
{
    return Always(Current(), index) != 0;
}
void InitSpeedTable()
{
    g_calls.Count();
    ::obl::game::InitSpeedTable();
}

void RestartClock()
{
    ClockConstruct(&Current()->m_clock, os::TickMs());
}
void StartProfileTimer()
{
    TimerStart(&Current()->m_profileTimer);
}
int StopProfileTimerMs()
{
    auto& timer = Current()->m_profileTimer;
    TimerStop(&timer);
    // The low 32 bits of the elapsed ticks, divided signed.
    return static_cast<int32_t>(timer.m_elapsed) / 1000;
}
void StartStopProfileTimer()
{
    TimerStartStop(&Current()->m_profileTimer);
}
bool RegisterTestDrivers()
{
    g_clocks.SetDriver(DriveClocks);
    g_speed.SetDriver(DriveSpeed);
    return true;
}
} // namespace game_api

bool WrapGameUpdate(GameFrame wrapper, GameFrame* body)
{
    return UpdateGame::Wrap(wrapper, body);
}
bool WrapGameRender(GameFrame wrapper, GameFrame* body)
{
    return RenderGame::Wrap(wrapper, body);
}
void SetGameRenderPresentObserver(PresentObserver observer)
{
    g_renderPresentObserver = observer;
}
bool WrapGameClockUpdate(GameClockStep wrapper, GameClockStep* body)
{
    if (ClockUpdateMethod::wrapper) return false;
    ClockUpdateMethod::wrapper = wrapper;
    *body = ClockUpdateMethod::Body;
    return true;
}
// The campaign order is read from the converted pack each time it is needed (a reset, a new world). A missing or refused file stops the
// game with its name, as every switched loader does (docs/build/game-data.md "No fallback").
void LoadCampaignOrFatal(scenario_load::Campaign& out)
{
    std::string error;
    if (scenario_load::LoadCampaign(pack_path::Selected(), out, error)) return;
    const std::string message = "Campaign: " + error + ". Import the game data again";
    FatalErrorWith("%s", message.c_str());
    std::abort(); // FatalError exits the process
}
} // namespace obl::game
