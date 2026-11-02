#include "core/log.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace obl::log {

namespace {
std::mutex g_mutex;
std::FILE* g_file = nullptr;
Tap g_tap = nullptr;
Sink g_debugSink = nullptr;

EmergencyBackend g_emergency{};

// Keep a separate native handle alive until process exit: crash reporting must also work during Close().
std::atomic<intptr_t> g_emergencyFile{kNoEmergencyFile};
static_assert(std::atomic<intptr_t>::is_always_lock_free);

// The C++ thread id as a number: the OS thread id on Windows (what crash.cpp's hang report names), pthread_t elsewhere.
unsigned long ThreadNumber()
{
    thread_local const unsigned long number = [] {
        std::ostringstream text;
        text << std::this_thread::get_id();
        return static_cast<unsigned long>(std::strtoull(text.str().c_str(), nullptr, 10));
    }();
    return number;
}

// "[hh:mm:ss.mmm][thread] message\n" in local time.
void FormatLine(char* line, size_t size, const char* message)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::snprintf(line, size, "[%02d:%02d:%02d.%03d][%5lu] %s\n", local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(millis),
            ThreadNumber(), message);
}

void WriteEmergency(const char* line, size_t size)
{
    const intptr_t file = g_emergencyFile.load();
    if (file == kNoEmergencyFile || !g_emergency.write) return;
    g_emergency.write(file, line, size);
}

void KeepEmergencyHandle(std::FILE* file)
{
    if (g_emergencyFile.load() != kNoEmergencyFile || !g_emergency.keep) return;
    g_emergencyFile.store(g_emergency.keep(file));
}
} // namespace

void Open(const std::filesystem::path& path)
{
    std::lock_guard lock(g_mutex);
    if (g_file) return;
#ifdef _WIN32
    g_file = _wfopen(path.c_str(), L"ab");
#else
    g_file = std::fopen(path.c_str(), "ab");
#endif
    if (!g_file) return;
    std::setvbuf(g_file, nullptr, _IONBF, 0); // every line reaches the file at once (the runner reads it while the game runs)
    KeepEmergencyHandle(g_file);
}

void Close()
{
    std::lock_guard lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void SetEmergencyBackend(const EmergencyBackend& backend)
{
    std::lock_guard lock(g_mutex);
    g_emergency = backend;
}

void SetTap(Tap tap)
{
    std::lock_guard lock(g_mutex);
    g_tap = tap;
}

void SetDebugSink(Sink sink)
{
    std::lock_guard lock(g_mutex);
    g_debugSink = sink;
}

void Write(const char* fmt, ...)
{
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char line[2200];
    FormatLine(line, sizeof(line), msg);

    std::lock_guard lock(g_mutex);
    if (g_tap) g_tap(msg);
    if (g_debugSink) g_debugSink(line);
    if (g_file) std::fwrite(line, 1, std::strlen(line), g_file);
}

void EmergencyWrite(const char* fmt, ...)
{
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    // No heap and no C runtime locks here (the crashed thread may hold them): the OS clock and thread id directly.
    char line[2200];
    const int prefix = g_emergency.stamp ? g_emergency.stamp(line, sizeof(line)) : 0;
    const int size = prefix < 0 ? -1 : std::snprintf(line + prefix, sizeof(line) - prefix, "%s\n", msg);
    if (size > 0) WriteEmergency(line, std::strlen(line));
}

} // namespace obl::log
