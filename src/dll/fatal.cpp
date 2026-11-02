#include "core/fatal.h"
#include "runtime_reports.h"

#include "core/log.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <cxxabi.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace obl::fatal {

namespace {

constexpr uint32_t kExitCode = 0xC0DEC0DE; // Linux keeps the low byte (222)

std::mutex g_describeLock; // dbghelp is single-threaded

#ifdef _WIN32
// Loaded on the first description, under the lock.
bool Symbols()
{
    static const bool kLoaded = [] {
        HMODULE module = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(&Symbols), &module))
            return false;
        SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME);
        if (!SymInitialize(GetCurrentProcess(), nullptr, FALSE)) return false;
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        return SymLoadModuleEx(GetCurrentProcess(), nullptr, path, nullptr, reinterpret_cast<DWORD64>(module), 0, nullptr, 0) != 0;
    }();
    return kLoaded;
}
#endif

void Describe(uintptr_t address, char* out, size_t size)
{
#ifndef _WIN32
    // obl-sim links with -rdynamic, so dladdr names our functions.
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(address), &info) && info.dli_fname) {
        if (info.dli_sname) {
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            snprintf(out, size, "%s+0x%x", status == 0 && demangled ? demangled : info.dli_sname,
                    static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(info.dli_saddr)));
            std::free(demangled);
        } else {
            const char* name = info.dli_fname;
            for (const char* p = info.dli_fname; *p; ++p)
                if (*p == '/') name = p + 1;
            snprintf(out, size, "%s+0x%x", name, static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(info.dli_fbase)));
        }
        return;
    }
    snprintf(out, size, "%08x", static_cast<unsigned>(address));
#else
    if (Symbols()) {
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
            snprintf(out, size, "%s+0x%x (dll)", symbol->Name, static_cast<unsigned>(displacement));
            return;
        }
    }
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address), &module) &&
            module) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = path;
        for (const char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') name = p + 1;
        snprintf(out, size, "%s+0x%x", name, static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(module)));
    } else {
        snprintf(out, size, "%08x", static_cast<unsigned>(address));
    }
#endif
}

} // namespace

std::string DescribeAddressRuntime(uintptr_t address)
{
    char text[512];
    std::lock_guard lock(g_describeLock);
    Describe(address, text, sizeof(text));
    return text;
}

[[noreturn]] void UnhandledInputRuntime(const char* function, const char* reason)
{
    log::EmergencyWrite("autotest: FAILED unsupported input reached: %s (%s)", function, reason);
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), kExitCode);
    for (;;) Sleep(INFINITE); // TerminateProcess on the own process does not return
#else
    _exit(static_cast<int>(kExitCode & 0xff));
#endif
}

} // namespace obl::fatal

void obl::InitRuntimeReports()
{
    fatal::SetReportHandlers({fatal::DescribeAddressRuntime, fatal::UnhandledInputRuntime});
}
