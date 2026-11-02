#pragma once

#include "core/char16.h"
#include <filesystem>
#include <string>

namespace obl::test {

// Scripted test runs, enabled by OBL_SCRIPT (launcher --script). See autotest.cpp for commands.
bool InitAutotest(const Text16& gameDirectory);

// For the headless script loop (headless_sim.cpp): true once the script reached `quit`, was aborted (a missed `await`) or was
// refused at load (a directive the headless mode has no device for); ScriptCompleted: it reached `quit`.
bool ScriptCloseRequested();
bool ScriptCompleted();
// Headless: logs each directive refused at load (`autotest: FAILED headless refuses ...`); any refusal aborts the script. Returns the
// count.
int ReportRefusedDirectives();

// A headless run takes a script list (`--headless <name>.list`: one path per line, relative to the list): the scripts run one after
// another in one process. CurrentScript is the running one; StartNextScript (false after the last) resets the interpreter and the
// modules whose results a script reads to a fresh process's state and loads the next. The host leaves the world first.
const std::filesystem::path& CurrentScript();
size_t ScriptCount();
bool StartNextScript();

} // namespace obl::test
