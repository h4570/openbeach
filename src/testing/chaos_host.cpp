#include "core/char16.h"
#include "chaos_host.h"
#include "game/game/game_api.h"

#include "bot_host.h"
#include "building_events.h"
#include "game/bot/chaos.h"
#include "game/bot/chaos_findings.h"
#include "game/game/command_recorder.h"
#include "core/ascii_case.h"
#include "core/config.h"
#include "platform/test_tools/crash.h"
#include "determinism.h"
#include "flow.h"
#include "game/game/game_accessors.h"
#include "game/game/command_loop.h"
#include "headless_sim.h"
#include "game/game/game.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "core/log.h"
#include "core/wide_path.h"
#include "world_hash.h"
#include "game/world/world_sections.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>

namespace obl::test {
namespace {

// LoadGame enters WorldTest after 0.1 s of frame time; far more frames means the start or the reload never settles.
constexpr int kMaxBootFrames = 10000;
// The recorded run, relative to the game directory (the runner replays it in a fresh process).
constexpr const char* kRecordDirectory = "testlogs/chaos";

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

// Every headless host beats the hang watchdog once per update, as the script runner does per frame: a save load arms it (`LoadFromSave`),
// and a host that never beat was reported as hung 20 s after a reload in the middle of a healthy run.
void Update(game::Game& game)
{
    WatchdogBeat();
    game::game_api::Update(&game);
}

void Render(game::Game& game)
{
    game::game_api::Render(&game);
}

// World hashes taken and their time: the rejected-command check hashes around every command, which dominates a Debug run.
uint32_t g_hashes = 0;
double g_hashMs = 0.0;

bool MessageBalloonActive()
{
    return game::GetGuiManager()->m_balloonActive != 0;
}

std::optional<WorldHash> HashNow()
{
    const auto started = std::chrono::steady_clock::now();
    WorldHash hash;
    const char* error = nullptr;
    const bool ok = HashWorld(hash, error);
    ++g_hashes;
    g_hashMs += Milliseconds(started, std::chrono::steady_clock::now());
    if (!ok) return std::nullopt;
    return hash;
}

// The sections of a reloaded world that differ from the uninterrupted run, without `rng`: it counts rand() calls since the last seed,
// and the reload seeds the rand() state of the save point, which restarts the count.
std::vector<SectionDifference> ReloadDifferences(const SectionDigests& expected, const SectionDigests& actual)
{
    std::vector<SectionDifference> differences = DiffSections(expected, actual);
    std::erase_if(differences, [](const SectionDifference& difference) { return difference.section == WorldSection::Rng; });
    return differences;
}

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary) << text;
}

uint64_t HashValue(const std::string& hex)
{
    uint64_t value = 0;
    std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
    return value;
}

std::string ReadFile(const std::filesystem::path& path, bool& ok)
{
    std::ifstream in(path, std::ios::binary);
    ok = static_cast<bool>(in);
    return ok ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
}

// Watches the command loop: records every executed command (rejected ones too, a replay rejects them the same way) and checks that a
// rejected command left the world hash as it was. The hash before a command is the tick's start or the state after the previous
// command of the tick. A hash costs ~50 ms in Debug, so only the first `budget` rejections of each command name are checked (the run
// file's `check-rejected`); the others, and those where the world cannot be hashed (messages in delivery after an applied command),
// stay unchecked.
class Watch final : public game::TickListener {
public:
    command::Recorder* recorder = nullptr; // null while the reload replays the recorded commands
    uint32_t budget = 0;
    uint32_t checked = 0;
    uint32_t changed = 0; // rejected commands that changed the hash

    void OnWorldStart() override {}
    void OnWorldLeft(uint32_t) override {}
    void BeginTick(uint32_t, std::vector<command::Command>& commands) override
    {
        m_names.clear();
        for (const command::Command& command : commands) m_names.push_back(command::Name(command));
        m_next = 0;
        m_before = WantCheck() ? HashNow() : std::nullopt;
    }
    void OnCommand(uint32_t tick, const command::Command& command, bool applied) override
    {
        if (recorder) recorder->Record(tick, command);
        const std::string_view name = command::Name(command);
        if (m_next < m_names.size()) ++m_next;
        if (applied) {
            m_before = WantCheck() ? HashNow() : std::nullopt;
            return;
        }
        uint32_t& count = m_checks[name];
        if (!m_before || count >= budget) return;
        std::optional<WorldHash> after = HashNow();
        if (!after) return;
        ++count;
        ++checked;
        if (after->hash != m_before->hash) {
            ++changed;
            const auto differences = DiffSections(DigestSections(m_before->text), DigestSections(after->text));
            Report("FAILED @%u rejected command changed the world: %s (sections=%s)", tick, command::Format(command).c_str(),
                    SectionNames(differences).c_str());
        }
        m_before = std::move(after);
    }

private:
    // A command still to come this tick has check budget left: the state before it is worth hashing.
    bool WantCheck() const
    {
        for (size_t i = m_next; i < m_names.size(); ++i)
            if (const auto it = m_checks.find(m_names[i]); it == m_checks.end() || it->second < budget) return true;
        return false;
    }

    std::vector<std::string_view> m_names; // this tick's commands
    size_t m_next = 0; // the index of the next command of the tick
    std::map<std::string_view, uint32_t> m_checks; // checked rejections per command name
    std::optional<WorldHash> m_before;
};

// Steps until the world is settled gameplay, rendering after transition frames as a script run does (a save load passes through the
// main menu). False when it does not settle.
bool Settle(game::Game& game, int& frames)
{
    for (frames = 0; frames < kMaxBootFrames && !WorldSettled(); ++frames) {
        const game::GameState* state = game.m_currentState;
        const bool transition = state != game.m_nextState || game.m_loadSlot != -1;
        Update(game);
        if (transition || state != game.m_currentState || game.m_guiReloadPending) Render(game);
    }
    return WorldSettled();
}

// One tick of gameplay; false (with a FAILED line) when the update left it.
bool Tick(game::Game& game, uint32_t tick)
{
    Update(game);
    const game::CommandLoopStats stats = game::GetCommandLoopStats();
    if (stats.inWorld && stats.tick == tick + 1) return true;
    Report("FAILED tick %u left gameplay (in-world=%d tick=%u)", tick, stats.inWorld, stats.tick);
    return false;
}

struct SavePoint {
    uint32_t tick = 0;
    std::string file; // the obl.save the game wrote (game::WriteOblSave), with the state the old save format drops
    uint32_t randState = 0; // the game's rand() state at the save, for the reloaded world
};

// The save round trip (chaos-bot.md, "Invariants"): load the save taken at `save.tick`, give the reloaded world the rand() state of the
// uninterrupted run at that tick, play the recorded commands of the later ticks and compare every recorded checkpoint. Returns the number
// of mismatching checkpoints, or -1 when the reload could not run.
// `texts` holds the uninterrupted run's snapshot at every checkpoint; the first mismatch writes it and the reloaded one next to the
// recorded replay (`<record>-<tick>-run.tsv` / `-reload.tsv`) for tools/world_hash_compare.ps1.
int ReloadAndCompare(game::Game& game, const SavePoint& save, const command::Recording& run, const std::map<uint32_t, std::string>& texts,
        const std::filesystem::path& record)
{
    if (!LoadOblSave(save.file)) return -1;
    int frames = 0;
    if (!Settle(game, frames)) {
        Report("FAILED the reloaded world did not settle after %d frames", frames);
        return -1;
    }
    // The rest of the run, its ticks counted from the save (a load starts the command loop at tick 0 again).
    command::Recording rest;
    rest.endTick = run.endTick - save.tick;
    for (const command::TimedCommand& timed : run.commands)
        if (timed.tick >= save.tick) rest.commands.push_back({timed.tick - save.tick, timed.command});
    for (const command::Checkpoint& checkpoint : run.checkpoints)
        if (checkpoint.tick >= save.tick) rest.checkpoints.push_back({checkpoint.tick - save.tick, checkpoint.hash, checkpoint.sections});
    if (!SeedGameRand(save.randState)) {
        Report("FAILED seed (RNG tape active)");
        return -1;
    }
    command::ReplayCursor cursor(rest);
    int mismatches = 0;
    for (uint32_t tick = 0;; ++tick) {
        if (const command::Checkpoint* expected = cursor.ExpectedCheckpoint(tick)) {
            const std::optional<WorldHash> hash = HashNow();
            if (!hash) {
                Report("FAILED reload: no world hash at tick %u", save.tick + tick);
                return -1;
            }
            const auto differences = ReloadDifferences(*expected->sections, DigestSections(hash->text));
            // The first differing checkpoint names its sections and writes both snapshots; later ones only count (they follow from it).
            if (!differences.empty() && ++mismatches == 1) {
                const uint32_t at = save.tick + tick;
                const std::string stem = record.stem().string() + "-" + std::to_string(at);
                WriteText(record.parent_path() / (stem + "-reload.tsv"), hash->text);
                if (const auto text = texts.find(at); text != texts.end())
                    WriteText(record.parent_path() / (stem + "-run.tsv"), text->second);
                Report("FAILED reload @%u hash=%s expected=%016llx sections=%s snapshots=%s-run.tsv,-reload.tsv", at, hash->hash.c_str(),
                        static_cast<unsigned long long>(expected->hash), SectionNames(differences).c_str(), stem.c_str());
            }
        }
        if (cursor.Done(tick)) break;
        for (command::Command& command : cursor.Due(tick)) game::SubmitCommand(std::move(command));
        if (!Tick(game, tick)) return -1;
    }
    return mismatches;
}

} // namespace

bool IsChaosRunPath(Text16View path)
{
    constexpr Text16View kExtension = u".chaos";
    if (path.size() < kExtension.size()) return false;
    const Text16View tail = path.substr(path.size() - kExtension.size());
    return ascii::EqualsNoCase(tail, kExtension);
}

int RunChaos()
{
    game::Game& game = *game::GetGame();
    const auto started = std::chrono::steady_clock::now();
    const std::filesystem::path runPath = WidePath(config::Get().headlessReplay);
    bool readable = false;
    const std::string text = ReadFile(runPath, readable);
    std::string error;
    const std::optional<chaos::ChaosRun> run = readable ? chaos::ReadChaosRun(text, &error) : std::nullopt;
    if (!run) {
        Report("FAILED chaos run %s: %s", PathUtf8(runPath).c_str(), readable ? error.c_str() : "cannot read the file");
        return kExitHeadlessFailed;
    }
    const auto start = command::ParseStartScenario(run->start);
    // A fixed command list (a shrunk finding) replaces the generator; its own header and checkpoints are ignored.
    std::optional<command::Recording> fixed;
    if (!run->commands.empty()) {
        const std::filesystem::path listPath = runPath.parent_path() / Utf8Path(run->commands);
        const std::string listText = ReadFile(listPath, readable);
        fixed = readable ? command::ReadReplay(listText, &error) : std::nullopt;
        if (!fixed) {
            Report("FAILED commands %s: %s", PathUtf8(listPath).c_str(), readable ? error.c_str() : "cannot read the file");
            return kExitHeadlessFailed;
        }
    }
    Report("chaos start %s seed=%u fixed-dt=%g max-ticks=%u save-at=%s commands=%s", run->start.c_str(), run->seed, run->fixedDt,
            run->maxTicks, run->saveAt ? std::to_string(*run->saveAt).c_str() : "none",
            fixed ? run->commands.c_str() : (run->tricky.empty() ? "random" : run->tricky.c_str()));

    if (!SeedGameRand(run->seed)) {
        Report("FAILED seed (RNG tape active)");
        return kExitHeadlessFailed;
    }
    SetFixedDt(run->fixedDt);
    BootNewGame(game, start->sandbox, start->island);
    int bootFrames = 0;
    if (!Settle(game, bootFrames)) {
        Report("FAILED the world did not settle after %d frames", bootFrames);
        return kExitHeadlessFailed;
    }
    const auto booted = std::chrono::steady_clock::now();
    Report("boot frames=%d ms=%.0f", bootFrames, Milliseconds(started, booted));

    command::Recorder recorder({command::StartKind::Scenario, run->start, run->seed, run->fixedDt, kWorldHashSchema});
    Watch watch;
    watch.recorder = &recorder;
    watch.budget = run->checkRejected;
    game::SetTickListener(&watch);
    const game::CommandLoopStats before = game::GetCommandLoopStats();
    chaos::ChaosBot bot(run->seed);
    const std::unique_ptr<chaos::TrickyProbe> tricky = run->tricky.empty() ? nullptr : chaos::MakeTrickyProbe(run->tricky);
    std::optional<command::ReplayCursor> cursor;
    if (fixed) cursor.emplace(*fixed);
    std::vector<command::Command> commands;
    std::optional<SavePoint> save;
    bool saveTried = false;
    std::map<uint32_t, std::string> texts; // checkpoint snapshots, for the reload's mismatch report
    std::string endHash = "none";
    bool reachedEnd = true;
    uint32_t softSamples = 0, softFindings = 0;
    chaos::CashLedger cashLedger;
    const auto findingsPath = std::filesystem::path(kRecordDirectory) / (PathUtf8(runPath.stem()) + "-findings.tsv");
    std::error_code findingsError;
    std::filesystem::create_directories(findingsPath.parent_path(), findingsError);
    std::ofstream findingsFile(findingsPath);
    findingsFile << "tick\tfinding\n";
    uint32_t submitted = 0;
    for (uint32_t tick = 0;; ++tick) {
        const bool last = tick == run->maxTicks;
        if (tick % run->checkpointEvery == 0 || last || (run->saveAt && tick == *run->saveAt)) {
            ++softSamples;
            auto observations = chaos::SoftFindings(*game::GetWorld());
            const auto cash = cashLedger.Observe(*game::GetWorld());
            observations.insert(observations.end(), cash.begin(), cash.end());
            for (const auto& finding : observations) {
                ++softFindings;
                findingsFile << tick << '\t' << finding << '\n';
                Report("FINDING @%u %s", tick, finding.c_str());
            }
            const std::optional<WorldHash> hash = HashNow();
            if (hash) {
                recorder.AddCheckpoint(tick, HashValue(hash->hash), DigestSections(hash->text));
                endHash = hash->hash;
                if (run->saveAt && tick >= *run->saveAt) texts[tick] = hash->text;
            }
        }
        if (last) break;
        // The save waits for the first tick from save-at without a message balloon: a loaded world starts without one
        // (gui_manager_messages.h DropMessage), so a balloon scrolling at the save would show the next notice earlier after
        // the reload than in the uninterrupted run (FeedbackManager's lists, chaos s3 seed 3).
        if (run->saveAt && !saveTried && tick >= *run->saveAt && !MessageBalloonActive()) {
            saveTried = true;
            const std::string file = std::string(kRecordDirectory) + "/" + PathUtf8(runPath.stem()) + ".obl.save";
            if (!SaveObl(file))
                Report("FAILED save at tick %u: the world cannot be saved now", tick);
            else
                save = SavePoint{tick, file, chaos::RandStateAfter(run->seed, GameRandTape().Calls())};
        }
        commands.clear();
        if (cursor)
            commands = cursor->Due(tick);
        else if (tick % bot.ThinkInterval() == 0) {
            if (tricky) {
                tricky->Think(CaptureWorldView(tick), LiveQueries(), commands);
                for (const auto& finding : tricky->observations) {
                    ++softFindings;
                    findingsFile << tick << '\t' << finding << '\n';
                    Report("FINDING @%u %s", tick, finding.c_str());
                }
                tricky->observations.clear();
            } else
                bot.Think(CaptureWorldView(tick), LiveQueries(), commands);
        }
        for (command::Command& command : commands) {
            game::SubmitCommand(std::move(command));
            ++submitted;
        }
        if (!Tick(game, tick)) {
            reachedEnd = false;
            break;
        }
    }
    Report("soft samples=%u findings=%u file=%s", softSamples, softFindings, PathUtf8(findingsPath).c_str());
    game::SetTickListener(nullptr);
    const game::CommandLoopStats after = game::GetCommandLoopStats();
    const uint32_t ticks = after.tick;
    recorder.Finish(ticks);
    const double ms = Milliseconds(booted, std::chrono::steady_clock::now());

    // The run as a replay: the runner plays it in a fresh process (determinism), the shrinker cuts its commands.
    const std::filesystem::path record = std::filesystem::path(kRecordDirectory) / (PathUtf8(runPath.stem()) + ".replay");
    std::error_code ignored;
    std::filesystem::create_directories(record.parent_path(), ignored);
    std::ofstream(record, std::ios::binary) << command::WriteReplay(recorder.Result());

    std::string reload = "none";
    if (run->saveAt && reachedEnd) {
        if (!save) {
            reload = "unsaved";
        } else {
            Watch quiet; // records nothing; its own budget checks the reloaded world's rejections
            quiet.budget = run->checkRejected;
            game::SetTickListener(&quiet);
            const int mismatches = ReloadAndCompare(game, *save, recorder.Result(), texts, record);
            game::SetTickListener(nullptr);
            watch.changed += quiet.changed;
            watch.checked += quiet.checked;
            reload = mismatches < 0 ? "failed" : mismatches ? "MISMATCH" : "match";
        }
    }

    Report("end ticks=%u commands=%u applied=%u rejected=%u rejected-checked=%u rejected-changed=%u reload=%s hash=%s replay=%s hashes=%u "
           "hash-ms=%.0f ms=%.0f ticks-per-s=%.0f",
            ticks, submitted, after.applied - before.applied, after.rejected - before.rejected, watch.checked, watch.changed,
            reload.c_str(), endHash.c_str(), record.generic_string().c_str(), g_hashes, g_hashMs, ms, ms > 0 ? ticks * 1000.0 / ms : 0.0);
    const bool pass = reachedEnd && watch.changed == 0 && (reload == "none" || reload == "match");
    Report("%s", pass ? "PASS" : "FAILED");
    return pass ? 0 : kExitHeadlessMismatch;
}

} // namespace obl::test
