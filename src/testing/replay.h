#pragma once

#include "core/char16.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace obl::test {

// Command recording and replay on the tick callback (docs/testing/replays.md):
//   record <file> [save <path>] [every N]   after `seed`, `fixed-dt` and (unless save) `boot-gameplay scenario <n>`:
//                             records the next world's commands and world hashes every N ticks (default 100);
//                             save paths and output paths are relative to the game directory
//   record end                ends the recording at the current tick with a last checkpoint and writes the file
//   replay <file> [allow-mismatch]   blocking: boots the file's start state with its seed and fixed-dt, feeds the recorded commands at their
//                             ticks, compares every checkpoint and names the differing sections of a mismatch;
//                             relative paths come from the script's directory
void InitReplay(const Text16& gameDirectory);
// Before the next script of a headless script list: `replay` paths are relative to that script's directory.
void ResetReplay(const std::filesystem::path& script);
bool ExecuteRecordCommand(const std::string& name, const std::vector<std::string>& args);

struct ReplayProgress {
    enum class Kind : uint8_t {
        Waiting, // the start state is loading: let normal frames run
        Steps, // run `steps` step frames, then call again
        Done, // finished (PASS/FAILED logged): continue the script
    } kind = Kind::Done;
    uint32_t steps = 0;
};
// One call per frame while the script stands on a `replay` directive.
ReplayProgress StepReplay(const std::vector<std::string>& args);

} // namespace obl::test
