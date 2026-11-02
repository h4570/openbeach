#include "platform/instance_lock.h"

#include "core/log.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace obl::platform {

namespace {

std::filesystem::path g_path;

#ifdef _WIN32
HANDLE g_file = INVALID_HANDLE_VALUE;
#else
int g_file = -1;
#endif

} // namespace

void SetInstanceLockDirectory(const Text16& gameDir)
{
    // FNV-1a over the lower-cased directory: stable for one directory, different between directories.
    uint32_t hash = 2166136261u;
    for (Char16 c : gameDir) {
        Char16 lower = (c >= u'A' && c <= u'Z') ? static_cast<Char16>(c + (u'a' - u'A')) : (c == u'/' ? u'\\' : c);
        hash = (hash ^ static_cast<uint32_t>(lower)) * 16777619u;
    }
    char name[48];
    std::snprintf(name, sizeof name, "OpenBeach-%08x.lock", hash);
    std::error_code error;
    g_path = std::filesystem::temp_directory_path(error) / name;
    OBL_LOG("instance lock: '%s'", g_path.string().c_str());
}

bool AcquireInstanceLock()
{
#ifdef _WIN32
    // No sharing: a second open fails while this handle lives; the system closes it when the process ends.
    g_file = CreateFileW(g_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
#else
    g_file = open(g_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (g_file < 0) return false;
    if (flock(g_file, LOCK_EX | LOCK_NB) == 0) return true;
    close(g_file);
    g_file = -1;
    return false;
#endif
}

void ReleaseInstanceLock()
{
#ifdef _WIN32
    if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
#else
    if (g_file >= 0) {
        unlink(g_path.c_str());
        close(g_file);
    }
    g_file = -1;
#endif
}

} // namespace obl::platform
