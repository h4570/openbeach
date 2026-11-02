// Game start-up: OpenBeach.exe (src/launcher) calls OpenGame, then platform::RunGame, so every module is initialized before
// the game starts.

#include "core/config.h"
#include "platform/log_emergency.h"
#include "runtime_reports.h"
#include "core/wide_path.h"
#include "core/pack_path.h"
#include "game/ui/gui_content_runtime.h"
#include "game/ui/gui_layouts_runtime.h"
#include "game/ui/blit.h"
#include "platform/frame_limiter.h"
#include "platform/hidden_window.h"
#include "platform/instance_lock.h"
#include "game/ui/input.h"
#include "platform/intro_movie.h"
#include "game/ui/font_construct.h"
#include "game/ui/image.h"
#include "game/ui/music_player.h"
#include "game/ui/sound_types.h"
#include "game/ui/playlist_services.h"
#include "game/ui/text_table.h"
#include "game/messages/message_handler.h"
#include "game/objects/script_program_run.h"
#include "game/objects/script_program_load.h"
#include "game/world/world_script_load.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/feedback/feedback_manager.h"
#include "game/world/world_script_host_game.h"
#include "game/repair/repair_manager.h"
#include "game/ui/cursor.h"
#include "game/ferry/ferry.h"
#include "game/ui/furniture_runtime.h"
#include "game/events/event_manager.h"
#include "game/world/world_script.h"
#include "game/objects/guest.h"
#include "game/objects/guest_script.h"
#include "game/objects/staff_feedback.h"
#include "game/objects/performer_lifecycle.h"
#include "game/objects/staff_roster.h"
#include "game/weather/weather.h"
#include "game/world/world_control.h"
#include "game/world/world_step.h"
#include "game/world/world_factory.h"
#include "game/world/world_lifecycle.h"
#include "game/world/world_view.h"
#include "game/world/world_create.h"
#include "game/world/world_objects.h"
#include "game/world/world_receive.h"
#include "game/world/world_messages.h"
#include "game/world/world_update.h"
#include "game/objects/building_methods.h"
#include "game/objects/object_building.h"
#include "game/objects/building_serial.h"
#include "game/objects/building_queue.h"
#include "game/objects/commodity_providers.h"
#include "game/ui/sprite_anim.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/gui_variable.h"
#include "game/game/game_api.h"
#include "game/ui/gui_variable_image.h"
#include "game/ui/gui_modes_tools.h"
#include "game/ui/gui_sort.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_radar.h"
#include "game/ui/save_slots.h"
#include "game/ui/gui_action_entries.h"
#include "game/ui/gui_modes_place.h"
#include "game/ui/gui_script.h"
#include "game/ui/scripted_sound_runtime.h"
#include "game/ui/loop_sound_control.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_controls.h"
#include "game/ui/gui_containers.h"
#include "game/ui/gui_lists.h"
#include "game/ui/gui_tables.h"
#include "game/ui/sprite_instance_render.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/map/water_anim.h"
#include "platform/renderer_display.h"
#include "game/ui/palette_modifiers.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_instance_preview.h"
#include "game/ui/sprite_instance_bake.h"
#include "game/ui/music_stream.h"
#include "game/ui/sample_mixer.h"
#include "game/map/grid_state.h"
#include "game/map/grid_lifecycle.h"
#include "game/objects/object_scenery.h"
#include "game/objects/scenery_lifecycle.h"
#include "game/ui/map_cache.h"
#include "game/world/world_queries.h"
#include "game/construction/construction_manager.h"
#include "game/objects/lifeguard_fns.h"
#include "game/world/world_queries_api.h"
#include "game/ui/rep_security.h"
#include "game/objects/staff_direct.h"
#include "game/objects/staff_actions.h"
#include "game/objects/object_base.h"
#include "game/objects/object_moving.h"
#include "platform/window_placement.h"
#include "platform/sdl_present.h"
#include "platform/platform_main.h"
#include "game/world/world_params.h"
#include "platform/host_init.h"
#include "init.h"
#include "core/log.h"
#include "testing/autotest.h"
#include "testing/headless_sim.h"

#include <string>

// OpenGame is the game executable's start-up; InitGame below is shared with the Linux obl-sim
// (docs/architecture/linux-host.md), which skips the Windows-only start-up steps (OBL_WINDOWS_ONLY).
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>

namespace {

std::string Utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

#ifdef _WIN32
std::filesystem::path ModulePath(HMODULE module)
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(module, buf, MAX_PATH);
    return std::wstring(buf, n);
}
#endif

void CloseLogAtExit()
{
    OBL_LOG("process exit");
    obl::log::Close();
}

} // namespace

int obl::OpenGame(const std::filesystem::path& gameDirectory)
{
    using namespace obl;

    const std::u16string wide = gameDirectory.u16string();
    const Text16 gameDir(reinterpret_cast<const Char16*>(wide.c_str()));
    platform::InitLogEmergency();
    log::Open(WidePath(gameDir + host::kLogFileName));
    std::atexit(CloseLogAtExit);

    OBL_LOG("==== OpenBeach started (built " __DATE__ " " __TIME__ ") ====");
#ifdef _WIN32
    log::SetDebugSink([](const char* line) { OutputDebugStringA(line); });
    OBL_LOG("pid %lu, game directory %s, host %s", GetCurrentProcessId(), Utf8(gameDirectory).c_str(), Utf8(ModulePath(nullptr)).c_str());
#else
    OBL_LOG("pid %d, game directory %s", static_cast<int>(getpid()), Utf8(gameDirectory).c_str());
#endif

    return InitGame(gameDir);
}

// The shared part of start-up: the Windows OpenGame above and the Linux obl-sim's main (src/host_linux/main.cpp).
int obl::InitGame(const Text16& gameDir)
{
    using namespace obl;

    InitRuntimeReports();
    config::Load(gameDir);
    // A headless run reports on stdout and in the file; it has no debugger to feed (and the Linux obl-sim has no such call).
    if (config::Get().headless) log::SetDebugSink(nullptr);
    // The pack every loader reads (OBL_PACK, default `base`); a wrong id or a missing pack stops the start before any loader.
    if (std::string packError; !pack_path::CheckSelected(WidePath(gameDir), packError)) {
        OBL_LOG("FatalError: pack: %s. Import the game data again", packError.c_str());
        return 8;
    }
    // Converted GUI tables and layouts (packs/base/gui/); missing files stop the game only when the GUI first needs them.
    gui::gui_content::Load(gameDir);
    gui::gui_layouts::Load(gameDir);
    const bool started = (platform::SetInstanceLockDirectory(gameDir), true) && OBL_WINDOWS_ONLY(platform::RegisterPlatformMainReports()) &&
            OBL_WINDOWS_ONLY(platform::StartHiddenMode()) && ui::RegisterInput() && platform::InitializeResolutions() &&

            ui::RegisterBitmapFontTestDriver() && OBL_WINDOWS_ONLY(platform::ConfigureFrameLimiter()) &&
            world::RegisterWorldParamsDriver() && (audio::ConfigureScriptedSound(gameDir), audio::ConfigureLoopSounds(), true) &&
            ui::RegisterTextTableDrivers() && ui::images::RegisterDrivers() && ui::RegisterBlitDrivers() &&
            (audio::ConfigureSamples(gameDir), true) && (map::SetPackDirectory(gameDir), true) && gui::RegisterGuiDialogDrivers() &&
            ui::SetMapPackDirectory(gameDir) && world::ConfigureWorldQueryDrivers() && ui::ConfigureSpriteAnimations(WidePath(gameDir)) &&
            ui::ConfigureSpriteBank(WidePath(gameDir)) && world::RegisterWorldScriptDrivers() && object::RegisterGuestTestDrivers() &&
            object::RegisterPerformerTestDrivers() && ui::RegisterFurniture() && world::RegisterWorldUpdateDriver() &&
            world::RegisterWorldMessageDrivers() && weather::RegisterTestDrivers() && object::RegisterBuildingThoughtDriver() &&
            object::RegisterGuestScriptDelegates() && object::LoadScriptProgramVocabulary(gameDir) &&
            world::LoadWorldScriptVocabulary(gameDir) && feedback::LoadFeedbackVocabulary(gameDir) &&
            object::RegisterCommodityProviderDrivers() && disaster::RegisterEventTestDrivers() && ferry::RegisterFerryTestDriver() &&
            repair::RegisterRepairTestDrivers() && cleaning::RegisterCleaningTestDriver() && feedback::RegisterFeedbackTestDriver() &&
            world::RegisterWorldControlDrivers() && game::game_api::RegisterTestDrivers() && gui::RegisterToolModeDrivers() &&
            gui::RegisterGuiSortDrivers() && ui::RegisterSaveSlotDrivers() && gui::RegisterPlaceModeDrivers() &&
            gui::RegisterGuiActionDrivers() && gui::RegisterGuiControls() && gui::RegisterGuiLists() && gui::RegisterGuiTables() &&
            ui::RegisterPaletteDrivers() &&

            ui::RegisterSpriteBakeDrivers() &&
            // The vehicle presentation driver must wrap the native renderer and guest/building queries.
            test::InitAutotest(gameDir) && test::InitHeadless();
    if (!started) return 4;

    OBL_LOG("init done");
    return 0;
}
