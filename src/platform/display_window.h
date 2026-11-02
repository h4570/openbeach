#pragma once

#include <windows.h>

#include "game/ui/display_geometry.h"
#include "platform/sdl_present.h"

// Win32 adapters over the portable display geometry: the presenter, cursor and input code work in RECT/POINT/SIZE.
namespace obl::platform {

inline RECT ToWinRect(const screen::Rect& r)
{
    return {r.left, r.top, r.right, r.bottom};
}

inline screen::Rect FromWinRect(const RECT& r)
{
    return {static_cast<int>(r.left), static_cast<int>(r.top), static_cast<int>(r.right), static_cast<int>(r.bottom)};
}

inline POINT MapClientToGame(POINT client, SIZE gameSize, const RECT& image)
{
    ui::DisplayPoint p = ui::MapClientToGame(ui::DisplayPoint{static_cast<int>(client.x), static_cast<int>(client.y)},
            ui::DisplaySize{static_cast<int>(gameSize.cx), static_cast<int>(gameSize.cy)}, FromWinRect(image));
    return {p.x, p.y};
}

inline POINT MapGameToClient(POINT point, SIZE gameSize, const RECT& image)
{
    ui::DisplayPoint p = ui::MapGameToClient(ui::DisplayPoint{static_cast<int>(point.x), static_cast<int>(point.y)},
            ui::DisplaySize{static_cast<int>(gameSize.cx), static_cast<int>(gameSize.cy)}, FromWinRect(image));
    return {p.x, p.y};
}

// The game image inside the client area of the game window `window` (the game's opaque handle, sdl_present.h).
inline RECT ClientImageRect(void* window, SIZE gameSize)
{
    int width = 0, height = 0;
    GameWindowClientSize(window, width, height);
    return ToWinRect(ui::ImageRect(gameSize.cx, gameSize.cy, width, height));
}

} // namespace obl::platform
