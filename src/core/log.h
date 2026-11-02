#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

// The game log (`openbeach.log`): one line per message, "[hh:mm:ss.mmm][thread] text". Portable (C runtime file, std::chrono,
// the C++ thread id); only the crash path's native handle is platform code (EmergencyBackend, src/platform/log_emergency.cpp).
namespace obl::log {

// Opens (appends to) the log file. Safe to call once; later calls are ignored.
void Open(const std::filesystem::path& path);
void Close();

void Write(const char* fmt, ...);

// Crash diagnostics only: bypass the log mutex, tap, debug sink and CRT file locks.
// Uses a process-lifetime native append handle; formatting stays in fixed stack buffers.
void EmergencyWrite(const char* fmt, ...);

// The OS side of EmergencyWrite, installed by the host before Open (without it EmergencyWrite drops its lines). None of
// the three may allocate or take C runtime locks when called from the crash path (write, stamp).
inline constexpr intptr_t kNoEmergencyFile = -1;
struct EmergencyBackend {
    intptr_t (*keep)(std::FILE* file); // a duplicate of the file's native handle that outlives fclose; kNoEmergencyFile on failure
    void (*write)(intptr_t file, const char* data, size_t size);
    int (*stamp)(char* out, size_t size); // writes "[hh:mm:ss.mmm][thread] " in local time; its length, or -1
};
void SetEmergencyBackend(const EmergencyBackend& backend);

// Receives every formatted message (without timestamp) while the log mutex is held, so it must be quick and
// must not log. The test runner uses it to read back values from its own report lines.
using Tap = void (*)(const char* message);
void SetTap(Tap tap);

// Receives every complete line (with timestamp and newline) while the log mutex is held: the Windows game passes it to the
// debugger (OutputDebugStringA) outside headless runs. Null removes it.
using Sink = void (*)(const char* line);
void SetDebugSink(Sink sink);

} // namespace obl::log

#define OBL_LOG(...) ::obl::log::Write(__VA_ARGS__)
