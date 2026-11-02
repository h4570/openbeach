// obl-sim for i386 Linux (docs/architecture/linux-host.md): the headless game without a DLL or host process. It does what
// OpenBeach.exe and OBL_Init do on Windows (open the log, initialize the C++ modules) and then what the C++ entry
// and WinMain do in headless mode, and returns the headless run's exit code.
//
//   obl-sim --game-dir <run> --headless <replay|bot|script>

#include "core/char16.h"
#include "core/code_page.h"
#include "core/config.h"
#include "platform/log_emergency.h"
#include "platform/headless_main.h"
#include "game/game/startup.h"
#include "platform/host_init.h"
#include "dll/init.h"
#include "core/log.h"
#include "core/fatal.h"
#include "core/wide_path.h"

#if !defined(__EMSCRIPTEN__)
#include <execinfo.h>
#include <ucontext.h>
#endif
#include <unistd.h>

#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kExitUsage = 2; // the launcher's code for bad arguments (docs/build/running-the-game.md)

std::string Absolute(const char* path)
{
    char resolved[PATH_MAX];
    return realpath(path, resolved) ? std::string(resolved) : std::string();
}

bool EndsWith(const std::string& text, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

// msvcrt's _onexit list for the game's one static destructor; it runs when the headless run is over, as msvcrt's exit does.
std::vector<obl::game::ExitHandler> g_onExit;
obl::game::ExitHandler __cdecl OnExit(obl::game::ExitHandler function)
{
    g_onExit.push_back(function);
    return function;
}

#if !defined(__EMSCRIPTEN__)
// platform/test_tools/crash.cpp's job on Windows: a fault names where it happened in the log (the replay runner reads only the log and
// the exit code), then the default action ends the process with the signal.
void OnFault(int signal, siginfo_t* info, void* context)
{
    const auto* registers = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
#if defined(__x86_64__)
    const auto eip = static_cast<uintptr_t>(registers[REG_RIP]);
#else
    const auto eip = static_cast<uintptr_t>(registers[REG_EIP]);
#endif
    OBL_LOG("obl-sim: FAILED signal %d (%s) at %0*llX, address %p, in %s", signal, strsignal(signal), int(2 * sizeof(eip)),
            static_cast<unsigned long long>(eip), info->si_addr, obl::fatal::DescribeAddress(eip).c_str());
    void* frames[32];
    const int count = backtrace(frames, 32);
    for (int i = 0; i < count; ++i)
        OBL_LOG("obl-sim:   frame %d %s", i, obl::fatal::DescribeAddress(reinterpret_cast<uintptr_t>(frames[i])).c_str());
    obl::log::Close();
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void InitFaultHandlers()
{
    void* warmUp[1];
    backtrace(warmUp, 1); // loads libgcc now: inside the handler the heap may be the broken part
    static std::vector<char> altStack(1 << 16); // a stack overflow still gets its report
    stack_t stack{};
    stack.ss_sp = altStack.data();
    stack.ss_size = altStack.size();
    sigaltstack(&stack, nullptr);
    struct sigaction action{};
    action.sa_sigaction = &OnFault;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(signal, &action, nullptr);
}
#else
// WebAssembly traps end the run with a JavaScript stack trace from Node; there are no signals to catch.
void InitFaultHandlers() {}
#endif

} // namespace

int main(int argc, char** argv)
{
    std::string gameDir, headless;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc)
            gameDir = Absolute(argv[++i]);
        else if (!std::strcmp(argv[i], "--headless") && i + 1 < argc)
            headless = Absolute(argv[++i]);
        else {
            std::fprintf(stderr, "usage: obl-sim --game-dir <dir> --headless <replay|bot|script>\n");
            return kExitUsage;
        }
    }
    if (gameDir.empty() || headless.empty()) {
        std::fprintf(stderr, "error: obl-sim: --game-dir and --headless must name existing paths\n");
        return kExitUsage;
    }
    setenv("OBL_HEADLESS", headless.c_str(), 1);
    // Anything but a replay, a bot run or a chaos run is a test script, as the Windows launcher decides it.
    if (!EndsWith(headless, ".replay") && !EndsWith(headless, ".bot") && !EndsWith(headless, ".chaos"))
        setenv("OBL_SCRIPT", headless.c_str(), 1);
    if (chdir(gameDir.c_str()) != 0) {
        std::fprintf(stderr, "error: obl-sim: cannot enter %s\n", gameDir.c_str());
        return kExitUsage;
    }
    if (!EndsWith(gameDir, "/")) gameDir += '/';

    const std::u16string wideDir = obl::code_page::Utf8ToUtf16(gameDir.c_str());
    const obl::Text16 gameDirW(reinterpret_cast<const obl::Char16*>(wideDir.data()), wideDir.size());
    obl::platform::InitLogEmergency();
    obl::log::Open(obl::WidePath(gameDirW + obl::host::kLogFileName));
    OBL_LOG("==== obl-sim (Linux) started (built " __DATE__ " " __TIME__ ") ====");
    OBL_LOG("pid %d, game directory %s", static_cast<int>(getpid()), gameDir.c_str());
    InitFaultHandlers();
    if (const int code = obl::InitGame(gameDirW); code != 0) return code;

    obl::game::RunStartup(&OnExit);
    obl::game::SnapshotStartupData();
    const int code = obl::platform::RunHeadlessGame(nullptr);
    for (auto it = g_onExit.rbegin(); it != g_onExit.rend(); ++it) (*it)();
    OBL_LOG("obl-sim: exit %d", code);
    obl::log::Close();
    std::fflush(stdout);
    return code;
}
