#include "cursor.h"

#include "core/cursor_file.h"
#include "core/log.h"
#include "core/os.h"
#include "core/text16.h"
#include "game/game/game_accessors.h"
#include "platform/display_window.h"
#include "platform/sdl_present.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace obl::platform {
namespace {

bool g_enabled;
bool g_hidden;
bool g_visible = true;
bool g_dragHidden;
SDL_Cursor* g_shape;
SDL_Cursor* g_applied;
bool g_appliedValid;

SDL_Window* GameSdlWindow()
{
    return SDL_GetWindowFromID(SdlGameWindowId());
}

void Apply(SDL_Cursor* cursor)
{
    if (g_appliedValid && cursor == g_applied) return;
    g_applied = cursor;
    g_appliedValid = true;
    if (!cursor) {
        SDL_HideCursor();
        return;
    }
    SDL_SetCursor(cursor);
    SDL_ShowCursor();
}

SDL_Cursor* CreateCursor(int resourceId)
{
    const std::string path = "remaster\\cursor_" + std::to_string(resourceId) + ".cur";
    std::vector<uint8_t> bytes;
    if (std::FILE* file = os::OpenFile(path, "rb")) {
        uint8_t buffer[4096];
        for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) bytes.insert(bytes.end(), buffer, buffer + n);
        std::fclose(file);
    }
    cursor_file::Image image;
    if (bytes.empty() || !cursor_file::Decode(bytes.data(), bytes.size(), image)) {
        OBL_LOG("cursor: remaster/cursor_%d.cur not loaded, import the game data again", resourceId);
        return nullptr;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_ARGB8888, image.argb.data(), image.width * 4);
    SDL_Cursor* cursor = surface ? SDL_CreateColorCursor(surface, image.hotX, image.hotY) : nullptr;
    if (surface) SDL_DestroySurface(surface);
    if (!cursor) OBL_LOG("cursor: SDL cursor for remaster/cursor_%d.cur failed: %s", resourceId, SDL_GetError());
    return cursor;
}

} // namespace

bool LocalCursorEnabled()
{
    return g_enabled;
}
bool LocalCursorVisible()
{
    return g_visible;
}

void SetLocalCursorVisible(bool visible)
{
    g_visible = visible;
    RefreshLocalCursor();
}

Text16 CursorFilePath(int resourceId)
{
    return u"remaster\\cursor_" + text16::Decimal(resourceId) + u".cur";
}

SDL_Cursor* LoadCursorFile(int resourceId)
{
    // One cursor per shape, created once SDL video runs; a failed load is retried on the next request.
    static std::map<int, SDL_Cursor*> cache;
    auto& cursor = cache[resourceId];
    if (!cursor && SDL_WasInit(SDL_INIT_VIDEO)) cursor = CreateCursor(resourceId);
    return cursor;
}

SDL_Cursor* ArrowCursor()
{
    return SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetDefaultCursor() : nullptr;
}

void SetLocalCursorShape(int shape)
{
    constexpr int kResources[] = {106, 101, 102, 103, 107, 108, 111};
    if (shape >= 1 && shape <= 7) {
        if (auto cursor = LoadCursorFile(kResources[shape - 1])) g_shape = cursor;
    }
    RefreshLocalCursor();
}

void SetLocalCursorDragHidden(bool hidden)
{
    if (g_dragHidden == hidden) return;
    g_dragHidden = hidden;
    RefreshLocalCursor();
}

SDL_Cursor* ResolveLocalCursor(int x, int y, bool clientArea)
{
    const ui::Renderer* renderer = game::GetRenderer();
    if (clientArea && g_dragHidden) return nullptr; // The grab point may lie over a bar; it is not the game's cursor position.
    if (!clientArea || !renderer) return ArrowCursor();
    const RECT image = ClientImageRect(game::GetGameWindow(), SIZE{renderer->m_width, renderer->m_height});
    if (x < image.left || x >= image.right || y < image.top || y >= image.bottom) return ArrowCursor();
    return g_visible ? (g_shape ? g_shape : ArrowCursor()) : nullptr;
}

void RefreshLocalCursor()
{
    // Hidden mode must never change the user's cursor; it is SDL's only while the mouse is over the game window.
    if (g_hidden) return;
    SDL_Window* window = GameSdlWindow();
    if (!window || SDL_GetMouseFocus() != window) return;
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    Apply(SDL_GetKeyboardFocus() == window ? ResolveLocalCursor(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), true)
                                           : ArrowCursor());
}

bool InitLocalCursor()
{
    g_enabled = true;
    OBL_LOG("input: LocalCursor=%d", g_enabled);
    g_hidden = os::HasEnv("OBL_HIDDEN");
    return true;
}

} // namespace obl::platform
