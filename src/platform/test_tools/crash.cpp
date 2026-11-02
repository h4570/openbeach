#include "crash.h"

#include "game/objects/object_building.h"
#include "platform/import_redirect.h"
#include "core/log.h"

#include <windows.h>
#include <crtdbg.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdint>

// The CONTEXT registers are Eax.. on x86 and Rax.. on x64; both print as size_t.
#if defined(_M_X64)
#define OBL_REG(ctx, r) static_cast<size_t>((ctx).R##r)
#else
#define OBL_REG(ctx, r) static_cast<size_t>((ctx).E##r)
#endif

namespace obl::test {

namespace {

// Loaded modules use module offsets; unmapped addresses remain absolute.
void Describe(uintptr_t address, char* out, size_t size)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address), &module) &&
            module) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = path;
        for (const char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') name = p + 1;
        snprintf(out, size, "%08x %s+0x%x", static_cast<unsigned>(address), name,
                static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(module)));
    } else {
        snprintf(out, size, "%08x", static_cast<unsigned>(address));
    }
}

bool InCode(uintptr_t value)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(reinterpret_cast<void*>(value), &info, sizeof(info))) return false;
    constexpr DWORD kExec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & kExec);
}

// Logs words on the stack that point into game code (call return addresses plus some stale ones - the top entries are the
// useful ones). `prefix` is the report tag.
void ScanStack(const char* prefix, uintptr_t esp, int limit, uintptr_t minAddress = 0x10000)
{
    char where[MAX_PATH + 64];
    int found = 0;
    __try {
        const uintptr_t* stack = reinterpret_cast<const uintptr_t*>(esp);
        for (int i = 0; i < 512 && found < limit; ++i) {
            uintptr_t value = stack[i];
            if (value >= minAddress && InCode(value)) {
                Describe(value, where, sizeof(where));
                log::EmergencyWrite("autotest: CRASH %sstack[%d] %s", prefix, i, where);
                ++found;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log::EmergencyWrite("autotest: CRASH %sstack unreadable", prefix);
    }
}

LONG WINAPI Filter(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
    const CONTEXT* ctx = ep->ContextRecord;
    char where[MAX_PATH + 64];
    Describe(reinterpret_cast<uintptr_t>(rec->ExceptionAddress), where, sizeof(where));
    log::EmergencyWrite("autotest: CRASH code=%08lx at %s", rec->ExceptionCode, where);
    if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && rec->NumberParameters >= 2)
        log::EmergencyWrite("autotest: CRASH %s of %08x",
                rec->ExceptionInformation[0] == 1 ? "write" : (rec->ExceptionInformation[0] == 8 ? "execute" : "read"),
                static_cast<unsigned>(rec->ExceptionInformation[1]));
    log::EmergencyWrite("autotest: CRASH eax=%08zx ebx=%08zx ecx=%08zx edx=%08zx esi=%08zx edi=%08zx ebp=%08zx esp=%08zx",
            OBL_REG(*ctx, ax), OBL_REG(*ctx, bx), OBL_REG(*ctx, cx), OBL_REG(*ctx, dx), OBL_REG(*ctx, si), OBL_REG(*ctx, di),
            OBL_REG(*ctx, bp), OBL_REG(*ctx, sp));

    ScanStack("", OBL_REG(*ctx, sp), 24);
    TerminateProcess(GetCurrentProcess(), 0xC0DEDEAD);
    return EXCEPTION_EXECUTE_HANDLER;
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI KeepFilter(LPTOP_LEVEL_EXCEPTION_FILTER)
{
    // Script mode owns fatal exceptions. Allowing the game/CRT to replace this filter loses diagnostics.
    return Filter;
}

// Hang watchdog: the script runner beats once per frame; when the game thread stops beating (an endless loop inside game
// code, a step that never returns) the thread is sampled a few times and the process ends, so the report names where it
// spins instead of the test just timing out.
std::atomic<DWORD> g_beatThread{0};
std::atomic<uint32_t> g_beats{0};
constexpr DWORD kHangMs = 20000;
// Commands that legitimately run for a long time inside one frame (`call-drive` over every shipped file) get a longer limit.
std::atomic<DWORD> g_hangMs{kHangMs};

DWORD WINAPI Watchdog(void*)
{
    uint32_t seen = 0;
    DWORD sinceChange = GetTickCount();
    for (;;) {
        Sleep(500);
        uint32_t now = g_beats.load();
        if (now != seen) {
            seen = now;
            sinceChange = GetTickCount();
            continue;
        }
        if (!now || GetTickCount() - sinceChange < g_hangMs.load()) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_beatThread.load());
        log::EmergencyWrite(
                "autotest: CRASH code=HANG no frame for %lu ms, sampling thread %lu", GetTickCount() - sinceChange, g_beatThread.load());
        for (int sample = 0; thread && sample < 8; ++sample) {
            if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                if (GetThreadContext(thread, &ctx)) {
                    char where[MAX_PATH + 64];
                    Describe(OBL_REG(ctx, ip), where, sizeof(where));
                    char tag[16];
                    snprintf(tag, sizeof(tag), "s%d ", sample);
                    log::EmergencyWrite("autotest: CRASH %sat %s", tag, where);
                    log::EmergencyWrite("autotest: CRASH %seax=%08zx ebx=%08zx ecx=%08zx edx=%08zx esi=%08zx edi=%08zx ebp=%08zx esp=%08zx",
                            tag, OBL_REG(ctx, ax), OBL_REG(ctx, bx), OBL_REG(ctx, cx), OBL_REG(ctx, dx), OBL_REG(ctx, si), OBL_REG(ctx, di),
                            OBL_REG(ctx, bp), OBL_REG(ctx, sp));
                    if (sample == 0 || sample == 7) ScanStack(tag, OBL_REG(ctx, sp), 16);
                }
                ResumeThread(thread);
            }
            Sleep(137);
        }
        // Where the game thread waits, the thread holding the lock matters: dump every other thread once as well.
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry = {sizeof(entry)};
        for (BOOL ok = snapshot != INVALID_HANDLE_VALUE && Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == g_beatThread.load() ||
                    entry.th32ThreadID == GetCurrentThreadId())
                continue;
            HANDLE other = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
            if (!other) continue;
            if (SuspendThread(other) != static_cast<DWORD>(-1)) {
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                if (GetThreadContext(other, &ctx)) {
                    char where[MAX_PATH + 64];
                    char tag[32];
                    snprintf(tag, sizeof(tag), "t%lu ", entry.th32ThreadID);
                    Describe(OBL_REG(ctx, ip), where, sizeof(where));
                    log::EmergencyWrite("autotest: CRASH %sat %s", tag, where);
                    ScanStack(tag, OBL_REG(ctx, sp), 14, 0x10000);
                }
                ResumeThread(other);
            }
            CloseHandle(other);
        }
        if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
        TerminateProcess(GetCurrentProcess(), 0xC0DEDEAD);
    }
}

void CrashInGame()
{
    // A null `this` through the member, which reads the building in the DLL (tools/test_crash_diagnostics.ps1).
    object::Building* volatile building = nullptr;
    building->object::Building::OnExitState();
}

void CrashFromTap(const char*)
{
    CrashInGame();
}

void CheckHandledException()
{
    // A first-chance handler would incorrectly terminate here; only unhandled exceptions are fatal.
    __try {
        RaiseException(0xE0424242, 0, 0, nullptr);
    } __except (GetExceptionCode() == 0xE0424242 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        OBL_LOG("autotest: PASS crash-handled exception reached its SEH handler");
    }
}

#if defined(_DEBUG)
// A Debug CRT assert (heap check, iterator debugging) would open a message box nobody sees in a hidden run and the test would
// only time out; logging it and breaking turns it into a crash report with the faulting stack.
int ReportToLog(int type, char* message, int* result)
{
    if (type == _CRT_WARN) return FALSE;
    log::EmergencyWrite("autotest: CRASH crt report: %s", message ? message : "");
    *result = 1; // _CrtDbgReport returns 1: the CRT executes __debugbreak, which the unhandled exception filter reports
    return TRUE;
}
#endif

} // namespace

bool InitCrashDiagnostics()
{
#if defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, ReportToLog);
#endif
    g_exchangeWatchdogLimit = [](uint32_t limit) -> uint32_t {
        WatchdogBeat();
        return g_hangMs.exchange(limit);
    };
    SetUnhandledExceptionFilter(Filter);
    // A test shortens the limit to prove that a host beats often enough (the chaos ctest with a save round trip).
    wchar_t value[16];
    if (GetEnvironmentVariableW(L"OBL_HANG_MS", value, 16) && _wtoi(value) > 0) g_hangMs.store(static_cast<DWORD>(_wtoi(value)));
    if (HANDLE watchdog = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr)) CloseHandle(watchdog);
    // The DLL's own CRT and SDL would replace it; system DLLs do not.
    return platform::RedirectImport("kernel32.dll", "SetUnhandledExceptionFilter", reinterpret_cast<void*>(KeepFilter), nullptr);
}

void WatchdogBeat()
{
    g_beatThread.store(GetCurrentThreadId());
    g_beats.fetch_add(1);
}

bool ExecuteCrashCommand(const std::string& name)
{
    if (name == "crash-handled") {
        CheckHandledException();
        return true;
    }
    if (name != "crash-test" && name != "crash-test-log-lock" && name != "crash-test-filter" && name != "crash-test-hang") return false;
    OBL_LOG("autotest: %s - calling the building exit-state handler with a null object", name.c_str());
    if (name == "crash-test-hang") {
        // The frame beat stops here, so the watchdog must report this thread (inside Sleep) after kHangMs and end the process.
        Sleep(120000);
        return true;
    }
    if (name == "crash-test-filter") SetUnhandledExceptionFilter(nullptr);
    if (name == "crash-test-log-lock") {
        log::SetTap(CrashFromTap);
        OBL_LOG("autotest: triggering crash inside the locked log tap");
    } else {
        CrashInGame();
    }
    return true;
}

} // namespace obl::test
