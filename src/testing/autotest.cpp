#include "core/char16.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/renderer_api.h"
// Scripted test runs (launcher --script <file> -> OBL_SCRIPT). Commands run on the game thread at
// the start of game_api::Update, so they never race with game code:
//
//   # comment
//   wait <ms> [frames]   pause the script for <ms> and at least [frames] frames (prefer `await`, it does not depend on the
//                        machine speed)
//   move <x> <y>         synthetic cursor position in game coordinates
//   click <x> <y> [right]   move, press, release; each step waits 50/100 ms and at least 3/6 frames (kClickFrames below)
//   shot <name>          save the next presented frame to <game dir>/captures/<name>.png
//   expect-shot <name> [tol] [max-percent]   wait for the next presented frame, save it like `shot` and compare it with
//                        <game dir>/golden/<name>.png: PASS when at most max-percent (default 0) of the pixels have a channel
//                        error above tol (default 0); no golden = SKIP; OBL_UPDATE_GOLDEN=1 records it instead (golden.cpp)
//   res <w> <h>          request a resolution change (as the Options screen does)
//   log <text>
//   desire-report / desire-dump [guest] / opinion-report / weather-state   guest desire and opinion model checks (testing/guest_desires.cpp)
//   opinion-helpers on|off / opinion-helpers-force [first [count]] / opinion-helpers-report   per-visit opinion helpers: checks on live guests
//       (off by default; force turns them on while it runs) / force every branch on scratch guests / report
//   provider-force / provider-report   provider selection, slot scores and rental sea-area gates (fresh scenario fixture; testing/commodity_providers.cpp)
//   mp3-scan <dir> <file>   MP3 length scan of one file (testing/mp3_scan.cpp)
//   id3-read <dir> <file>   PlaylistTrack::ReadId3Tag on a fresh track record: tag, artist/title lengths and byte sums (testing/mp3_scan.cpp)
//   image-load <path>   images::LoadTga/LoadTiff on one file; logs size and a 32-bit hash of the image (testing/image_load.cpp);
//   music-play <path>, music-report, music-pause/resume/close, music-volume <float>, music-mute/unmute;
//   music-playlist / music-playlist-tick: the playlist on a two-track fixture; music-seek-end: real decoder EOF.
//   music-night-playlist / music-night <0|1> [manual forced]: MusicPlayer::SetNight on a day+night playlist with a real stream (log: filter, index, last).
//   music-disabled <0|1>: enabled guard; live probes music-active/-paused/-seconds/-energy/-eof/-index (docs/architecture/presentation-fonts-audio.md).
//   sound-bank-contract, samples-contract: bank/Miles comparison and synchronous channel/fade contract.
//   sample-play <name> [flags] [fade], sample-select <0..15>, sample-volume <float>, sample-fade <float> <seconds>,
//   sample-out <seconds>, sample-start; samples-stop/report/volume/disabled/tick; probes samples-occupied,
//   sample-active/-seconds/-energy (docs/architecture/presentation-fonts-audio.md).
//   spatial-play/-scaled <name> <x> <y> <z> <flags> <owner>, spatial-listener <x> <y> <z>, spatial-contract/-report/-owner/-select/-start,
//   spatial-expect-mix <mono|stereo> <left> <right> (PCM energy handed to SDL vs gains), spatial-bank-refs <wav>,
//   spatial-shared-data <chA> <chB>, spatial-mixed-report <ch2D> <ch3D> (music + 2D + 3D on one device);
//   probes spatial-occupied/-active/-seconds/-out-left/-out-right (docs/architecture/presentation-fonts-audio.md).
//   fail-allowed/fail-create/fail-arm/fail-report/fail-hold   building breakdown model (testing/breakdowns.cpp)
//   building-list/building-visits/building-age/building-price/building-demolish/building-state/building-poll-flag/building-dismiss-staff/building-hire-staff   toilet cleaning, demolition and state of buildings (testing/building_fixture.cpp)
//   cleaning-report <label> / cleaning-fixture [n]   CleaningManager queues and cleaners (testing/cleaning_manager.cpp)
//   fatal-error <text> / show-error-exit <text>   end the game through FatalError / ShowErrorAndExit (`# @expect-exit` scripts; platform/test_tools/expected_exit.cpp)
//   close-window         end the game through WM_CLOSE like the window's close button, without `quit` (`# @expect-exit` scripts)
//   feedback-report <label> / feedback-fixture [n]   FeedbackManager queues, history and silenced types (testing/feedback_manager.cpp)
//   repair-report <label> / repair-fixture [buildings] [scenery]   RepairManager queues and mechanics (testing/repair_manager.cpp)
//   startup-power <NAME> <tx> <ty> [demandAll installed output]   STATE_STARTUP power rule (testing/power_startup.cpp)
//   spawn-building <NAME> <tx> <ty> [user]   scenario creation, bypasses purchase/build tree/construction; reports id; `user` = built by the player (world_state.cpp)
//   remove-building [id]   immediately remove a building through the normal cleanup (default: last spawned in this world)
//   staff-list <Class> [homeId]   members of one profession (Lifeguard, Mechanic, Cleaner, Rep, Builder, SecurityGuard): `staff-member` lines and
//                        a report `count= homed= leaving=` (members with that home id / in state 0x4c)
//   staff-direct <Class> <state> [x y]   StaffDirectToPoint (the call of the send-staff GUI mode's left click) on the members in that state; report `directed=`
//   staff-direct-object <Class> <id|guest|building|scenery|last|none>   the first member's `DirectToObject` at that target; report `target=`
//   staff-sack <Class> [id]   the virtual Sack of the last member of the profession (or the given id): the sack of the staff menu
//   staff-setvar <Class> <KEY> <int|float> [id]   the virtual SetVariable with one word (a '.' makes it a float): the keyword of the staff menu on a live member
//   building-setvar <KEY> <int|float> [id|queue|events|beer]   the virtual SetVariable of a live building (default: the last spawned; `queue`/`events`/`beer`: the first built one with that): a keyword of the building dialogs; the report reads the written fields back
//   staff-gettext <Class> <KEY> [id]               the virtual GetVariableText: the text of a keyword of the staff menu on a live member, logged
//   staff-demolish-target <Class> <state>   demolishes the building that the first member in that state targets (`building-demolish`); report `target= demolished=`
//                        probes staff-<profession>-<state>: members of the profession in that state
//   force-pause <0|1>   set World::m_paused and m_forcedPaused as a script's PauseGame does (fixture, not recorded)
//   set-cash <n> / set-hour <h>   cash (float) / clock within current day, 0 <= h < 24; no replay of skipped hours
//   zoom <0|1|2>        posts the zoom-button request (0 in, 1 normal, 2 out); applied by the next world update; probe zoom-level
//   camera <x> <y>      MapView::SetCameraPos (top-left corner in map pixels, clamped); the int camera follows on the next world update;
//                        probes camera-x/-y (int, what drawing uses), camera-fx/-fy (float), camera-locked, camera-moving;
//                        probes guests-drawn (the count of the last MapView::DrawObjects) and sea-share (MapView::GetVisibleTileRange)
//   probes map-blocks (mapped background blocks of MapView), map-hash and map-hash-lo/-hi (FNV-1a of their indices and bytes; the halves print exactly),
//   probe rebuild-queue (MapView::GetRebuildQueueSize: blocks waiting in the rebuild queue)
//   queue-blocks <bx> <by> <w> <h>   MapView::QueueBlockRebuild (block units): the rebuild queue UpdateCamera drains, one block per game second
//   restore-rect <x0> <y0> <x1> <y1>   MapView::RestoreBackgroundRect on a map pixel rectangle (what removing baked scenery does): the blocks under it
//                        get their MapCache colour and depth back
//   path-tile <tx> <ty> <flagsHex>   sets the grid flags of a tile and runs PathTiles::RebuildTile (0x10000080 = pavement, 0x7000 = grass, 0 = bare):
//                        paints the tile and restores the background under it (MapView::RestoreTileBackground with the frame 67 mask)
//   remove-scenery [n]   Scenery::RequestRemoval on the n-th (default 0) scenery object whose definition is burned into the blocks: restores its
//                        rectangle (MapView::RestoreBackgroundRect), rebuilds the paths, bakes the buildings over it again
//   world-darken <0|1>   The darken flag: the world draw runs MapView::DarkenByDepth each frame (otherwise never called)
//   lounger-stale-seat setup|check   seats a walking guest on a free lounger without the lying-down state (the game's "no Longer got"
//                        fatal error, now a released seat; testing/world_state.cpp); both report seated/seat0/state of that lounger
//   spawn-guests <n>     first loaded guest type, random walkable points; game startup messages, no ferry required
//   worldparams-dump     loaded WorldParams via world_params_tables.h -> run/worldparams-dump.json (tools/check_worldparams_dump.py)
//   gui-dump             GUI widget tree, dialogs and open-dialogs list via the ui:: records -> run/gui-dump.json (tools/check_gui_dump.py)
//   object-select <Cleaner|Guest|Building>   OnSelect(clicked) of the first such object: opens its info dialog with a real provider (testing/world_state.cpp)
//   gui-open <dialog> [modal] / gui-close <dialog>   GuiManager::ShowDialog (no provider) / CloseDialog; the dialog opens on the next frame
//   gui-close-all   GuiManager::CloseAllDialogs
//   gui-animate <dialog> <type> <seconds> [delay]   GuiManager::AnimateDialog on an open dialog (1 slides in, 2 slides out and closes it; docs/architecture/data-formats/converter/gui-runtime.md)
//   gui-click <dialog> <Class> <n> | tip <KEY> [right]   click the centre of a widget of an open dialog (n-th of the class, 0-based, in
//                        tree order, or the one with that tooltip key); gui-move takes the same arguments and only moves the cursor
//   gui-state [<dialog>] logs entries/open/animating/mode/requested/selection/hash/ctx*; probes gui-entries gui-open-dialogs gui-animating gui-mode
//                        gui-mode-requested gui-selection gui-context-<0..12> gui-hash (testing/gui_control.cpp, docs/architecture/data-formats/converter/gui-runtime.md)
//   gui-canvas-check     fixed sprite + text draw list through LiveCanvas (seen by the sprite/text observers) and RecordingCanvas; logs
//                        calls/sprites/text/wrapped/lines/mismatch/leaked/restored/live, FAILED on a difference (testing/gui_capture.cpp)
//   scripts-dump         scenario + object scripts via the native script records -> run/scripts-dump.json (tools/check_script_dump.py)
//   profile on|off [n]   sampling profiler of the game thread, logs PDB names and module offsets
//   call-drive <fn> <n>, call-report <fn>   process-lifetime call counters of the C++ functions (core/call_counter.cpp,
//                        docs/testing/call-counters.md); model/golden check failures fail the test directly
//   command <text> / command-report / object-find / place-find / object-report / tile-report   the command layer: submit a command to the
//                        game loop's queue, its totals, object ids, a fitting place, object fields and tile flags (testing/commands.h)
//   $key                 in the arguments of any directive (not await/expect-shot/event-await): the value of a probe or of the latest
//                        report key at the moment the directive runs, e.g. `object-report $id` after `object-find`
//   boot-gameplay [slot|last|sandbox [n]|scenario [n]]   leave the intro for gameplay at once (default: sandbox island 1;
//                        a slot number loads that save) - no logo/menu clicking (testing/flow.cpp)
//   next-state <0..6>   game_api::SetNextState: 0 WorldTest, 1 MainMenu, 2 Intro, 3 ExitGame, 4 ResetGame, 5 LoadGame, 6 GameWon (testing/flow.cpp)
//   await <cond> [timeout_ms]   block the script until: gameplay | world-loaded | frames N | hour H (next time the game clock
//                        reads H) | hours N (N game hours from now) | <probe|key> <>=|<=|==|>|<> <value>; default timeout 60 s.
//                        A missed timeout logs `FAILED await ...` and aborts the script (event-await stays as it was)
//   speed <x> / speed-level <0..2> / speed-table   game speed: write the world's speed / the game's own switch / log the table
//   world-script-change <state>   the scenario script's `ChangeState <state>;` (e.g. STATE_WORLD_AFTERGAME: no day limit; testing/flow.cpp)
//   expect <name> <value> [tol]   PASS/FAILED line; <name> is a live probe (hour day game-seconds speed speed-level paused
//                        cash buildings guests state frames render-width render-height power-demand-all power-capacity power-output power-demand
//                        world-script-state = index of the scenario script's current state, -1 without one;
//                        scenario-objective0..2 = status words the scenario script sets) or a
//                        `key=number` from the latest report line
//   expect-range <name> <min> <max> / expect-last <key> <value> [tol] / expect-last-range <key> <min> <max>
//                        (expect-last looks at report values only) / expect-same <keyA> <keyB> [tol] (two report values agree)
//   seed <n>             srand(n) of the game's own MSVCRT (its rand() drives breakdowns, guest rates, show picks...)
//   rand-test            RNG tape self-test (reseeds CRT); rand-calls probe counts since the last seed
//   fixed-dt <s>|off     from now on every frame has a fixed frame time (the game clock's frame delta) instead of GetTickCount
//   fp-report           read the FPU control word and MXCSR controls on the game thread, without changing them
//   world-hash [every N|off|expect-same|expect-different]   versioned simulation projection; periodic hashes after step
//                        frames, TSV checkpoints in run/world-hashes (OBL_WORLD_HASH_DIR overrides); docs/testing/world-hashes.md
//   record <file> [every N] / record end   record the next world's commands with a world hash every N ticks (after seed, fixed-dt,
//                        boot-gameplay scenario <n>); replay <file> [allow-mismatch] boots the file's start, feeds its commands, compares
//                        the checkpoints and blocks until its last tick (testing/replay.h, docs/testing/replays.md)
//   serial-save [copy] / serial-load / serial-cleanup   the save panel's obl.save writer into a scratch slot (9000+), load
//                        it back, delete it (testing/binary_serial.cpp)
//   serial-load <file>   an obl.save copied into the scratch slot and loaded from there (serial-cleanup deletes it), or an old
//                        `.dat` converted into it in process (obl-convert save)
//   step <n> [dt]        n game_api::Update frames back to back, rendering only at state/save/GUI script changes;
//                        dt seconds per update (default: fixed-dt or 1/60); logs `step steps= dt= renders= ...`.
//                        docs/testing/determinism.md: with `seed` + `fixed-dt`/`step` the same script gives the same report values
//   sdl-verify           the next SDL present reads the frame back and compares it with the game's buffer; probes
//                        sdl-active sdl-presents sdl-texture-width/-height sdl-verified sdl-mismatches sdl-pixels (presenter.cpp)
//   window-size <w> <h>  resize only the client area, without focus/z-order changes; window-place reapplies [Display] Scale
//                        (`window-place <w> <h>` places the window as for a game resolution of w x h; beyond the monitor = borderless)
//   move-client / click-client <x> <y>  client coordinates through the physical cursor mapping (mouse-x / mouse-y probes);
//                        move-client also takes `center`/`max` per coordinate
//                        probes monitor-width/-height, client-offset-x/-y, client-is-monitor, window-popup/-topmost, flag-*-windowed
//                        window-width/-height, display-scale, sdl-bar-pixels/-mismatches are also readable
//   crash-test[-log-lock|-filter] deliberate fatal exceptions (diagnostics check, crash.cpp)
//   crash-handled        verify that a handled SEH exception does not terminate the script
//   quit                 log `summary passed=N failed=M` and close the game window
//   display-contract     assert windowed surfaces/format/DirectDraw counters and trace cleanup
//   display-jpeg <name>   decode crop/surface/odd color bands and a numbered photo album JPEG
//   graphics-modules     report loaded ddraw/d3d8/d3d9/d3dimm DLLs (`loaded=N game-dir=M`; M > 0 = dgVoodoo next to the game executable);
//                        probes intro-movie-complete/-skipped/-player/-frames/-seconds/-duration/-audio[-clock|-seconds|-open]/-destructed (intro_movie.h;
//                        env OBL_MOVIE_SKIP_MS=N injects Escape into the pl_mpeg player)
//   cursor-contract      cursor resources, visibility, client/bar policy and Windows API trace (cursor.cpp)
//   relative-motion <dx> <dy> / input-focus <0|1>    raw delta / activation injection without physical input;
//                        probes mouse-relative mouse-dx mouse-dy camera-drag camera-speed
//   posted-mouse-burst <0|1>    left button release/press between queued Windows motion messages (mouse-left probe)
//   sdl-mouse-contract        SDL position/buttons/wheel/focus through the SDL mouse records and the input's mouse update;
//                        sdl-mouse probe selects the effective source
//   sdl-relative-drag-contract / sdl-relative-motion <dx> <dy>   SDL relative drag: SDL relative-mode request, deltas,
//                        return to the grab point and resets (hidden runs record the mode, cursor.cpp);
//                        probes sdl-relative-drag sdl-relative-requested
//   posted-key <vk> <0|1>       WM_KEYDOWN/WM_KEYUP posted to the game window; with the wrapped SDL window (default) the probes
//                        sdl-wrapped sdl-keyboard-events sdl-mouse-motion-events sdl-mouse-button-events sdl-other-events
//                        sdl-window-width/-height show SDL's side
//   sdl-key <scancode> <0|1>    an SDL key event for the game window (225 = left Shift): posted-key cannot press a modifier
//   input-object-contract   Input lifecycle, size, signed getters and all key states;
//   keyboard-contract       SDL scancode/state/repeat/tap/modifier/focus contract through Input_* entry points;
//                        probes sdl-keyboard keyboard-device key-<code> camera-vx camera-vy
//   lightning <level> (<l> <t> <r> <b> | building <i> <radius>) / lightning-target <KEY>|building <i> / lightning-state   storms through the
//                        game's Weather methods (game/weather.cpp)
//
// tools/runtests.ps1 runs every script of tests/manifest.txt. Waiting: use `await` (frames / probes / hours), never a long `wait`;
// `event-await` (building_events.cpp) polls the show state, `await limiter-n >= N` the frame limiter's 3 s samples.
// `event-tick` runs BuildingEvents::Update at the current hour without advancing the world clock (boundary checks).
//
// Script mode (OBL_SCRIPT) skips the intro movie and runs without a frame cap (config.cpp); `--max-fps`/`--movie` of the
// launcher or `# @maxfps N` in the script bring them back. Every `key=number` of an `autotest:` log line becomes readable
// by expect-last (assertions.cpp taps the log), PASS/FAILED lines are counted for the summary and for tools/autotest.ps1.
//
// A crash in script mode logs `autotest: CRASH ...` (code, address, registers, return-address candidates from the stack;
// crash.cpp) and ends the process; tools/autotest.ps1 prints those addresses with module offsets and available PDB names.
//
// The cursor position is faked through the cursor mapping in game/input.cpp (windowed mode), buttons
// are injected into the fallback mouse (always active in hidden mode).

#include "autotest.h"
#include "gui_image_bar_frame.h"
#include "game/game/game_api.h"
#include "binary_serial.h"
#include "savegame_managers.h"
#include "assertions.h"
#include "breakdowns.h"
#include "building_fixture.h"
#include "platform/test_tools/crash.h"
#include "platform/test_tools/cursor.h"
#include "determinism.h"
#include "building_events.h"
#include "flow.h"
#include "golden.h"
#include "guest_desires.h"
#include "commodity_providers.h"
#include "image_load.h"
#include "mp3_scan.h"
#include "music.h"
#include "samples.h"
#include "core/call_counter.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "test_util.h"
#include "cleaning_manager.h"
#include "commands.h"
#include "platform/test_tools/expected_exit.h"
#include "feedback_manager.h"
#include "power_startup.h"
#include "repair_manager.h"
#include "platform/test_tools/presenter.h"
#include "world_state.h"
#include "world_create_props.h"
#include "world_create_building.h"
#include "world_hash.h"
#include "replay.h"
#include "world_dump.h"
#include "world_params.h"
#include "gui_capture.h"
#include "gui_control.h"
#include "gui_dump.h"
#include "platform/test_tools/windowed_display.h"

#include "game/ui/image.h"
#include "game/objects/script_program_load.h"
#include "game/world/world_script_load.h"
#include "game/weather/weather.h"
#include "game/ui/map_view_restore.h"
#include "game/ui/input.h"
#include "game/game/command_loop.h"
#include "platform/intro_movie.h"
#include "platform/runtime.h"
#include "core/config.h"
#include "platform/windows_only.h"
#include "core/log.h"
#include "core/os.h"
#include "testing/png.h"
#include "platform/test_tools/profile.h"
#include "core/wide_path.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iterator>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace obl::test {

namespace {

struct Command {
    std::string name;
    std::vector<std::string> args;
};

std::vector<Command> g_commands;
size_t g_next;
uint32_t g_resumeAt;
unsigned long long g_resumeFrame; // `wait <ms> <frames>`: the frame count the script waits for as well
std::string g_pendingShot;
Text16 g_captureDir;

game::GameFrame g_gameUpdate;
// The outer game_api::Update of the frame that ran `step` is skipped: `step` already ran its own frames.
bool g_skipOuterUpdate;
void* g_gameSelf;

// Set by `quit`, an aborted script or a script the headless mode refuses: the headless loop stops (ScriptCloseRequested).
bool g_closeRequested;
bool g_completed;
// Directives the headless mode refused at load, as `name (line N)`; reported when the headless loop starts (a FAILED line during
// OBL_Init would fail the host's start instead, standalone_host.cpp).
std::vector<std::string> g_refused;
// The scripts of this run: one, or the lines of a `.list` file (headless only); g_script indexes the running one.
std::vector<std::filesystem::path> g_scripts;
size_t g_script;

// Ends the run. Headless has no window: posting WM_CLOSE to FindWindowA's answer would close another test's game.
void CloseGame()
{
    g_closeRequested = true;
    if (!config::Get().headless) platform::PostGameWindowClose();
}

// Directives the headless mode refuses (docs/architecture/headless/scripts.md, "Test scripts"): they need input devices, a presented
// frame, the GUI, audio or a window, none of which a headless run has. Probes stay readable; a refused script runs nothing.
bool RefusedHeadless(const std::string& name)
{
    static const char* const kExact[] = {"move", "down", "up", "click", "move-client", "click-client", "relative-motion", "input-focus",
            "close-window", "fatal-error", "show-error-exit", "shot", "expect-shot", "res", "object-select", "cursor-contract",
            "keyboard-contract", "input-object-contract", "graphics-modules", "mp3-scan", "id3-read", "sound-bank-contract"};
    static const char* const kPrefixes[] = {"gui-", "sdl-", "music-", "sample-", "samples-", "spatial-", "window-", "display-", "posted-"};
    for (const char* exact : kExact)
        if (name == exact) return true;
    for (const char* prefix : kPrefixes)
        if (name.rfind(prefix, 0) == 0) return true;
    return false;
}

// Probes of renderer-only state, refused headless wherever a directive reads them (`expect`, `await`...): headless keeps no map
// view block cache (every block view is one scratch buffer, game/map_view_blocks.cpp), so the `map-*` hashes read noise.
bool RenderOnlyProbe(const std::string& probe)
{
    return probe == "map-blocks" || probe.rfind("map-hash", 0) == 0;
}

int Arg(const Command& c, size_t i)
{
    return i < c.args.size() ? std::atoi(c.args[i].c_str()) : 0;
}

// step <n> [dt]: n game_api::Update frames back to back with a fixed frame time (dt seconds, default: the `fixed-dt` value or 1/60)
// with rendering only at state/save/GUI script changes, so settled gameplay runs as fast as the simulation allows.
// A resolution change sets Game::m_guiReloadPending: game_api::Render must clear it before another update touches the new GUI.
// The game at 8x and ~40 FPS steps 0.2 s per frame; measured in docs/testing/determinism.md, from ~1 s on guests visit less.
constexpr double kSafeGameDt = 0.25;

void RunSteps(const Command& c)
{
    int count = Arg(c, 0);
    double previous = FixedDt();
    double dt = c.args.size() > 1 ? std::atof(c.args[1].c_str()) : previous > 0 ? previous : 1.0 / 60;
    game::Game* game = Game();
    if (count <= 0 || dt <= 0 || !game || GameStateIndex() < 0) {
        OBL_LOG("autotest: FAILED step %d %g (needs a game state and positive count/dt)", count, dt);
        return;
    }
    using Clock = std::chrono::steady_clock;
    const Clock::time_point t0 = Clock::now();
    SetFixedDt(dt);
    int renders = 0;
    auto renderTransition = [&](bool transition) {
        if (transition || game->m_guiReloadPending) {
            // Use the normal render path (including state PostRender), without an extra simulation update.
            game::game_api::Render(static_cast<game::Game*>(g_gameSelf));
            ++renders;
        }
    };
    renderTransition(false);
    // Where the time of a step goes: the test helper (OnFrame), the game's update, transition renders and world-hash checkpoints.
    double onFrameMs = 0, updateMs = 0, renderMs = 0, hashMs = 0;
    Clock::time_point mark = t0;
    for (int i = 0; i < count; ++i) {
        game::GameState* state = game->m_currentState;
        // Loading a save can exit/re-enter WorldTest within one update, leaving the same state pointer.
        bool transition = state != game->m_nextState || game->m_loadSlot != -1;
        auto lap = [&] {
            const Clock::time_point now = Clock::now();
            double ms = std::chrono::duration<double, std::milli>(now - mark).count();
            mark = now;
            return ms;
        };
        lap();
        OnFrame();
        onFrameMs += lap();
        g_gameUpdate(g_gameSelf);
        updateMs += lap();
        renderTransition(transition || state != game->m_currentState);
        renderMs += lap();
        bool more = WorldHashStep();
        hashMs += lap();
        if (!more) break;
    }
    SetFixedDt(previous);
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    // World update: game dt = frame dt * World::m_gameSpeed, clamped to 4 s.
    world_data::World* world = World();
    double gameDt = world ? std::min(dt * world->m_gameSpeed, 4.0) : dt;
    OBL_LOG("autotest: step steps=%d dt=%g game-dt=%g game-seconds=%g renders=%d ms=%.1f steps-per-s=%.0f", count, dt, gameDt,
            count * gameDt, renders, ms, ms > 0 ? count * 1000.0 / ms : 0.0);
    OBL_LOG("autotest: step breakdown on-frame=%.1f update=%.1f render=%.1f world-hash=%.1f ms", onFrameMs, updateMs, renderMs, hashMs);
    if (gameDt > kSafeGameDt)
        OBL_LOG("autotest: step WARNING game-dt %g s per frame is above %g s: guests and event timers no longer behave like "
                "play (docs/testing/determinism.md)",
                gameDt, kSafeGameDt);
    g_skipOuterUpdate = true;
}

// Returns false when the script must pause (wait).
bool Execute(const Command& raw, uint32_t now)
{
    // `$key` takes a probe or report value at the moment the directive runs (testing/commands.h).
    Command c{raw.name, {}};
    for (const std::string& arg : raw.args) {
        std::string resolved, missing;
        if (!SubstituteValues(arg, resolved, missing)) {
            OBL_LOG("autotest: FAILED %s: no value for $%s", raw.name.c_str(), missing.c_str());
            return true;
        }
        c.args.push_back(std::move(resolved));
    }
    // convert-save and save-obl convert a whole save against the pack inside one frame (tens of seconds on a loaded PC).
    WatchdogLongCommand longCommand(c.name == "call-drive" || c.name == "convert-save" || c.name == "save-obl");
    if (c.name == "wait") {
        g_resumeAt = now + Arg(c, 0);
        g_resumeFrame = c.args.size() > 1 ? Frames() + Arg(c, 1) : 0;
        return false;
    }
    if (c.name == "move") {
        platform::SetSyntheticCursor(Arg(c, 0), Arg(c, 1));
    } else if (c.name == "down" || c.name == "up") {
        platform::InjectMouseButton(Arg(c, 0), c.name == "down");
    } else if (c.name == "shot") {
        g_pendingShot = c.args.empty() ? "shot" : c.args[0];
    } else if (c.name == "res") {
        auto* gui = reinterpret_cast<ui::GuiManager*>(game::GetGuiManager());
        if (gui)
            gui->RequestResolution(Arg(c, 0), Arg(c, 1));
        else
            OBL_LOG("autotest: res - no GUI manager yet");
    } else if (c.name == "log") {
        std::string text;
        for (const auto& a : c.args) text += a + " ";
        OBL_LOG("autotest: %s", text.c_str());
    } else if (c.name == "step") {
        RunSteps(c);
        g_resumeAt = 0;
        return false;
    } else if (c.name == "quit") {
        OBL_LOG("autotest: summary passed=%d failed=%d", PassCount(), FailureCount());
        OBL_LOG("autotest: completed");
        g_completed = true;
        CloseGame();
    } else if (ExecuteDeterminismCommand(c.name, c.args)) {
    } else if (ExecuteFlowCommand(c.name, c.args)) {
    } else if (ExecuteAssertionCommand(c.name, c.args)) {
    } else if (ExecuteCommandLayerCommand(c.name, c.args)) {
    } else if (ExecuteWorldStateCommand(c.name, c.args)) {
    } else if (ExecuteWorldCreatePropsCommand(c.name, c.args)) {
    } else if (ExecuteWorldCreateBuildingCommand(c.name, c.args)) {
    } else if (ExecuteProfileCommand(c.name, c.args)) {
    } else if (ExecuteWorldHashCommand(c.name, c.args)) {
    } else if (ExecuteRecordCommand(c.name, c.args)) {
    } else if (ExecuteWorldParamsCommand(c.name, c.args)) {
    } else if (ExecuteGuiDumpCommand(c.name, c.args)) {
    } else if (ExecuteGuiControlCommand(c.name, c.args)) {
    } else if (ExecuteGuiCaptureCommand(c.name, c.args)) {
    } else if (ExecuteWorldDumpCommand(c.name, c.args)) {
    } else if (ExecuteBinarySerialCommand(c.name, c.args)) {
    } else if (ExecuteSavegameManagersCommand(c.name, c.args)) {
    } else if (ExecuteBuildingEventCommand(c.name, c.args)) {
    } else if (ExecuteGuestDesireCommand(c.name, c.args)) {
    } else if (ExecuteCommodityProviderCommand(c.name, c.args)) {
    } else if (ExecuteMp3ScanCommand(c.name, c.args)) {
    } else if (ExecuteImageLoadCommand(c.name, c.args)) {
    } else if (ExecuteMusicCommand(c.name, c.args)) {
    } else if (ExecuteSamplesCommand(c.name, c.args)) {
    } else if (ExecuteCallCounterCommand(c.name, c.args)) {
    } else if (ExecutePowerStartupCommand(c.name, c.args)) {
    } else if (ExecuteBreakdownCommand(c.name, c.args)) {
    } else if (ExecuteBuildingUpdateCommand(c.name, c.args)) {
    } else if (ExecuteRepairCommand(c.name, c.args)) {
    } else if (ExecuteCleaningCommand(c.name, c.args)) {
    } else if (ExecuteFeedbackCommand(c.name, c.args)) {
    } else if (ExecutePresenterCommand(c.name, c.args)) {
    } else if (ExecuteDisplayCommand(c.name, c.args)) {
    } else if (ExecuteCursorCommand(c.name)) {
    } else if (ExecuteCrashCommand(c.name)) {
    } else if (ExecuteExpectedExitCommand(c.name, c.args)) {
    } else if (weather::ExecuteCommand(c.name, c.args)) {
    } else if (ui::ExecuteMapRestoreCommand(c.name, c.args)) {
    } else {
        OBL_LOG("autotest: FAILED unrecognized command '%s'", c.name.c_str());
    }
    return true;
}

void RunScript()
{
    uint32_t now = os::TickMs();
    if (now < g_resumeAt || Frames() < g_resumeFrame) return;
    while (g_next < g_commands.size()) {
        if (g_commands[g_next].name == "await") {
            // The current time, not the frame's start: commands before it in this frame (convert-save, load-obl-save) can take
            // a minute on a loaded PC, and that time must not count against the await's timeout.
            AwaitResult result = StepAwait(g_commands[g_next].args, os::TickMs());
            if (result == AwaitResult::Waiting) return;
            if (result == AwaitResult::Failed) {
                // Everything after a missed condition would run against the wrong state.
                OBL_LOG("autotest: aborted summary passed=%d failed=%d", PassCount(), FailureCount());
                g_next = g_commands.size();
                CloseGame();
                return;
            }
            ++g_next;
            continue;
        }
        if (g_commands[g_next].name == "expect-shot") {
            if (!StepExpectShot(g_commands[g_next].args)) return;
            ++g_next;
            continue;
        }
        if (g_commands[g_next].name == "replay") {
            const ReplayProgress progress = StepReplay(g_commands[g_next].args);
            if (progress.kind == ReplayProgress::Kind::Waiting) return;
            if (progress.kind == ReplayProgress::Kind::Steps) {
                RunSteps({"step", {std::to_string(progress.steps)}});
                return;
            }
            ++g_next;
            continue;
        }
        if (g_commands[g_next].name == "event-await") {
            if (!AwaitBuildingEventState(g_commands[g_next].args)) return;
            ++g_next;
            continue;
        }
        const Command& c = g_commands[g_next++];
        if (!Execute(c, now)) return;
    }
}

void WrappedGameUpdate(void* gameState)
{
    g_gameSelf = gameState;
    OnFrame();
    RunScript();
    if (g_skipOuterUpdate) {
        g_skipOuterUpdate = false;
        return;
    }
    g_gameUpdate(gameState);
}

void OnPresent(void* renderer)
{
    auto* rendererBytes = static_cast<uint8_t*>(renderer);
    // The back buffer is only copied when a frame is wanted.
    if (g_pendingShot.empty() && !GoldenShotPending()) return;
    int width = *reinterpret_cast<int*>(rendererBytes + 4);
    int height = *reinterpret_cast<int*>(rendererBytes + 8);
    uint16_t* bits = ui::renderer::GetBackBufferBits(rendererBytes);
    OnPresentGolden(rendererBytes[0] ? bits : nullptr, width, height, width);
    if (g_pendingShot.empty()) return;
    Text16 path = g_captureDir + Text16(g_pendingShot.begin(), g_pendingShot.end()) + u".png";
    bool ok = bits && rendererBytes[0] && WritePngRgb555(WidePath(path).c_str(), bits, width, height, width);
    OBL_LOG("autotest: shot %s %dx%d %s", g_pendingShot.c_str(), width, height, ok ? "saved" : "FAILED");
    g_pendingShot.clear();
}

bool LoadScript(const std::filesystem::path& file)
{
    const std::string path = PathUtf8(file);
    std::ifstream in(file);
    if (!in) {
        OBL_LOG("autotest: cannot open script %s", path.c_str());
        return false;
    }
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        std::istringstream words(line);
        Command c;
        if (!(words >> c.name) || c.name[0] == '#') continue;
        for (std::string a; words >> a;) c.args.push_back(a);
        if (config::Get().headless && RefusedHeadless(c.name)) {
            g_refused.push_back("`" + c.name + "` (line " + std::to_string(lineNumber) + ")");
            continue;
        }
        if (config::Get().headless && !c.args.empty() && RenderOnlyProbe(c.args[0])) {
            g_refused.push_back("`" + c.name + " " + c.args[0] + "` (line " + std::to_string(lineNumber) + ")");
            continue;
        }
        // A GuiButton takes a press only when idle and hovered; after one it fires 0.05 s and recovers another 0.05 s later on the
        // game clock (gui_controls.h ButtonTick). The wall-clock waits alone left a loaded PC 1-3 frames per click at fixed-dt
        // 0.02, so a second click on the same button got lost; 9 frames per click cover the 0.1 s at that step.
        static const std::string kClickFrames[] = {"3", "6"};
        if (c.name == "click" || c.name == "click-client") {
            // click x y [right] -> move, press, hold briefly, release
            int button = c.args.size() > 2 && c.args[2] == "right" ? 1 : 0;
            g_commands.push_back({c.name == "click-client" ? "move-client" : "move", {c.args.at(0), c.args.at(1)}});
            g_commands.push_back({"wait", {"50", kClickFrames[0]}});
            g_commands.push_back({"down", {std::to_string(button)}});
            g_commands.push_back({"wait", {"100", kClickFrames[1]}});
            g_commands.push_back({"up", {std::to_string(button)}});
            continue;
        }
        if (c.name == "gui-click") {
            // gui-click <dialog> <selector...> [right]: the widget is looked up when the script reaches it, so gui-move
            // resolves the position and the usual press/release follows
            int button = !c.args.empty() && c.args.back() == "right" ? 1 : 0;
            if (button) c.args.pop_back();
            g_commands.push_back({"gui-move", c.args});
            g_commands.push_back({"wait", {"50", kClickFrames[0]}});
            g_commands.push_back({"down", {std::to_string(button)}});
            g_commands.push_back({"wait", {"100", kClickFrames[1]}});
            g_commands.push_back({"up", {std::to_string(button)}});
            continue;
        }
        g_commands.push_back(c);
    }
    // Nothing runs: the rest of the script would test a state the refused directives never set up.
    if (!g_refused.empty()) g_commands.clear();
    OBL_LOG("autotest: %zu commands from %s", g_commands.size(), path.c_str());
    return true;
}

// A script list (`<name>.list`, headless only): one script path per line, `#` comments, relative paths from the list's directory.
bool ReadScriptList(const std::filesystem::path& list)
{
    std::ifstream in(list);
    if (!in) {
        OBL_LOG("autotest: cannot open script list %s", PathUtf8(list).c_str());
        return false;
    }
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        const std::filesystem::path script =
                std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(line.data() + start), line.size() - start));
        g_scripts.push_back(script.is_absolute() ? script : std::filesystem::absolute(list).parent_path() / script);
    }
    if (g_scripts.empty()) OBL_LOG("autotest: script list %s names no script", PathUtf8(list).c_str());
    return !g_scripts.empty();
}

} // namespace

const std::filesystem::path& CurrentScript()
{
    static const std::filesystem::path none;
    return g_script < g_scripts.size() ? g_scripts[g_script] : none;
}

size_t ScriptCount()
{
    return g_scripts.size();
}

bool StartNextScript()
{
    if (g_script + 1 >= g_scripts.size()) return false;
    ++g_script;
    // What a fresh process starts with: the interpreter's own state and that of the modules whose results a script reads.
    g_commands.clear();
    g_next = 0;
    g_resumeAt = 0;
    g_resumeFrame = 0;
    g_pendingShot.clear();
    g_skipOuterUpdate = false;
    g_closeRequested = false;
    g_completed = false;
    g_refused.clear();
    ResetAssertions();
    ResetDeterminism();
    ResetFlow();
    ResetWorldHash();
    ResetReplay(g_scripts[g_script]);
    ResetBinarySerial();
    ResetBreakdowns();
    ResetBuildingEvents();
    game::ResetCommandLoopTotals();
    ResetCallCounters();
    if (!LoadScript(g_scripts[g_script])) {
        // Reported like a refused script: nothing runs, the run goes on with the next one.
        g_refused.push_back("the unreadable script " + PathUtf8(g_scripts[g_script]));
    }
    return true;
}

bool ScriptCloseRequested()
{
    return g_closeRequested;
}

bool ScriptCompleted()
{
    return g_completed;
}

int ReportRefusedDirectives()
{
    for (const std::string& refused : g_refused)
        OBL_LOG("autotest: FAILED headless refuses %s: input, frames, renderer state, GUI, audio and window need the hidden game",
                refused.c_str());
    if (!g_refused.empty()) {
        OBL_LOG("autotest: aborted summary passed=0 failed=%zu", g_refused.size());
        g_closeRequested = true;
    }
    return static_cast<int>(g_refused.size());
}

bool InitAutotest(const Text16& gameDirectory)
{
    SetCallDriver("ui.music-panel-drive", &DriveMusicPanel); // the fixture lives here, the counter in the music player
    const std::optional<std::string> path = os::GetEnv("OBL_SCRIPT"); // UTF-8
    if (!path || path->empty()) return true;
    const std::filesystem::path script = Utf8Path(*path);
    if (script.extension() == ".list") {
        if (!config::Get().headless) {
            OBL_LOG("autotest: a script list runs only headless (--headless %s)", path->c_str());
            return false;
        }
        if (!ReadScriptList(script)) return false;
    } else {
        g_scripts.push_back(script);
    }
    g_script = 0;
    if (!LoadScript(g_scripts[0])) return false;
    g_captureDir = gameDirectory + u"captures\\";
    std::error_code error;
    std::filesystem::create_directory(WidePath(g_captureDir), error);
    InitGolden(gameDirectory, g_captureDir);
    InitWorldHash(gameDirectory);
    InitReplay(gameDirectory);
    ResetReplay(g_scripts[0]); // a list's first script, not the list
    InitWorldParamsDump(gameDirectory);
    InitGuiDump(gameDirectory);
    InitWorldDump(gameDirectory);

    RegisterAssertions();
    if (!OBL_WINDOWS_ONLY(InitCrashDiagnostics())) return false;
    game::SetGameRenderPresentObserver(&OnPresent);
    return InitDeterminism() && RegisterBuildingEventTrace() && RegisterGuestDesireTrace() && RegisterCommodityProviderTrace() &&
            RegisterGuiImageBarFrame() && OBL_WINDOWS_ONLY(RegisterDisplayTrace()) && OBL_WINDOWS_ONLY(RegisterCursorTrace()) &&
            game::WrapGameUpdate(&WrappedGameUpdate, &g_gameUpdate);
}

} // namespace obl::test
