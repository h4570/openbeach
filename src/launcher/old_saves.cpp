#include "old_saves.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <thread>

extern char** environ;
#endif

#include <algorithm>
#include <regex>
#include <stdexcept>

namespace obl::launcher {
namespace {

#ifdef _WIN32
// CommandLineToArgvW/CRT quoting, including a directory's trailing backslash.
std::wstring Quote(const std::filesystem::path& path)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : path.native()) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"') result += L'\\';
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

void RunConverter(const std::filesystem::path& converter, const std::filesystem::path& old, const std::filesystem::path& pack,
        const std::filesystem::path& partial, std::chrono::milliseconds timeout)
{
    auto command = Quote(converter) + L" save " + Quote(old) + L" " + Quote(pack) + L" " + Quote(partial);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(converter.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        throw std::runtime_error("cannot start converter (Windows error " + std::to_string(GetLastError()) + ")");
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, static_cast<DWORD>(std::clamp<int64_t>(timeout.count(), 0, INFINITE - 1)));
    DWORD code = 1;
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        // The process must stop writing before its partial file is removed.
        WaitForSingleObject(process.hProcess, INFINITE);
    } else {
        GetExitCodeProcess(process.hProcess, &code);
    }
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0) throw std::runtime_error(wait == WAIT_TIMEOUT ? "converter timed out" : "converter wait failed");
    if (code != 0) throw std::runtime_error("converter exit code " + std::to_string(code));
    if (!std::filesystem::is_regular_file(partial)) throw std::runtime_error("converter produced no partial file");
}
#else
void RunConverter(const std::filesystem::path& converter, const std::filesystem::path& old, const std::filesystem::path& pack,
        const std::filesystem::path& partial, std::chrono::milliseconds timeout)
{
    const std::string arguments[] = {converter.string(), "save", old.string(), pack.string(), partial.string()};
    char* argv[] = {const_cast<char*>(arguments[0].c_str()), const_cast<char*>(arguments[1].c_str()),
            const_cast<char*>(arguments[2].c_str()), const_cast<char*>(arguments[3].c_str()), const_cast<char*>(arguments[4].c_str()),
            nullptr};
    pid_t child = 0;
    if (posix_spawn(&child, argv[0], nullptr, nullptr, argv, environ) != 0) throw std::runtime_error("cannot start converter");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    bool done = false;
    while (!done) {
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            done = true;
        } else if (result < 0) {
            throw std::runtime_error("converter wait failed");
        } else if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            // The process must stop writing before its partial file is removed.
            waitpid(child, &status, 0);
            throw std::runtime_error("converter timed out");
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    if (code != 0) throw std::runtime_error("converter exit code " + std::to_string(code));
    if (!std::filesystem::is_regular_file(partial)) throw std::runtime_error("converter produced no partial file");
}
#endif

} // namespace

OldSaveResult ConvertOldSaves(const std::filesystem::path& saves, const std::filesystem::path& pack, const std::filesystem::path& converter,
        const std::function<void(const std::string&)>& log, std::chrono::milliseconds timeout)
{
    namespace fs = std::filesystem;
    OldSaveResult result;
    if (!fs::is_directory(saves)) return result;
    const std::regex slot("save([0-9]+)\\.dat", std::regex::icase);
    struct Slot {
        fs::path path;
        std::string number;
    };
    std::vector<Slot> slots;
    for (const auto& entry : fs::directory_iterator(saves)) {
        std::smatch match;
        auto name = entry.path().filename().string();
        // A converter killed mid-write leaves its partial behind; no run is writing one at this point.
        if (entry.is_regular_file() && entry.path().extension() == ".partial" && name.size() > 13 && name.starts_with("save")) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
            continue;
        }
        if (entry.is_regular_file() && std::regex_match(name, match, slot)) slots.push_back({entry.path(), match[1]});
    }
    // Decimal strings avoid overflowing on a slot name with arbitrarily many digits.
    auto numeric = [](const std::string& n) {
        const auto first = n.find_first_not_of('0');
        return first == std::string::npos ? std::string("0") : n.substr(first);
    };
    std::sort(slots.begin(), slots.end(), [&](const Slot& a, const Slot& b) {
        auto x = numeric(a.number), y = numeric(b.number);
        if (x.size() != y.size()) return x.size() < y.size();
        return x == y ? a.number < b.number : x < y;
    });
    for (const auto& old : slots) {
        const auto name = old.path.filename().string();
        const auto stem = "save" + old.number;
        const auto target = saves / (stem + ".obl.save");
        const auto partial = saves / (stem + ".obl.save.partial");
        try {
            if (fs::exists(target)) continue;
            fs::remove(partial);
            RunConverter(fs::absolute(converter), fs::absolute(old.path), fs::absolute(pack), fs::absolute(partial), timeout);
            const auto archive = saves / "legacy";
            auto moved = archive / old.path.filename();
            for (unsigned copy = 2; fs::exists(moved); ++copy) moved = archive / (stem + "." + std::to_string(copy) + ".dat");
            fs::create_directories(archive);
            fs::rename(partial, target);
            try {
                fs::rename(old.path, moved);
            } catch (...) {
                // A failed archive move must remain retryable on the next start.
                std::error_code ignored;
                fs::remove(target, ignored);
                throw;
            }
            result.converted.push_back(name);
            if (log) log(name + " -> " + target.filename().string() + ", the old file in legacy/" + moved.filename().string());
        } catch (const std::exception& error) {
            std::error_code ignored;
            fs::remove(partial, ignored);
            result.failed.push_back(name);
            if (log) log(name + ": not converted, left in place: " + error.what());
        }
    }
    return result;
}

} // namespace obl::launcher
