#include "core/char16.h"
#include "replay.h"

#include "game/game/command_recorder.h"
#include "determinism.h"
#include "flow.h"
#include "building_events.h"
#include "game/ui/save_slots.h"
#include "game/ui/save_slot_header.h"
#include "game/game/command_loop.h"
#include "core/log.h"
#include "world_hash.h"
#include "game/world/world_sections.h"
#include "core/wide_path.h"
#include "core/os.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace obl::test {
namespace {

constexpr uint32_t kDefaultCheckpointEvery = 100;
// Step frames per script frame: a long replay returns to the frame loop between batches, so the hang watchdog sees progress.
constexpr uint32_t kStepBatch = 1000;

std::filesystem::path g_gameDirectory;
std::filesystem::path g_scriptDirectory;

// An obl.save loads through the bridge in memory (save-load-bridge.md), an old `.dat` from its bytes (the slot list reads only
// obl.save since save-switch.md); neither needs a slot copy.
struct SaveStart {
    bool Load(const std::string& path) { return IsOblSaveFile(path) ? LoadOblSave(path) : LoadOldSaveFile(path); }
};
std::optional<SaveStart> g_saveStart;

std::filesystem::path Resolve(const std::filesystem::path& base, const std::string& file)
{
    const std::filesystem::path path(file);
    return path.is_absolute() ? path : base / path;
}

std::string Hex(uint64_t value)
{
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

// One record or replay at a time. Armed waits for the next world; Running follows its ticks from 0.
class Session final : public game::TickListener {
public:
    enum class Phase : uint8_t { Idle, Armed, Running, Failed };

    void Record(std::filesystem::path file, command::ReplayHeader header, uint32_t every)
    {
        *this = Session{};
        m_recording = true;
        m_file = std::move(file);
        m_recorder.emplace(std::move(header));
        m_every = every;
        Arm();
    }

    void Replay(std::filesystem::path file, command::Recording recording, bool allowMismatch)
    {
        *this = Session{};
        m_file = std::move(file);
        m_replay = std::move(recording);
        m_cursor.emplace(m_replay);
        m_allowMismatch = allowMismatch;
        Arm();
    }

    void OnWorldStart() override
    {
        if (m_phase == Phase::Armed) m_phase = Phase::Running;
    }

    void OnWorldLeft(uint32_t tick) override
    {
        if (m_phase == Phase::Running) Fail("the world was left at tick " + std::to_string(tick) + " before the end");
    }

    void BeginTick(uint32_t tick, std::vector<command::Command>& commands) override
    {
        if (m_phase != Phase::Running) return;
        m_nextTick = tick + 1;
        if (m_recording) {
            if (tick % m_every == 0) AddCheckpoint(tick);
            return;
        }
        if (!commands.empty()) {
            // A replay owns the input: anything else submitted (a script, the GUI) would make the run differ from the recording.
            OBL_LOG("autotest: replay ignored %zu submitted command(s) at tick %u", commands.size(), tick);
            m_ignored += static_cast<uint32_t>(commands.size());
        }
        commands = m_cursor->Due(tick);
        if (const command::Checkpoint* expected = m_cursor->ExpectedCheckpoint(tick)) Compare(*expected);
    }

    void OnCommand(uint32_t tick, const command::Command& command, bool applied) override
    {
        if (m_phase != Phase::Running) return;
        ++(applied ? m_applied : m_rejected);
        if (m_recording && !m_recorder->Record(tick, command)) Fail("command at tick " + std::to_string(tick) + " out of order");
    }

    // `record end`: the last checkpoint at the current tick, then the file.
    void EndRecording()
    {
        const game::CommandLoopStats stats = game::GetCommandLoopStats();
        if (!m_recording || m_phase != Phase::Running) {
            OBL_LOG("autotest: FAILED record end (no recording is running%s)", m_phase == Phase::Failed ? ": it failed earlier" : "");
            Stop();
            return;
        }
        if (stats.pending) {
            // They would run at tick m_nextTick, which the recording no longer covers.
            Fail(std::to_string(stats.pending) + " submitted command(s) have not run yet: step once more before `record end`");
            return;
        }
        const uint32_t end = m_nextTick;
        if (!AddCheckpoint(end)) return;
        if (!m_recorder->Finish(end)) {
            Fail("cannot end at tick " + std::to_string(end));
            return;
        }
        const command::Recording& result = m_recorder->Result();
        std::error_code error;
        if (m_file.has_parent_path()) std::filesystem::create_directories(m_file.parent_path(), error);
        std::ofstream out(m_file, std::ios::binary);
        out << command::WriteReplay(result);
        out.close();
        if (!out) {
            Fail("cannot write " + m_file.string());
            return;
        }
        OBL_LOG("autotest: record ticks=%u commands=%zu checkpoints=%zu applied=%u rejected=%u file=%s", end, result.commands.size(),
                result.checkpoints.size(), m_applied, m_rejected, m_file.string().c_str());
        Stop();
    }

    // The replay directive's per-frame state machine.
    ReplayProgress Progress()
    {
        if (m_phase == Phase::Failed) return Finish();
        if (m_phase != Phase::Running) return {ReplayProgress::Kind::Waiting};
        if (m_nextTick < m_replay.endTick) return {ReplayProgress::Kind::Steps, std::min(m_replay.endTick - m_nextTick, kStepBatch)};
        if (const command::Checkpoint* expected = m_cursor->ExpectedCheckpoint(m_replay.endTick)) Compare(*expected);
        return Finish();
    }

    bool Active() const { return m_phase == Phase::Armed || m_phase == Phase::Running; }
    bool Recording() const { return m_recording; }

private:
    void Arm()
    {
        m_phase = Phase::Armed;
        game::SetTickListener(this);
    }

    void Stop()
    {
        game::SetTickListener(nullptr);
        m_phase = Phase::Idle;
        g_saveStart.reset();
    }

    void Fail(const std::string& why)
    {
        OBL_LOG("autotest: FAILED %s %s: %s", m_recording ? "record" : "replay", m_file.filename().string().c_str(), why.c_str());
        game::SetTickListener(nullptr);
        m_phase = Phase::Failed;
        g_saveStart.reset();
    }

    bool AddCheckpoint(uint32_t tick)
    {
        const auto checkpoint = CaptureTickCheckpoint(tick);
        if (!checkpoint) {
            Fail("no world hash at tick " + std::to_string(tick));
            return false;
        }
        if (!m_recorder->AddCheckpoint(tick, checkpoint->hash, DigestSections(checkpoint->text))) {
            Fail("checkpoint at tick " + std::to_string(tick) + " out of order");
            return false;
        }
        return true;
    }

    void Compare(const command::Checkpoint& expected)
    {
        ++m_checked;
        const uint32_t tick = expected.tick;
        const auto actual = CaptureTickCheckpoint(tick);
        if (actual && actual->hash == expected.hash) return;
        if (!m_mismatches++) m_firstMismatch = tick;
        if (!actual) {
            OBL_LOG("autotest: replay mismatch tick=%u expected=%s actual=none", tick, Hex(expected.hash).c_str());
            return;
        }
        // Every differing section with both digests, so the report shows the whole scope of a change, not only its first field. The
        // candidate's TSV stays in the world-hash directory; a replay run of a good build gives the reference side for
        // tools/world_hash_compare.ps1, which counts the differing records per section (docs/testing/replays.md).
        const SectionDigests sections = DigestSections(actual->text);
        const auto differences = expected.sections ? DiffSections(*expected.sections, sections) : std::vector<SectionDifference>{};
        OBL_LOG("autotest: replay mismatch tick=%u expected=%s actual=%s checkpoint=%u sections=%s", tick, Hex(expected.hash).c_str(),
                Hex(actual->hash).c_str(), actual->index, expected.sections ? SectionNames(differences).c_str() : "unrecognized");
        for (const SectionDifference& difference : differences)
            OBL_LOG("autotest: replay section tick=%u name=%s expected=%s actual=%s", tick,
                    std::string(SectionName(difference.section)).c_str(), FormatSectionDigest(difference.expected).c_str(),
                    FormatSectionDigest(difference.actual).c_str());
        // A checkpoint without digests (an older or hand-written file): the actual ones, ready to paste after the hash.
        if (!expected.sections) OBL_LOG("autotest: replay sections tick=%u actual=%s", tick, FormatSectionDigests(sections).c_str());
        m_differingSections += static_cast<uint32_t>(differences.size());
    }

    ReplayProgress Finish()
    {
        const bool failed = m_phase == Phase::Failed;
        game::SetTickListener(nullptr);
        m_phase = Phase::Idle;
        g_saveStart.reset();
        OBL_LOG("autotest: replay ticks=%u commands=%zu checkpoints=%u applied=%u rejected=%u ignored=%u mismatches=%u "
                "first-mismatch-tick=%lld differing-sections=%u file=%s",
                m_nextTick, m_replay.commands.size(), m_checked, m_applied, m_rejected, m_ignored, m_mismatches,
                m_mismatches ? static_cast<long long>(m_firstMismatch) : -1LL, m_differingSections, m_file.filename().string().c_str());
        if (!failed && (!m_mismatches || m_allowMismatch) && !m_ignored)
            OBL_LOG("autotest: PASS replay %s", m_file.filename().string().c_str());
        else if (!failed)
            OBL_LOG("autotest: FAILED replay %s (%u mismatching checkpoint(s), first at tick %u; %u ignored command(s))",
                    m_file.filename().string().c_str(), m_mismatches, m_firstMismatch, m_ignored);
        return {ReplayProgress::Kind::Done};
    }

    Phase m_phase = Phase::Idle;
    bool m_recording = false;
    std::filesystem::path m_file;
    // Recording
    std::optional<command::Recorder> m_recorder;
    uint32_t m_every = kDefaultCheckpointEvery;
    // Replay
    command::Recording m_replay;
    std::optional<command::ReplayCursor> m_cursor;
    bool m_allowMismatch = false;
    uint32_t m_checked = 0;
    uint32_t m_mismatches = 0;
    uint32_t m_firstMismatch = 0;
    uint32_t m_differingSections = 0; // summed over the mismatching checkpoints that have digests
    uint32_t m_ignored = 0;
    // Both
    uint32_t m_nextTick = 0; // ticks begun since the world started
    uint32_t m_applied = 0;
    uint32_t m_rejected = 0;
};

Session g_session;
bool g_replayStarted; // the current `replay` directive has started its session

// fixed-dt as the replay header stores it: a float whose shortest text reads back as the same double the script set.
std::optional<float> HeaderFixedDt()
{
    const double dt = FixedDt();
    const auto value = static_cast<float>(dt);
    if (dt <= 0 || std::strtod(command::FormatFloat(value).c_str(), nullptr) != dt) return std::nullopt;
    return value;
}

void StartRecording(const std::vector<std::string>& args)
{
    const bool save = args.size() >= 3 && args[1] == "save";
    const size_t options = save ? 3 : 1;
    uint32_t every = kDefaultCheckpointEvery;
    if (args.size() == options + 2 && args[options] == "every") {
        const auto& value = args[options + 1];
        const auto result = std::from_chars(value.data(), value.data() + value.size(), every);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) every = 0;
    } else if (args.size() != options) {
        every = 0;
    }
    if (!every) {
        OBL_LOG("autotest: FAILED record (expected `record <file> [save <path>] [every N]` with N > 0, or `record end`)");
        return;
    }
    const auto seed = LastSeed();
    const auto fixedDt = HeaderFixedDt();
    const int scenario = LastScenarioBoot();
    // The replay repeats `seed`, `fixed-dt` and `boot-gameplay` back to back, so the recording must start from exactly that.
    if (!seed || !fixedDt || (!save && !scenario) || GameRandTape().Calls() != 0) {
        OBL_LOG("autotest: FAILED record (needs `seed`, `fixed-dt <s>` and a scenario boot or save path, with no random "
                "draws since the seed)");
        return;
    }
    if (g_session.Active()) {
        OBL_LOG("autotest: FAILED record (a %s is already running)", g_session.Recording() ? "recording" : "replay");
        return;
    }
    command::ReplayHeader header;
    header.startKind = save ? command::StartKind::Save : command::StartKind::Scenario;
    header.start = save ? args[2] : std::to_string(scenario);
    header.seed = *seed;
    header.fixedDt = *fixedDt;
    header.hashSchema = kWorldHashSchema;
    const std::filesystem::path file = Resolve(g_gameDirectory, args[0]);
    if (save) {
        g_saveStart.emplace();
        if (!g_saveStart->Load(header.start)) {
            g_saveStart.reset();
            return;
        }
    }
    OBL_LOG("autotest: record armed start=%s seed=%u every=%u file=%s", header.start.c_str(), *seed, every, file.string().c_str());
    g_session.Record(file, std::move(header), every);
}

// Reads the file and repeats its start: seed, fixed-dt, boot-gameplay. False after a FAILED line.
bool StartReplay(const std::vector<std::string>& args)
{
    const bool allowMismatch = args.size() == 2 && args[1] == "allow-mismatch";
    if (args.empty() || args.size() > 2 || (args.size() == 2 && !allowMismatch)) {
        OBL_LOG("autotest: FAILED replay (expected `replay <file> [allow-mismatch]`)");
        return false;
    }
    if (g_session.Active()) {
        OBL_LOG("autotest: FAILED replay (a %s is already running)", g_session.Recording() ? "recording" : "replay");
        return false;
    }
    const std::filesystem::path file = Resolve(g_scriptDirectory, args[0]);
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    if (!in) {
        OBL_LOG("autotest: FAILED replay (cannot read %s)", file.string().c_str());
        return false;
    }
    std::string error;
    auto recording = command::ReadReplay(text.str(), &error);
    if (!recording) {
        OBL_LOG("autotest: FAILED replay %s: %s", file.filename().string().c_str(), error.c_str());
        return false;
    }
    const command::ReplayHeader& header = recording->header;
    const auto start = header.startKind == command::StartKind::Scenario ? command::ParseStartScenario(header.start) : std::nullopt;
    const bool save = header.startKind == command::StartKind::Save;
    if ((!save && !start) || header.hashSchema != kWorldHashSchema || header.fixedDt <= 0 || recording->endTick == 0) {
        OBL_LOG("autotest: FAILED replay %s (needs a scenario or save start, hash-schema %d, fixed-dt > 0 and at least one "
                "tick)",
                file.filename().string().c_str(), kWorldHashSchema);
        return false;
    }
    OBL_LOG("autotest: replay start=%s seed=%u ticks=%u file=%s", header.start.c_str(), header.seed, recording->endTick,
            file.string().c_str());
    ExecuteDeterminismCommand("seed", {std::to_string(header.seed)});
    ExecuteDeterminismCommand("fixed-dt", {command::FormatFloat(header.fixedDt)});
    if (save) {
        g_saveStart.emplace();
        if (!g_saveStart->Load(header.start)) {
            g_saveStart.reset();
            return false;
        }
    } else {
        ExecuteFlowCommand("boot-gameplay", {start->sandbox ? "sandbox" : "scenario", std::to_string(start->island)});
    }
    g_session.Replay(file, std::move(*recording), allowMismatch);
    return true;
}

} // namespace

void InitReplay(const Text16& gameDirectory)
{
    g_gameDirectory = WidePath(gameDirectory);
    const auto script = os::GetEnv("OBL_SCRIPT");
    g_scriptDirectory = script && !script->empty() ? std::filesystem::absolute(Utf8Path(*script)).parent_path() : g_gameDirectory;
}

void ResetReplay(const std::filesystem::path& script)
{
    // A session the last script left running ends with its world: the next script's boot fails it.
    g_scriptDirectory = std::filesystem::absolute(script).parent_path();
    g_replayStarted = false;
}

bool ExecuteRecordCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "record") return false;
    if (args.size() == 1 && args[0] == "end")
        g_session.EndRecording();
    else
        StartRecording(args);
    return true;
}

ReplayProgress StepReplay(const std::vector<std::string>& args)
{
    if (!g_replayStarted) {
        if (!StartReplay(args)) return {ReplayProgress::Kind::Done};
        g_replayStarted = true;
        return {ReplayProgress::Kind::Waiting};
    }
    const ReplayProgress progress = g_session.Progress();
    if (progress.kind == ReplayProgress::Kind::Done) g_replayStarted = false;
    return progress;
}

} // namespace obl::test
