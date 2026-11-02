#pragma once

#include "core/det_math.h"

#include <cmath>
#include <cstdint>

// Pure parts of the process start-up (WinMain, WndProc; docs/architecture/platform.md):
// command-line tail, show command, main-window geometry and the system commands WndProc swallows in full screen.
namespace obl::platform_main {

// entry: the command line WinMain gets is _acmdln without the program name. A quoted name ends at the closing quote (or at the
// end when unterminated), an unquoted one at the first byte <= ' '; then bytes 1..' ' are skipped. Bytes compare unsigned.
inline const char* SkipProgramName(const char* commandLine)
{
    auto* p = reinterpret_cast<const unsigned char*>(commandLine);
    if (*p == '"') {
        do ++p;
        while (*p && *p != '"');
        if (*p == '"') ++p;
    } else {
        while (*p > ' ') ++p;
    }
    while (*p && *p <= ' ') ++p;
    return reinterpret_cast<const char*>(p);
}

// entry: STARTUPINFO's wShowWindow when STARTF_USESHOWWINDOW (1) is set, SW_SHOWDEFAULT (10) otherwise.
inline int ShowCommand(uint32_t startupFlags, uint16_t showWindow)
{
    return (startupFlags & 1u) ? showWindow : 10;
}

// WinMain's CreateWindowExA arguments. Full screen: WS_EX_TOPMOST, WS_POPUP at 0,0, 800x600. Windowed: WS_OVERLAPPED at
// CW_USEDEFAULT with an 800x600 client area plus the sizing frame and caption (SM_CXFRAME, SM_CYFRAME, SM_CYCAPTION).
struct MainWindowGeometry {
    uint32_t exStyle;
    uint32_t style;
    int x, y, width, height;
};
inline MainWindowGeometry MainWindow(bool windowed, int frameX, int frameY, int caption)
{
    constexpr int kUseDefault = static_cast<int>(0x80000000u);
    if (!windowed) return {0x8u, 0x80000000u, 0, 0, 800, 600};
    return {0u, 0u, kUseDefault, kUseDefault, 800 + frameX * 2, 600 + caption + frameY * 2};
}

// WinMain's ShowWindow(nShowCmd) on a window SDL created: SDL shows it, and the minimizing commands (SW_SHOWMINIMIZED 2,
// SW_MINIMIZE 6, SW_SHOWMINNOACTIVE 7, SW_FORCEMINIMIZE 11) minimize it afterwards; SW_HIDE (0) leaves it hidden.
// Maximizing is not carried over: the renderer places the window for its resolution right after.
enum class ShowAction { Hide, Show, ShowMinimized };
inline ShowAction SdlShowAction(int showCommand)
{
    switch (showCommand) {
    case 0: return ShowAction::Hide;
    case 2:
    case 6:
    case 7:
    case 11: return ShowAction::ShowMinimized;
    default: return ShowAction::Show;
    }
}

// The fast sine table built at start-up: the sine of every 1/32 degree, read by no game code.
inline constexpr int kFastSinTableSize = det_math::kSineTableSize;

// The table is built with det_math::BuildSineTable: on every platform and rounding mode bit-identical to the hardware sine table the
// x86 build used to compute (docs/testing/determinism.md).
inline void BuildFastSinTable(float* table)
{
    det_math::BuildSineTable(table);
}

} // namespace obl::platform_main
