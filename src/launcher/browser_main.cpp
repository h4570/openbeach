// OpenBeach in a browser tab (docs/architecture/browser-build.md). The page's pre-js (browser_files.js) fills MEMFS /game
// from browser storage and calls main with --game-dir /game. The tab is the only process: no host process, and no old-save
// conversion (the browser importer converts saves).

#include "launch_options.h"

#include "app/init.h"
#include "platform/platform_main.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string>

#include <unistd.h>

namespace {

using namespace obl;

// The game's exit code when the start-up fails, as standalone_host.h's kExitInitFailed.
constexpr int kExitInitFailed = 4;

bool ParseArgs(int argc, char** argv, launcher::GameOptions& o)
{
    for (int i = 1; i < argc; ++i) {
        if (launcher::ParseGameOption(argc, argv, i, o)) continue;
        std::fprintf(stderr, "Usage: OpenBeach %s\n", launcher::kSharedUsage);
        return false;
    }
    if (o.gameDir.empty()) o.gameDir = "/game";
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    launcher::GameOptions o;
    if (!ParseArgs(argc, argv, o)) return 1;
    launcher::SetGameEnvironment(o);
    // The game opens its data by relative paths.
    if (chdir(o.gameDir.c_str()) != 0) {
        std::fprintf(stderr, "error: cannot enter the game directory %s\n", o.gameDir.c_str());
        return kExitInitFailed;
    }
    std::string dir = o.gameDir;
    if (dir.back() != '/') dir += '/';
    const int result = OpenGame(dir);
    std::printf("browser: start-up returned %d\n", result);
    if (result != 0) return kExitInitFailed;
    std::fflush(stdout);
    return platform::RunGame();
}
