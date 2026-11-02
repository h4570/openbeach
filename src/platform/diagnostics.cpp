#include "core/char16.h"
// The game's two process-ending error paths, FatalError and ShowErrorAndExit. Both log their message to
// run/openbeach.log first, so failures are visible even though the game exits silently (FatalError) or its dialog is
// suppressed (hidden mode).

#include "game/game/diagnostics.h"

#include "core/code_page.h"
#include "core/config.h"
#include "game/game/game_accessors.h"
#include "platform/runtime.h"
#include "core/log.h"
#include "platform/headless_main.h"
#include "platform/sdl_present.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace obl::game {

namespace {

FatalErrorTrap g_trap = nullptr;

// A normal exit: the game's static destructors run and stdio is flushed.
[[noreturn]] void GameExit(const char* source)
{
    OBL_LOG("game exit: code 0 (C++ %s)", source);
    ::obl::platform::Exit(0);
    __assume(0);
}

[[noreturn]] void FatalErrorV(const char* format, va_list args)
{
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    if (g_trap) {
        OBL_LOG("trapped FatalError - %s", message); // not "FatalError:", which the test runner counts as a failure
        g_trap(message);
    }
    OBL_LOG("FatalError: %s", message);
    if (config::Get().headless) {
        // A fatal error can occur during startup or teardown, outside RunHeadless's tick loop.
        std::printf("obl-sim: FAILED fatal-error %s\n", message);
        std::fflush(stdout);
        OBL_LOG("obl-sim: FAILED fatal-error %s", message);
        ::obl::platform::Exit(platform::kExitHeadlessFailed);
        __assume(0);
    }
    // The formatted text is not shown anywhere else, so only the log keeps it (with a bounded format).
    GameExit("FatalError");
}

} // namespace

void FatalError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    FatalErrorV(format, args);
}

void ShowErrorAndExit(void* owner, const Char16* text)
{
    char message[0x200];
#ifdef _WIN32
    // A text longer than the buffer fails the conversion and leaves the buffer partly written and unterminated by the
    // API; what was written is kept and always terminated.
    if (!WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<const wchar_t*>(text), -1, message, sizeof(message), nullptr, nullptr))
        message[sizeof(message) - 1] = '\0';
    OBL_LOG("ShowErrorAndExit: %s", message);
    MessageBoxA(platform::GetWin32Window(owner), message, "OpenBeach error", MB_ICONHAND | MB_SETFOREGROUND);
#else
    // The Linux obl-sim has no dialog: the log keeps the message (Windows-1252, as the game's narrow text).
    (void)owner;
    code_page::Narrow(text, message, sizeof(message));
    OBL_LOG("ShowErrorAndExit: %s", message);
#endif
    GameExit("ShowErrorAndExit");
}

void ShowNotice(void* owner, const char* text, const char* caption, bool information)
{
#ifdef _WIN32
    MessageBoxA(platform::GetWin32Window(owner), text, caption, information ? MB_OK | MB_ICONINFORMATION : MB_OK);
#else
    (void)owner, (void)information;
    OBL_LOG("%s: %s", caption, text); // no dialog on Linux
#endif
}

void RaiseAccessViolation()
{
#ifdef _WIN32
    const ULONG_PTR details[] = {1, 0};
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, details);
    std::abort(); // not reached: nothing handles the exception
#else
    __builtin_trap(); // the process ends as a crash on Linux too
#endif
}

void SetFatalErrorTrap(FatalErrorTrap trap)
{
    g_trap = trap;
}

} // namespace obl::game
