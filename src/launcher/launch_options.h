#pragma once

// The command-line flags of OpenBeach (the game executable). They reach the game as environment variables of the
// host process. Text is UTF-8 (SDL's main hands over UTF-8 arguments on every system).

#include <string>

namespace obl::launcher {

struct GameOptions {
    std::string gameDir; // --game-dir; empty = the executable's default
    std::string gameArgs; // after `--`: the game's own arguments, each with its leading space (quoted when it has blanks)
    bool wait = true; // --no-wait: return without waiting for the game's exit code (Windows)
    bool hidden = false; // --hidden: test runs, window off-screen and never focused (OBL_HIDDEN)
    bool keepSaves = false; // --keep-saves: skip the first-start old-save conversion (OBL_KEEP_SAVES), for hosts on the save corpus
    std::string script; // --script: test automation script executed by the game (OBL_SCRIPT), absolute
    std::string maxFps; // --max-fps: frame cap override (OBL_MAXFPS), scripts default to unlimited
    std::string playMovie; // --movie: play the intro movie even in script mode (OBL_PLAYMOVIE)
    // --headless: replay file (`.replay`) or test script the game runs without window, renderer, audio or input (OBL_HEADLESS,
    // a script also as OBL_SCRIPT), absolute
    std::string headless;
};

// Consumes the shared flag at argv[i] (and its value; `--` takes the rest of the line). False = not a shared flag.
bool ParseGameOption(int argc, char** argv, int& i, GameOptions& o);

void SetGameEnvironment(const GameOptions& o);

// `--headless <file>`: a `.replay` file (any case) is a command replay, a `.bot` file a bot run (docs/architecture/bots/index.md), a
// `.chaos` file a chaos run (docs/testing/chaos-bot.md), anything else a test script.
bool IsReplayFile(const std::string& path);

// One argument as CreateProcess's command-line parser reads it back.
std::string QuoteArgument(const std::string& arg);

// An absolute UTF-8 path (`path` unchanged when it cannot be resolved).
std::string AbsolutePath(const std::string& path);

inline constexpr const char kSharedUsage[] =
        "[--game-dir <dir>] [--no-wait] [--hidden] [--keep-saves] [--script <file> | --headless <replay|script>] [--max-fps <n>] [--movie] "
        "[-- <game args>]";

} // namespace obl::launcher
