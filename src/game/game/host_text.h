#pragma once

#include "core/char16.h"

// The host's wide text conventions (platform/runtime.h) for game modules, which may not include src/platform/: the format
// rounding and wcstod parsing the UI references were recorded with.
namespace obl::ui::host_text {
int Print(Char16* out, const Char16* format, ...);
int PrintNarrow(char* out, const char* format, ...);
double ParseNumber(const Char16* text, Char16** end);
} // namespace obl::ui::host_text

// The host's quit request (platform::RequestQuit), for the menu's Quit action.
namespace obl::ui::host {
void RequestQuit(int code);
} // namespace obl::ui::host
