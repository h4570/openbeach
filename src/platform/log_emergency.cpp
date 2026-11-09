// The OS side of log::EmergencyWrite: a duplicated handle handle of the log file and a timestamp read straight from the
// OS clock, so a crashed thread that holds a heap or C runtime lock can still write its report.

#include "platform/log_emergency.h"

#include "core/log.h"

#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <ctime>
#include <pthread.h>
#include <unistd.h>
#endif

namespace obl::platform {
namespace {

#ifdef _WIN32
intptr_t Keep(std::FILE* file)
{
    HANDLE emergency;
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)));
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &emergency, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return log::kNoEmergencyFile;
    return reinterpret_cast<intptr_t>(emergency);
}

void Write(intptr_t file, const char* data, size_t size)
{
    DWORD written;
    WriteFile(reinterpret_cast<HANDLE>(file), data, static_cast<DWORD>(size), &written, nullptr);
}

int Stamp(char* out, size_t size)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    return std::snprintf(out, size, "[%02u:%02u:%02u.%03u][%5lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());
}
#else
intptr_t Keep(std::FILE* file)
{
    const int duplicate = ::dup(fileno(file));
    return duplicate < 0 ? log::kNoEmergencyFile : duplicate;
}

void Write(intptr_t file, const char* data, size_t size)
{
    [[maybe_unused]] const ssize_t written = ::write(static_cast<int>(file), data, size);
}

int Stamp(char* out, size_t size)
{
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    std::tm local{};
    localtime_r(&now.tv_sec, &local);
    return std::snprintf(out, size, "[%02d:%02d:%02d.%03ld][%5lu] ", local.tm_hour, local.tm_min, local.tm_sec, now.tv_nsec / 1000000,
            // pthread_t is an integer on Linux and a pointer on macOS: the C cast takes both.
            (unsigned long)(uintptr_t)pthread_self());
}
#endif

} // namespace

void InitLogEmergency()
{
    log::SetEmergencyBackend({&Keep, &Write, &Stamp});
}

} // namespace obl::platform
