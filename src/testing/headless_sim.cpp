#include "core/char16.h"
#include "headless_sim.h"
#include "game/game/game_api.h"

#include "assertions.h"
#include "autotest.h"
#include "bot_host.h"
#include "chaos_host.h"
#include "building_events.h"
#include "game/game/command_recorder.h"
#include "core/config.h"
#include "platform/windows_only.h"
#include "platform/test_tools/crash.h"
#include "determinism.h"
#include "flow.h"
#include "game/game/game_accessors.h"
#include "game/game/command_loop.h"
#include "game/game/game.h"
#include "core/log.h"
#include "core/os.h"
#include "game/ui/save_slots.h"
#include "game/ui/save_slot_header.h"
#include "core/wide_path.h"
#include "world_hash.h"
#include "game/world/world_sections.h"

#include <SDL3/SDL_init.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>

namespace obl::test {
namespace {

// LoadGame enters WorldTest after 0.1 s of frame time, so a start takes a handful of frames; far more means it never settles.
constexpr int kMaxBootFrames = 10000;

// Results go to stdout (the caller of OpenBeach.exe) and to the game log.
void Report(const char* format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    std::printf("obl-sim: %s\n", line);
    std::fflush(stdout);
    OBL_LOG("obl-sim: %s", line);
}

double Milliseconds(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// Beats the hang watchdog like a script frame (chaos_host.cpp, `Update`).
void Update(game::Game& game)
{
    WatchdogBeat();
    game::game_api::Update(&game);
}

void Render(game::Game& game)
{
    game::game_api::Render(&game);
}

std::optional<command::Recording> LoadReplay()
{
    const Text16& path = config::Get().headlessReplay;
    std::ifstream in(WidePath(path), std::ios::binary);
    if (!in) {
        Report("FAILED cannot read the replay file %s", WideUtf8(path).c_str());
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto recording = command::ReadReplay(text, &error);
    if (!recording) Report("FAILED replay %s: %s", WideUtf8(path).c_str(), error.c_str());
    return recording;
}

// `start save <path>`: an obl.save loads through the bridge in memory (save-load-bridge.md), an old `.dat` from its bytes (the
// slot list reads only obl.save since save-switch.md); neither needs a slot copy.
class SaveStart {
public:
    // A relative path is relative to the game directory (the working directory of the game).
    bool Load(const std::string& path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            Report("FAILED start save %s: no such file", path.c_str());
            return false;
        }
        if (IsOblSaveFile(path)) {
            if (LoadOblSave(path)) return true;
            Report("FAILED start save %s: the obl.save bridge refused it", path.c_str());
            return false;
        }
        if (LoadOldSaveFile(path)) return true;
        Report("FAILED start save %s: cannot read it", path.c_str());
        return false;
    }
};

// One test script on the headless loop: the interpreter (autotest.cpp) runs at the start of every game_api::Update as in the hidden
// game. game_api::Render runs only where a state switch, a save load or a GUI reload needs its path (the rule of the script's `step`),
// so the frames between are pure simulation. Returns the script's exit code.
int RunOneScript(game::Game& game)
{
    const auto started = std::chrono::steady_clock::now();
    Report("script %s", PathUtf8(CurrentScript()).c_str());
    const int refused = ReportRefusedDirectives();
    int frames = 0;
    int renders = 0;
    while (!ScriptCloseRequested()) {
        const game::GameState* state = game.m_currentState;
        // Loading a save can leave and re-enter WorldTest within one update, keeping the same state pointer.
        const bool transition = state != game.m_nextState || game.m_loadSlot != -1;
        Update(game);
        ++frames;
        if (transition || state != game.m_currentState || game.m_guiReloadPending) {
            Render(game);
            ++renders;
        }
    }
    const double ms = Milliseconds(started, std::chrono::steady_clock::now());
    const bool completed = ScriptCompleted();
    Report("end frames=%d renders=%d passed=%d failed=%d refused=%d completed=%d ms=%.0f sdl-subsystems=%#x", frames, renders, PassCount(),
            FailureCount(), refused, completed, ms, SDL_WasInit(0));
    Report("%s", completed && !FailureCount() ? "PASS" : "FAILED");
    if (!completed) return kExitHeadlessFailed;
    return FailureCount() ? kExitHeadlessMismatch : 0;
}

// Between two scripts of a list: back to the Intro state a fresh process runs its first script in (its exit destroys the world,
// LeaveGame), so the next script's `boot-gameplay` builds its world the way it would at start-up.
void LeaveForNextScript(game::Game& game)
{
    game::game_api::SetNextState(static_cast<int>(game::GameStateId::Intro));
    Update(game);
    Render(game);
    // The game clock from zero as after game_api::Create: a world stores the clock time it was built at (World::m_gameStartTime), so a
    // clock running on from the last script gave the same simulation a different hash.
    game::game_api::RestartClock();
}

// The scripts of the run (one, or a `.list`) one after another; with several, a summary line after their own lines. Exit code: 5 when
// any script did not reach `quit`, else 6 when any had FAILED lines, else 0.
int RunScript(game::Game& game)
{
    const auto started = std::chrono::steady_clock::now();
    const size_t count = ScriptCount();
    int passed = 0, failed = 0, aborted = 0;
    for (size_t i = 0;; ++i) {
        const int code = RunOneScript(game);
        passed += code == 0;
        failed += code == kExitHeadlessMismatch;
        aborted += code == kExitHeadlessFailed;
        if (i + 1 >= count) break;
        LeaveForNextScript(game);
        StartNextScript();
    }
    if (count > 1)
        Report("scripts=%zu passed=%d failed=%d aborted=%d ms=%.0f", count, passed, failed, aborted,
                Milliseconds(started, std::chrono::steady_clock::now()));
    if (aborted) return kExitHeadlessFailed;
    return failed ? kExitHeadlessMismatch : 0;
}

// Every differing section of a mismatching checkpoint (docs/testing/replays.md, "Diagnosing a mismatch").
void ReportSections(uint32_t tick, const command::Checkpoint& expected, const std::string& text)
{
    const SectionDigests actual = DigestSections(text);
    if (!expected.sections) {
        Report("@%u sections=unrecognized actual=%s", tick, FormatSectionDigests(actual).c_str());
        return;
    }
    const auto differences = DiffSections(*expected.sections, actual);
    Report("@%u sections=%s", tick, SectionNames(differences).c_str());
    for (const SectionDifference& difference : differences)
        Report("@%u section %s expected=%s actual=%s", tick, std::string(SectionName(difference.section)).c_str(),
                FormatSectionDigest(difference.expected).c_str(), FormatSectionDigest(difference.actual).c_str());
}

int RunReplay(game::Game& game)
{
    const auto started = std::chrono::steady_clock::now();
    const std::optional<command::Recording> recording = LoadReplay();
    if (!recording) return kExitHeadlessFailed;
    const command::ReplayHeader& header = recording->header;
    const bool fromSave = header.startKind == command::StartKind::Save;
    const auto start = fromSave ? std::nullopt : command::ParseStartScenario(header.start);
    if (!fromSave && !start) {
        Report("FAILED start scenario %s: only `start scenario s<n>|sandbox<n>` or `start save <path>` is supported", header.start.c_str());
        return kExitHeadlessFailed;
    }
    if (header.hashSchema != kWorldHashSchema) {
        Report("FAILED hash-schema %d (this build hashes schema %d)", header.hashSchema, kWorldHashSchema);
        return kExitHeadlessFailed;
    }
    Report("start %s%s seed=%u fixed-dt=%g end=%u commands=%zu checkpoints=%zu", fromSave ? "save " : "", header.start.c_str(), header.seed,
            header.fixedDt, recording->endTick, recording->commands.size(), recording->checkpoints.size());

    // The order of a script's `seed`, `fixed-dt`, `boot-gameplay`: the frames of the start already run at the fixed frame time.
    if (!SeedGameRand(header.seed)) {
        Report("FAILED seed (RNG tape active)");
        return kExitHeadlessFailed;
    }
    SetFixedDt(header.fixedDt);
    SaveStart save;
    if (fromSave) {
        if (!save.Load(header.start)) return kExitHeadlessFailed;
    } else {
        BootNewGame(game, start->sandbox, start->island);
    }
    int bootFrames = 0;
    for (; bootFrames < kMaxBootFrames && !WorldSettled(); ++bootFrames) Update(game);
    if (!WorldSettled()) {
        Report("FAILED the world did not settle after %d frames", bootFrames);
        return kExitHeadlessFailed;
    }
    const auto booted = std::chrono::steady_clock::now();
    Report("boot frames=%d ms=%.0f", bootFrames, Milliseconds(started, booted));

    const game::CommandLoopStats before = game::GetCommandLoopStats();
    command::ReplayCursor cursor(*recording);
    int mismatches = 0;
    int checkpoints = 0;
    std::string lastHash;
    for (uint32_t tick = 0;; ++tick) {
        const command::Checkpoint* expected = cursor.ExpectedCheckpoint(tick);
        const bool done = cursor.Done(tick);
        if (expected || tick == 0 || done) {
            WorldHash hash;
            const char* error = nullptr;
            if (!HashWorld(hash, error)) {
                Report("FAILED world hash at tick %u: %s", tick, error);
                return kExitHeadlessFailed;
            }
            ++checkpoints;
            lastHash = hash.hash;
            // OBL_HEADLESS_DUMP=<dir> keeps every checkpoint's snapshot text, so two builds can be diffed field by field.
            if (const char* dump = std::getenv("OBL_HEADLESS_DUMP")) {
                std::ofstream out(std::filesystem::path(dump) / ("tick-" + std::to_string(tick) + ".tsv"), std::ios::binary);
                out << hash.text;
            }
            if (expected) {
                char want[17];
                std::snprintf(want, sizeof(want), "%016llx", static_cast<unsigned long long>(expected->hash));
                const bool match = hash.hash == want;
                mismatches += !match;
                Report("@%u hash=%s fields=%zu expected=%s %s", tick, hash.hash.c_str(), hash.fields, want, match ? "match" : "MISMATCH");
                if (!match) ReportSections(tick, *expected, hash.text);
            } else {
                Report("@%u hash=%s fields=%zu", tick, hash.hash.c_str(), hash.fields);
            }
        }
        if (done) break;
        for (command::Command& command : cursor.Due(tick)) game::SubmitCommand(std::move(command));
        Update(game);
        // Every game_api::Update of settled gameplay is one tick; anything else (a state switch, a lost world) breaks the replay's clock.
        const game::CommandLoopStats stats = game::GetCommandLoopStats();
        if (!stats.inWorld || stats.tick != tick + 1) {
            Report("FAILED tick %u left gameplay (in-world=%d tick=%u)", tick, stats.inWorld, stats.tick);
            return kExitHeadlessFailed;
        }
    }

    const game::CommandLoopStats after = game::GetCommandLoopStats();
    const double ms = Milliseconds(booted, std::chrono::steady_clock::now());
    Report("end ticks=%u hash=%s checkpoints=%d mismatches=%d applied=%u rejected=%u ms=%.0f ticks-per-s=%.0f sdl-subsystems=%#x",
            recording->endTick, lastHash.c_str(), checkpoints, mismatches, after.applied - before.applied, after.rejected - before.rejected,
            ms, ms > 0 ? recording->endTick * 1000.0 / ms : 0.0, SDL_WasInit(0));
    Report("%s", mismatches ? "FAILED" : "PASS");
    return mismatches ? kExitHeadlessMismatch : 0;
}

} // namespace

bool InitHeadless()
{
    platform::SetHeadlessDriver(&RunHeadless);
    // A script run gets the determinism wrappers and the crash report from the interpreter (InitAutotest). A chaos run hunts crashes, so
    // it gets the report too; replays and bot runs stay without (ctest headless_sim pins the Windows calls of a replay). The Linux
    // obl-sim has its own fault handler.
    if (!config::Get().headless || config::Get().headlessScript) return true;
#ifndef OBL_LINUX_HOST
    if (IsChaosRunPath(config::Get().headlessReplay) && !OBL_WINDOWS_ONLY(InitCrashDiagnostics())) return false;
#endif
    return InitDeterminism();
}

int RunHeadless()
{
    game::Game* game = game::GetGame();
    if (!game) return kExitHeadlessFailed;
    if (config::Get().headlessScript) return RunScript(*game);
    const Text16& path = config::Get().headlessReplay;
    if (IsChaosRunPath(path)) return RunChaos();
    return IsBotRunPath(path) ? RunBot(*game) : RunReplay(*game);
}

} // namespace obl::test
