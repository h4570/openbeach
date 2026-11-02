#pragma once

#include "core/char16.h"

struct SDL_Cursor;

namespace obl::platform {

bool InitLocalCursor();
bool LocalCursorEnabled();
bool LocalCursorVisible();
// The SDL backend of the Input object (input_backend.h): showing and hiding the system cursor and setting the cursor shape
// (the shapes come from files, not from the module's resources).
void SetLocalCursorVisible(bool visible);
void SetLocalCursorShape(int shape);
// The files obl-convert resources wrote from the executable's resources (relative to the game directory).
Text16 CursorFilePath(int resourceId);
// The SDL colour cursor of one file (core/cursor_file.h), created once; null when the file is missing or SDL video is not up.
SDL_Cursor* LoadCursorFile(int resourceId);
// SDL's default (arrow) cursor: the bars, window borders and an unfocused window keep it.
SDL_Cursor* ArrowCursor();
// SDL relative drag hides the cursor; the game's shape must not show at the grab point meanwhile.
void SetLocalCursorDragHidden(bool hidden);

// The cursor for game-window client point (x, y) (`clientArea` false: a window border), null = hidden. Shared by
// RefreshLocalCursor and the hidden contract test.
SDL_Cursor* ResolveLocalCursor(int x, int y, bool clientArea);
// Applies the resolved cursor at SDL's mouse position (mouse motion in the game window, state changes). Hidden runs never
// change the user's cursor, and an unfocused window shows the arrow.
void RefreshLocalCursor();

} // namespace obl::platform
