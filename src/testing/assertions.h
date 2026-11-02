#pragma once

#include <string>
#include <vector>

namespace obl::test {

// Starts reading back the script's own report lines (log tap) and counting PASS / FAILED results.
void RegisterAssertions();
bool ExecuteAssertionCommand(const std::string& name, const std::vector<std::string>& args);

// Value by name: a live game probe (hour, speed, cash, ...) or, failing that, a `key=number` from the latest
// `autotest:` report line. Used by `expect` and by `await` conditions.
bool ReadValue(const std::string& name, double& out);

// game_api::Update calls seen so far; the `frames` probe and `await frames` count from it.
void CountFrame();
unsigned long long Frames();
// Before the next script of a headless script list: no report values, no PASS/FAILED counted, no frames seen.
void ResetAssertions();

int PassCount();
int FailureCount();
} // namespace obl::test
