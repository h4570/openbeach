#include "launch_options.h"

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace obl::launcher {

namespace {

void SetVariable(const char* name, const std::string& value)
{
#ifdef _WIN32
    // The wide API, so a non-ASCII path reaches the game's _wgetenv/GetEnvironmentVariableW unchanged.
    const std::filesystem::path wide(std::u8string(value.begin(), value.end()));
    SetEnvironmentVariableW(std::filesystem::path(name).c_str(), wide.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

} // namespace

std::string AbsolutePath(const std::string& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(std::filesystem::path(std::u8string(path.begin(), path.end())), error);
    if (error) return path;
    const std::u8string text = absolute.u8string();
    return std::string(text.begin(), text.end());
}

bool ParseGameOption(int argc, char** argv, int& i, GameOptions& o)
{
    const std::string_view a = argv[i];
    if (a == "--game-dir" && i + 1 < argc) {
        o.gameDir = argv[++i];
    } else if (a == "--no-wait") {
        o.wait = false;
    } else if (a == "--hidden") {
        o.hidden = true;
    } else if (a == "--keep-saves") {
        o.keepSaves = true;
    } else if ((a == "--script" || a == "--headless") && i + 1 < argc) {
        (a == "--script" ? o.script : o.headless) = AbsolutePath(argv[++i]);
    } else if (a == "--max-fps" && i + 1 < argc) {
        o.maxFps = argv[++i];
    } else if (a == "--movie") {
        o.playMovie = "1";
    } else if (a == "--") {
        // Each argument as one argument of the game's command line (a path with spaces stays one argument).
        for (++i; i < argc; ++i) o.gameArgs += " " + QuoteArgument(argv[i]);
    } else {
        return false;
    }
    return true;
}

void SetGameEnvironment(const GameOptions& o)
{
    if (o.hidden) SetVariable("OBL_HIDDEN", "1");
    if (o.keepSaves) SetVariable("OBL_KEEP_SAVES", "1");
    if (!o.script.empty()) SetVariable("OBL_SCRIPT", o.script);
    if (!o.headless.empty()) {
        SetVariable("OBL_HEADLESS", o.headless);
        // Anything but a replay or a bot run is a test script: the game runs its interpreter on the headless frame loop.
        if (!IsReplayFile(o.headless)) SetVariable("OBL_SCRIPT", o.headless);
    }
    if (!o.maxFps.empty()) SetVariable("OBL_MAXFPS", o.maxFps);
    if (!o.playMovie.empty()) SetVariable("OBL_PLAYMOVIE", o.playMovie);
}

bool IsReplayFile(const std::string& path)
{
    const auto endsWith = [&path](std::string_view extension) {
        if (path.size() < extension.size()) return false;
        for (size_t i = 0; i < extension.size(); ++i) {
            char c = path[path.size() - extension.size() + i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != extension[i]) return false;
        }
        return true;
    };
    return endsWith(".replay") || endsWith(".bot") || endsWith(".chaos");
}

std::string QuoteArgument(const std::string& arg)
{
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
    // Backslashes are literal unless they precede a quote: double those, and escape the quote itself.
    std::string out = "\"";
    size_t slashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++slashes;
            continue;
        }
        out.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, '\\');
    return out + "\"";
}

} // namespace obl::launcher
