#pragma once

#include "core/char16.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace obl::test {

// The snapshot schema of the report lines and TSV files (docs/testing/world-hashes.md).
inline constexpr int32_t kWorldHashSchema = 3;

struct WorldHashStats {
    unsigned checkpoints; // checkpoint files written so far (next index)
    unsigned deferred; // periodic checkpoints moved to a later settled frame
    bool pending; // a periodic checkpoint is due but the world is not settled yet
};

// The world hash of the current state without a checkpoint file (the headless simulation's checkpoints). False with `error` set when the
// world is not settled (no gameplay, GUI reload pending, messages in delivery).
struct WorldHash {
    std::string hash; // 16 hex digits
    std::string text; // the TSV snapshot
    size_t fields = 0;
};
bool HashWorld(WorldHash& out, const char*& error);
// Gameplay with a built GUI and no state switch or save load queued: the frame boundary checkpoints are taken at.
bool WorldSettled();

void InitWorldHash(const Text16& gameDirectory);
bool ExecuteWorldHashCommand(const std::string& name, const std::vector<std::string>& args);
// Before the next script of a headless script list: no periodic hashes, checkpoint numbering and `expect-same` start over.
void ResetWorldHash();
bool WorldHashStep();
WorldHashStats GetWorldHashStats();
// A checkpoint of a record/replay session (testing/replay.h): the same TSV file and report line as `world-hash`, with steps = the tick.
// Empty (after a FAILED line) when the world cannot be hashed now.
struct TickCheckpoint {
    uint64_t hash = 0;
    std::string text; // the TSV snapshot, for the section digests (world_sections.h)
    unsigned index = 0; // checkpoint-<index>.tsv
};
std::optional<TickCheckpoint> CaptureTickCheckpoint(uint32_t tick);

} // namespace obl::test
