// OpenBeach - the game (docs/architecture/portable-host.md). SDL's main hands over UTF-8 arguments on every system. On
// Windows the process starts a copy of itself as the host (standalone_host.h), which runs the game; elsewhere this process
// is the host.

#include "launch_options.h"
#include "standalone_host.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

using namespace obl;

struct Options : launcher::GameOptions {
    bool standaloneHost = false; // --standalone-host: this process is the host (set by Launch only)
    bool runEntry = true; // --no-entry: the host stops after the start-up (ctest standalone_host)
};

void PrintUsage()
{
    std::fprintf(stderr,
            "Usage: OpenBeach %s [--no-entry]\n"
            "  default game dir: this program's directory when it holds the game's Data, else %s\n",
            launcher::kSharedUsage, OBL_DEFAULT_GAME_DIR);
}

// Started by a double click inside an installed game, the game is right here; a development build in build/ plays run/.
std::string DefaultGameDir()
{
    const char* base = SDL_GetBasePath();
    std::error_code error;
    if (base && std::filesystem::is_directory(std::filesystem::path(reinterpret_cast<const char8_t*>(base)) / "Data", error)) return base;
    return OBL_DEFAULT_GAME_DIR;
}

bool ParseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        if (launcher::ParseGameOption(argc, argv, i, o)) continue;
        const std::string a = argv[i];
        if (a == "--standalone-host") {
            o.standaloneHost = true;
        } else if (a == "--no-entry") {
            o.runEntry = false;
        } else {
            PrintUsage();
            return false;
        }
    }
    if (!o.script.empty() && !o.headless.empty()) {
        std::fprintf(stderr, "error: --script and --headless exclude each other\n");
        return false;
    }
    if (o.gameDir.empty()) o.gameDir = DefaultGameDir();
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    Options o;
    if (!ParseArgs(argc, argv, o)) return 1;
    if (o.standaloneHost) return standalone::HostMain(o.gameDir, o.gameArgs, o.runEntry);
    launcher::SetGameEnvironment(o);
    return standalone::Launch(o.gameDir, o.gameArgs, o.wait, o.runEntry);
}
