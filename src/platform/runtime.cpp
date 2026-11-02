#include "core/char16.h"
#include "platform/runtime.h"
#include "game/game/local_time.h"

#include <cstdarg>
#include <cstdlib>
#include <type_traits>

#include "platform/sdl_present.h"
#ifdef _WIN32
#include "game/game/game_accessors.h"

#include <windows.h>
#else
#include "core/wide_format.h"
#include "core/log.h"

#include <sys/mman.h>
#endif

#include <cstdint>

namespace obl::platform {
#ifdef _WIN32
namespace {
// This runtime's rounding and wide %s conventions define the existing UI references.
template<class Function> Function RuntimeFunction(const char* name)
{
    static HMODULE module = LoadLibraryW(L"msvcrt.dll");
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}
// Resolve before InitGame starts import tracing, as the former bridge did.
const auto g_printText = RuntimeFunction<int(__cdecl*)(Char16*, const Char16*, va_list)>("vswprintf");
const auto g_printNarrow = RuntimeFunction<int(__cdecl*)(char*, const char*, va_list)>("vsprintf");
const auto g_parseNumber = RuntimeFunction<double(__cdecl*)(const Char16*, Char16**)>("wcstod");
// msvcrt's `localtime` takes the process-width `time_t`: 32 bits on Win32, 64 on x64 (a 32-bit argument crashed the x64
// save list on the garbage high word).
using RuntimeTime = std::conditional_t<sizeof(void*) == 4, int32_t, int64_t>;
const auto g_localTime = RuntimeFunction<std::tm*(__cdecl*)(const RuntimeTime*)>("localtime");
} // namespace
#endif

int PrintTextV(Char16* out, const Char16* format, va_list args)
{
#ifdef _WIN32
    return g_printText(out, format, args);
#else
    return wide_format::FormatUnbounded(reinterpret_cast<char16_t*>(out), reinterpret_cast<const char16_t*>(format), args);
#endif
}

int PrintText(Char16* out, const Char16* format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = PrintTextV(out, format, args);
    va_end(args);
    return result;
}

int PrintNarrowV(char* out, const char* format, va_list args)
{
#ifdef _WIN32
    return g_printNarrow(out, format, args);
#else
    return wide_format::FormatUnboundedNarrow(out, format, args);
#endif
}

int PrintNarrow(char* out, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = PrintNarrowV(out, format, args);
    va_end(args);
    return result;
}

double ParseNumber(const Char16* text, Char16** end)
{
#ifdef _WIN32
    return g_parseNumber(text, end);
#else
    const char16_t* stop = nullptr;
    const double result = wide_format::ParseDouble(reinterpret_cast<const char16_t*>(text), &stop);
    if (end) *end = reinterpret_cast<Char16*>(const_cast<char16_t*>(stop));
    return result;
#endif
}

[[noreturn]] void Exit(int code)
{
    std::exit(code);
}

void RequestQuit(int code)
{
    platform::RequestSdlQuit(code);
}

bool MemoryReadable(const void* pointer, size_t size)
{
    if (!pointer) return false;
#ifndef _WIN32
    // Mapped pages answer msync; an unmapped one fails with ENOMEM (PROT_NONE mappings are not told apart: test use only).
    constexpr uintptr_t kPage = 4096;
    const auto begin = reinterpret_cast<uintptr_t>(pointer);
    for (uintptr_t page = begin & ~(kPage - 1); page < begin + size; page += kPage)
        if (msync(reinterpret_cast<void*>(page), kPage, MS_ASYNC) != 0) return false;
    return true;
#else
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    const auto end = reinterpret_cast<uintptr_t>(pointer) + size;
    return end <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
#endif
}

void PostGameWindowClose()
{
#ifdef _WIN32
    void* window = game::GetGameWindow();
    PostMessageW(window ? platform::GetWin32Window(window) : FindWindowA("Beach Life", nullptr), WM_CLOSE, 0, 0);
#else
    PostSdlGameWindowClose();
#endif
}

bool ModuleLoaded(const Char16* name)
{
#ifdef _WIN32
    return GetModuleHandleW(reinterpret_cast<const wchar_t*>(name)) != nullptr;
#else
    (void)name;
    return false;
#endif
}
} // namespace obl::platform

namespace obl::platform {
std::tm LocalTime(int32_t seconds)
{
#ifdef _WIN32
    const platform::RuntimeTime value = seconds;
    return *platform::g_localTime(&value);
#else
    const std::time_t value = seconds;
    return *std::localtime(&value);
#endif
}
} // namespace obl::platform
