// OpenBeach - the game (docs/architecture/portable-host.md). SDL's main hands over UTF-8 arguments on every system. On
// Windows the process starts a copy of itself as the host (standalone_host.h), which runs the game; elsewhere this process
// is the host.

#include "launch_options.h"
#include "settings_window.h"
#include "standalone_host.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

using namespace obl;

struct Options : launcher::GameOptions {
    bool standaloneHost = false; // --standalone-host: this process is the host (set by Launch only)
    bool runEntry = true; // --no-entry: the host stops after the start-up (ctest standalone_host)
    bool forceSettings = false; // --settings: show the settings window even when openbeach.ini turned it off
    bool noSettings = false; // --no-settings: never show it (smoke tests, a desktop shortcut that wants the game at once)
};

void PrintUsage()
{
    std::fprintf(stderr,
            "Usage: OpenBeach %s [--no-entry]\n"
            "  default game dir: this program's directory when it holds the game's Data, else its userdata/ directory\n"
            "  (a release archive), else %s\n",
            launcher::kSharedUsage, OBL_DEFAULT_GAME_DIR);
}

// Started by a double click inside an installed game, the game is right here; in a release archive it is userdata/ beside
// the program (docs/build/releases.md), and a development build in build/ plays run/. The release name is not "data": on
// Windows that is the same directory as the installed game's Data.
std::string DefaultGameDir()
{
    const char* base = SDL_GetBasePath();
    if (!base) return OBL_DEFAULT_GAME_DIR;
    const std::filesystem::path dir(reinterpret_cast<const char8_t*>(base));
    std::error_code error;
    if (std::filesystem::is_directory(dir / "Data", error)) return base;
    if (std::filesystem::is_directory(dir / "userdata", error)) {
        const std::u8string text = (dir / "userdata").u8string();
        return std::string(text.begin(), text.end());
    }
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
        } else if (a == "--settings") {
            o.forceSettings = true;
        } else if (a == "--no-settings") {
            o.noSettings = true;
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

// Only a player's start: test runs and tools pass a flag or variable that rules it out, and must never wait on a window.
bool OffersSettings(const Options& o)
{
    if (o.noSettings || o.hidden || !o.runEntry || !o.script.empty() || !o.headless.empty()) return false;
    for (const char* name : {"OBL_HIDDEN", "OBL_SCRIPT", "OBL_HEADLESS"})
        if (const char* value = std::getenv(name); value && *value) return false;
    return launcher::ShouldShowSettings(o.gameDir, o.forceSettings);
}

} // namespace

int main(int argc, char* argv[])
{
    Options o;
    if (!ParseArgs(argc, argv, o)) return 1;
    if (o.standaloneHost) return standalone::HostMain(o.gameDir, o.gameArgs, o.runEntry);
    if (OffersSettings(o) && launcher::RunSettingsWindow(o.gameDir) == launcher::SettingsChoice::Quit) return 0;
    launcher::SetGameEnvironment(o);
    return standalone::Launch(o.gameDir, o.gameArgs, o.wait, o.runEntry);
}
