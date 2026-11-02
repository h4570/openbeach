#pragma once

// Recording of a command stream with world-hash checkpoints, its text file and the cursor a replay reads it with
// (docs/architecture/commands.md, "Replay file"). Pure code shared by the game and the headless simulation.

#include "game/game/command.h"
#include "game/world/world_sections.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obl::command {

inline constexpr int32_t kReplayFormatVersion = 1;

enum class StartKind : uint8_t {
    Scenario, // `start` is a scenario name
    Save, // `start` is a save file path
};

struct ReplayHeader {
    StartKind startKind = StartKind::Scenario;
    std::string start;
    uint32_t seed = 0;
    float fixedDt = 0.0f;
    int32_t hashSchema = 0; // world-hash schema of the checkpoints (docs/testing/world-hashes.md)
    bool operator==(const ReplayHeader&) const = default;
};

// A command applied at the start of `tick`.
struct TimedCommand {
    uint32_t tick = 0;
    Command command;
    bool operator==(const TimedCommand&) const = default;
};

// The world hash after `tick` ticks, before the commands of that tick, with its section digests when the recording has them
// (docs/testing/world-hashes.md, "Sections"; older and hand-written files leave them out).
struct Checkpoint {
    uint32_t tick = 0;
    uint64_t hash = 0;
    std::optional<SectionDigests> sections;
    bool operator==(const Checkpoint&) const = default;
};

struct Recording {
    ReplayHeader header;
    std::vector<TimedCommand> commands;
    std::vector<Checkpoint> checkpoints;
    uint32_t endTick = 0; // the replay runs ticks 0 .. endTick - 1
    bool operator==(const Recording&) const = default;
};

// Collects a recording and enforces its order: ticks never decrease, a checkpoint at tick T comes after every command of earlier
// ticks and before those of tick T, and Finish(T) comes after the commands of every tick below T. A call that breaks the order is
// refused (returns false, nothing recorded): in the game it means the caller's tick bookkeeping is wrong.
class Recorder {
public:
    explicit Recorder(ReplayHeader header) { m_recording.header = std::move(header); }

    bool Record(uint32_t tick, const Command& command);
    bool AddCheckpoint(uint32_t tick, uint64_t hash, std::optional<SectionDigests> sections = std::nullopt);
    bool Finish(uint32_t endTick);

    bool Finished() const { return m_finished; }
    const Recording& Result() const { return m_recording; }

private:
    Recording m_recording;
    std::optional<uint32_t> m_lastCommand;
    std::optional<uint32_t> m_lastCheckpoint;
    bool m_finished = false;
};

// The replay file text. WriteReplay of an unfinished recording still writes `end` with its endTick.
std::string WriteReplay(const Recording& recording);
// Parses and validates a replay file (same order rules as Recorder). On failure `error` says "line N: why".
std::optional<Recording> ReadReplay(std::string_view text, std::string* error = nullptr);

// The new game a `start scenario <name>` names: `s<n>` or `<n>` (what the script's `record` writes) = campaign scenario n,
// `sandbox<n>` = sandbox island n (both 1-based, what `boot-gameplay scenario|sandbox n` starts). Nothing for any other name.
struct StartScenario {
    bool sandbox = false;
    int32_t island = 0;
    bool operator==(const StartScenario&) const = default;
};
std::optional<StartScenario> ParseStartScenario(std::string_view name);

// Hands a recording's commands to the tick loop of a replay. The recording must outlive the cursor.
class ReplayCursor {
public:
    explicit ReplayCursor(const Recording& recording) : m_recording(&recording) {}

    // The commands of every tick up to and including `tick` not yet returned, in recorded order. A loop that visits each tick returns
    // exactly that tick's commands.
    std::vector<Command> Due(uint32_t tick);
    // The recorded hash of the checkpoint at `tick`, if there is one.
    std::optional<uint64_t> ExpectedHash(uint32_t tick) const;
    // The recorded checkpoint at `tick` (hash and section digests), null when there is none.
    const Checkpoint* ExpectedCheckpoint(uint32_t tick) const;
    bool Done(uint32_t tick) const { return tick >= m_recording->endTick; }

private:
    const Recording* m_recording;
    size_t m_next = 0;
};

} // namespace obl::command
