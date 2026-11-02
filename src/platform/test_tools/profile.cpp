#include "profile.h"

#if defined(_M_X64)
#define OBL_STACK(ctx) (ctx).Rsp
#define OBL_PC(ctx) (ctx).Rip
#else
#define OBL_STACK(ctx) (ctx).Esp
#define OBL_PC(ctx) (ctx).Eip
#endif

#include "core/log.h"

#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace obl::test {
namespace {

std::atomic<bool> g_run;
std::thread g_thread;
HANDLE g_target;
std::unordered_map<uintptr_t, unsigned> g_self, g_inclusive;
unsigned g_samples;

uintptr_t g_dllBegin, g_dllEnd;

// The report keeps 16-byte buckets: enough to name a function, few enough to log.
uintptr_t Bucket(uintptr_t a)
{
    return a & ~uintptr_t(15);
}

// Copies the stack words that point into the game DLL (no C++ objects here: __try cannot unwind them).
size_t ReadStack(uintptr_t esp, uintptr_t* out, size_t capacity)
{
    size_t n = 0;
    __try {
        const auto* stack = reinterpret_cast<const uintptr_t*>(esp);
        for (size_t i = 0; i < 400 && n < capacity; ++i) {
            uintptr_t v = stack[i];
            if (v >= g_dllBegin && v < g_dllEnd) out[n++] = v;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

void Sample()
{
    if (SuspendThread(g_target) == DWORD(-1)) return;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_CONTROL;
    uintptr_t words[400];
    size_t n = 0;
    bool ok = GetThreadContext(g_target, &ctx);
    if (ok) n = ReadStack(OBL_STACK(ctx), words, std::size(words));
    ResumeThread(g_target);
    if (!ok) return;
    ++g_samples;
    ++g_self[Bucket(OBL_PC(ctx))];
    // The game DLL is built without frame pointers, so the stack words that point into it stand for the callers (stale words add noise).
    std::unordered_set<uintptr_t> seen;
    for (size_t i = 0; i < n; ++i)
        if (seen.insert(Bucket(words[i])).second) ++g_inclusive[Bucket(words[i])];
}

void Report(size_t limit)
{
    OBL_LOG("autotest: profile samples=%u", g_samples);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    for (auto* map : {&g_self, &g_inclusive}) {
        std::vector<std::pair<uintptr_t, unsigned>> sorted(map->begin(), map->end());
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < sorted.size() && i < limit; ++i) {
            HMODULE module = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(sorted[i].first), &module);
            char path[MAX_PATH] = {};
            if (module) GetModuleFileNameA(module, path, MAX_PATH);
            const char* base = path;
            for (const char* c = path; *c; ++c)
                if (*c == '\\' || *c == '/') base = c + 1;
            // Debug builds of the game DLL ship a PDB; other addresses retain their module offsets.
            char name[512] = {};
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            if (SymFromAddr(GetCurrentProcess(), sorted[i].first, &displacement, symbol)) {
                snprintf(name, sizeof(name), "%s", symbol->Name);
            }
            OBL_LOG("autotest: profile %s %08x count=%u module-offset=%x module=%s symbol=%s", map == &g_self ? "self" : "incl",
                    static_cast<unsigned>(sorted[i].first), sorted[i].second,
                    static_cast<unsigned>(sorted[i].first - reinterpret_cast<uintptr_t>(module)), base, name[0] ? name : "-");
        }
    }
    SymCleanup(GetCurrentProcess());
}

} // namespace

bool ExecuteProfileCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "profile") return false;
    if (!args.empty() && args[0] == "on" && !g_run) {
        g_self.clear();
        g_inclusive.clear();
        g_samples = 0;
        HMODULE module = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&ExecuteProfileCommand), &module);
        g_dllBegin = reinterpret_cast<uintptr_t>(module);
        g_dllEnd = g_dllBegin +
                reinterpret_cast<const IMAGE_NT_HEADERS*>(g_dllBegin + reinterpret_cast<const IMAGE_DOS_HEADER*>(g_dllBegin)->e_lfanew)
                        ->OptionalHeader.SizeOfImage;
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_target, 0, FALSE, DUPLICATE_SAME_ACCESS);
        g_run = true;
        g_thread = std::thread([] {
            while (g_run) {
                Sample();
                Sleep(2);
            }
        });
        OBL_LOG("autotest: profile on");
    } else if (!args.empty() && args[0] == "off" && g_run) {
        g_run = false;
        g_thread.join();
        CloseHandle(g_target);
        Report(args.size() > 1 ? size_t(std::atoi(args[1].c_str())) : 60);
    } else {
        OBL_LOG("autotest: FAILED profile (expected on or off [n], and not already in that state)");
    }
    return true;
}

} // namespace obl::test
