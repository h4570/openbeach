#pragma once

#include "game/ui/gui_canvas.h"

#include <string>
#include <vector>

namespace obl::test {

// Records what the game's sprite and text code is asked to draw, as the same `gui_canvas::Call` list a `RecordingCanvas` makes
// (docs/architecture/data-formats/converter/gui-runtime.md): `BeginGuiCapture`, run the draw, `EndGuiCapture`, then `gui_canvas::Compare` against a
// run on a RecordingCanvas. Both taps (sprite_observer, text_observer) are chained: events still reach whatever observer was
// installed before, and `EndGuiCapture` puts it back. Starting a capture clears the previous one.
void BeginGuiCapture();
void EndGuiCapture();
bool GuiCaptureActive();

// The calls seen since BeginGuiCapture (still readable after EndGuiCapture).
const std::vector<gui_canvas::Call>& GuiCaptureCalls();

// Instances created since BeginGuiCapture whose sprite's reference count is not back to (count after the first create of that
// sprite) - 1; read after the draw has released everything it made.
int GuiCaptureLeaked();

// gui-canvas-check: draws a fixed list of sprite and text operations once through `LiveCanvas` (the game's own functions,
// seen by the observers) and once through `RecordingCanvas`, and logs `autotest: gui-canvas-check calls=.. sprites=.. text=..
// wrapped=.. lines=.. mismatch=0|1 leaked=..` (`FAILED` plus the first difference on a mismatch). Needs a running game with a
// GUI manager (boot-gameplay).
bool ExecuteGuiCaptureCommand(const std::string& name, const std::vector<std::string>& args);

} // namespace obl::test
