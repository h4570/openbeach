#pragma once

#include <cstdint>
#include <string>

// Diagnostics for the end of a run that cannot go on: naming a code address in a crash report, and stopping on input a
// code path does not handle (the fallback for failures the game code cannot report itself).
namespace obl::fatal {

// Installed once by the host before game startup; absent services use portable fallbacks.
struct ReportHandlers {
    std::string (*describeAddress)(uintptr_t) = nullptr;
    void (*unhandledInput)(const char*, const char*) = nullptr;
};
void SetReportHandlers(ReportHandlers handlers);

// The function an address lies in: a symbol of the game (its PDB) or of obl-sim (dladdr), else module+offset.
std::string DescribeAddress(uintptr_t address);

// Input the game has no defined answer for (an index out of range, an unknown kind): logs `function` and `reason` through
// the host and ends the process.
[[noreturn]] void UnhandledInput(const char* function, const char* reason);

} // namespace obl::fatal
