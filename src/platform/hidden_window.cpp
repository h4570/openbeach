// Hidden mode for automated test runs (launcher --hidden -> OBL_HIDDEN=1). The game window is
// kept at the bottom of the z-order, never activated and without a taskbar/alt-tab entry, so the
// machine stays usable while the game runs. It must stay on the desktop: off-screen, the renderer stops presenting and
// PrintWindow captures nothing.
// Occluded is fine - PrintWindow(PW_RENDERFULLCONTENT) still captures it (tools/capture.ps1).

#include "platform/hidden_window.h"

#include "platform/import_redirect.h"
#include "core/log.h"

#include <windows.h>

namespace obl::platform {

namespace {

HWND g_gameWindow;
decltype(&SetWindowPos) g_setWindowPos;

decltype(&CreateWindowExA) g_createWindowEx;
HWND WINAPI CreateHiddenWindow(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width, int height,
        HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    bool topLevel = !parent && !(style & WS_CHILD) && windowName && lstrcmpA(windowName, "Beach Life") == 0;
    if (topLevel && !g_gameWindow) {
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        exStyle &= ~WS_EX_APPWINDOW;
    }
    HWND hwnd = g_createWindowEx(exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, param);
    if (topLevel && !g_gameWindow) {
        g_gameWindow = hwnd;
        g_setWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        OBL_LOG("hidden mode: game window %p kept at the bottom of the z-order", hwnd);
    }
    return hwnd;
}

decltype(&ShowWindow) g_showWindow;
BOOL WINAPI ShowHiddenWindow(HWND hwnd, int cmdShow)
{
    if (hwnd == g_gameWindow && cmdShow != SW_HIDE) cmdShow = SW_SHOWNOACTIVATE;
    return g_showWindow(hwnd, cmdShow);
}

BOOL WINAPI PositionHiddenWindow(HWND hwnd, HWND insertAfter, int x, int y, int cx, int cy, UINT flags)
{
    if (hwnd == g_gameWindow) {
        insertAfter = HWND_BOTTOM;
        flags = (flags | SWP_NOACTIVATE) & ~SWP_NOZORDER;
    }
    return g_setWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
}

// Error dialogs (e.g. "DirectX problem") would pop up on the user's desktop; log them instead.
decltype(&MessageBoxA) g_messageBox;
int WINAPI LogMessageBox(HWND, LPCSTR text, LPCSTR caption, UINT)
{
    OBL_LOG("hidden mode: suppressed MessageBoxA caption='%s' text='%s'", caption ? caption : "", text ? text : "");
    return IDOK;
}

// The callers are the game's own code and the SDL linked into it.
template<typename T> bool RedirectUser32Import(const char* name, T* replacement, T** previous)
{
    return platform::RedirectImport("user32.dll", name, replacement, previous);
}

bool g_hidden;

} // namespace

void AdoptHiddenGameWindow(HWND hwnd)
{
    if (!g_hidden || g_gameWindow) return;
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    SetWindowLongW(hwnd, GWL_EXSTYLE, (exStyle | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW);
    g_gameWindow = hwnd;
    g_setWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    OBL_LOG("hidden mode: SDL game window %p kept at the bottom of the z-order", hwnd);
}

bool StartHiddenMode()
{
    if (!GetEnvironmentVariableA("OBL_HIDDEN", nullptr, 0)) return true;
    OBL_LOG("hidden mode: on (OBL_HIDDEN)");
    g_hidden = true;
    return RedirectUser32Import("CreateWindowExA", &CreateHiddenWindow, &g_createWindowEx) &&
            RedirectUser32Import("ShowWindow", &ShowHiddenWindow, &g_showWindow) &&
            RedirectUser32Import("SetWindowPos", &PositionHiddenWindow, &g_setWindowPos) &&
            RedirectUser32Import("MessageBoxA", &LogMessageBox, &g_messageBox);
}

} // namespace obl::platform
