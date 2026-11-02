#include "expected_exit.h"

#include "game/game/game_accessors.h"
#include "game/game/diagnostics.h"
#include "core/log.h"
#include "platform/sdl_present.h"

#include <windows.h>

namespace obl::test {

bool ExecuteExpectedExitCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "close-window") {
        // The normal shutdown (WndProc WM_CLOSE -> WM_QUIT -> WinMain teardown -> entry's exit) without `quit`'s summary.
        OBL_LOG("autotest: close-window - posting WM_CLOSE");
        PostMessageW(platform::GetWin32Window(game::GetGameWindow()), WM_CLOSE, 0, 0);
        return true;
    }
    if (name != "fatal-error" && name != "show-error-exit") return false;
    std::string text;
    for (const auto& a : args) text += (text.empty() ? "" : " ") + a;
    OBL_LOG("autotest: %s - ending the game with '%s'", name.c_str(), text.c_str());
    if (name == "fatal-error") {
        game::FatalError("autotest fatal-error %s %d", text.c_str(), 42);
    } else {
        Text16 wide(text.begin(), text.end()); // script words are ASCII
        game::ShowErrorAndExit(game::GetGameWindow(), wide.c_str());
    }
    OBL_LOG("autotest: FAILED %s returned", name.c_str());
    return true;
}

} // namespace obl::test
