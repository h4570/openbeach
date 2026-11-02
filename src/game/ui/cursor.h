#pragma once

#include "game/ui/cursor_types.h"

#include <cstdint>

namespace obl::ui {

// The software cursor operations the Game and the World call (the registered entry points forward to them). Construct registers g_Cursor and
// loads the cursor image; SetDragMode switches the Input's relative mode; IsInRect tests the screen position.
ui::SoftwareCursor* CursorConstruct(ui::SoftwareCursor* cursor, const char* tgaPath, void* owner);
void CursorDestruct(ui::SoftwareCursor* cursor);
bool CursorUpdate(ui::SoftwareCursor* cursor);
void CursorSetDragMode(ui::SoftwareCursor* cursor, uint32_t enable);
bool CursorIsInRect(ui::SoftwareCursor* cursor, int left, int top, int width, int height);
void CursorSetShape(ui::SoftwareCursor* cursor, int shape);
// Render ends the process through FatalError (the game's text) on an illegal shape or frame.
bool CursorRender(ui::SoftwareCursor* cursor);
void CursorSetSystemHidden(ui::SoftwareCursor* cursor, uint32_t hide);
// Writes the world position under the cursor to out[0..1] and returns out.
int* CursorGetPos(ui::SoftwareCursor* cursor, int* out);

} // namespace obl::ui
