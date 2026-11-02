#include "standalone_host.h"

#include "dll/init.h"
#include "platform/host_init.h"
#include "platform/platform_main.h"
#include "old_saves.h"
#include "core/pack_path.h"

#include <SDL3/SDL_filesystem.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace obl::standalone {

namespace {

namespace fs = std::filesystem;

fs::path Utf8Path(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string Utf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// The game directory, absolute, with a trailing separator.
fs::path DirectoryPrefix(const std::string& dir)
{
    std::error_code error;
    fs::path path = fs::absolute(Utf8Path(dir), error);
    if (error) path = Utf8Path(dir);
    path.make_preferred();
    return path.has_filename() ? path / "" : path;
}

fs::path HostDirectory()
{
    const char* base = SDL_GetBasePath();
    return base ? fs::path(reinterpret_cast<const char8_t*>(base)) : fs::path();
}

void ConvertStartupSaves(const std::string& gameDir)
{
    auto log = [](const std::string& line) { std::fprintf(stderr, "old saves: %s\n", line.c_str()); };
    // Script hosts share the development saves; migration would move their corpus out from under other tests.
    if (std::getenv("OBL_SCRIPT") || std::getenv("OBL_HEADLESS")) {
        log("skipped for scripted/headless test host");
        return;
    }
    // Capture and manual runs on run/ share the save corpus too (--keep-saves).
    if (std::getenv("OBL_KEEP_SAVES")) {
        log("skipped by --keep-saves");
        return;
    }
    try {
        const auto directory = DirectoryPrefix(gameDir).parent_path();
        std::string error;
        if (!pack_path::CheckSelected(directory, error)) {
            log("not converted: " + error);
            return;
        }
        const auto pack = directory / pack_path::Selected();
#ifdef _WIN32
        const auto converter = HostDirectory() / "obl-convert.exe";
#else
        const auto converter = HostDirectory() / "obl-convert";
#endif
        if (!fs::is_regular_file(pack / "pack.json")) {
            log("not converted: missing pack " + Utf8(pack));
            return;
        }
        if (!fs::is_regular_file(converter)) {
            log("not converted: missing converter " + Utf8(converter));
            return;
        }
        launcher::ConvertOldSaves(directory / "SaveGames", pack, converter, log);
    } catch (const std::exception& error) {
        // Directory enumeration and prerequisite checks can fail too; migration must never prevent playing.
        log(std::string("not converted: ") + error.what());
    }
}

fs::path LogPath(const std::string& gameDir)
{
    return DirectoryPrefix(gameDir) / fs::path(reinterpret_cast<const char16_t*>(host::kLogFileName));
}

// The log lines the game appended since `offset` that tell whether start-up finished: "init done" and any failure.
bool CheckInitLog(const std::string& gameDir, std::streamoff offset)
{
    std::ifstream log(LogPath(gameDir), std::ios::binary);
    log.seekg(offset);
    bool done = false;
    int failures = 0;
    for (std::string line; std::getline(log, line);) {
        const bool failed = line.find("FAILED") != std::string::npos;
        const bool summary = line.find("init done") != std::string::npos;
        if (failed || summary) std::printf("standalone host: log: %s\n", line.c_str());
        done |= summary;
        failures += failed;
    }
    if (!done) std::fprintf(stderr, "error: standalone host: no \"init done\" in %s\n", Utf8(LogPath(gameDir)).c_str());
    return done && failures == 0;
}

// The game's start-up in this process (OpenGame, dllmain.cpp); its log must show "init done" and no failure.
bool InitGameInProcess(const std::string& gameDir)
{
    std::error_code error;
    const auto size = fs::file_size(LogPath(gameDir), error);
    const std::streamoff offset = error ? 0 : static_cast<std::streamoff>(size);
    const int result = OpenGame(DirectoryPrefix(gameDir));
    std::printf("standalone host: start-up returned %d\n", result);
    return CheckInitLog(gameDir, offset) && result == 0;
}

} // namespace

#ifdef _WIN32
int Launch(const std::string& gameDir, const std::string& gameArgs, bool wait, bool runEntry)
{
    // The host starts in the game directory, so it gets the directory as an absolute path.
    const std::wstring dir = DirectoryPrefix(gameDir).parent_path().wstring();
    const std::wstring args = Utf8Path(gameArgs).wstring();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    // tools/common.ps1 (Get-RunGame) finds the host of a game directory by this flag and the quoted --game-dir.
    std::wstring cmdLine = L"\"" + std::wstring(self) + L"\" --standalone-host --game-dir \"" + dir + L"\"";
    if (!runEntry) cmdLine += L" --no-entry";
    if (!args.empty()) cmdLine += L" --" + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::fflush(stdout); // the host writes to the same console
    if (!CreateProcessW(self, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        std::fprintf(stderr, "error: standalone: CreateProcessW (host) (GetLastError=%lu)\n", GetLastError());
        return 1;
    }
    CloseHandle(pi.hThread);

    DWORD exitCode = 0;
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        std::printf("standalone host exited with code %lu (0x%08lX)\n", exitCode, exitCode);
    }
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}
#else
int Launch(const std::string& gameDir, const std::string& gameArgs, bool wait, bool runEntry)
{
    // One process: the game runs here, in its directory (the game opens its data by relative paths).
    if (!wait) std::fprintf(stderr, "note: --no-wait has no effect on this system\n");
    const fs::path dir = DirectoryPrefix(gameDir);
    if (chdir(dir.c_str()) != 0) {
        std::fprintf(stderr, "error: cannot enter the game directory %s\n", dir.c_str());
        return kExitInitFailed;
    }
    return HostMain(gameDir, gameArgs, runEntry);
}
#endif

int HostMain(const std::string& gameDir, const std::string& gameArgs, bool runEntry)
{
    ConvertStartupSaves(gameDir);
    if (!InitGameInProcess(gameDir)) return kExitInitFailed;
    // The game reads no command line; its arguments (after `--`) are only reported. One line, so ctest can match the ready
    // state and the arguments in one expression.
    std::printf("standalone host: ready, game arguments [%s]%s\n", gameArgs.c_str(), runEntry ? "" : " (--no-entry)");
    if (!runEntry) return 0;
    std::fflush(stdout);
    return platform::RunGame();
}

} // namespace obl::standalone
