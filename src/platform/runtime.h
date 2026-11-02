#pragma once

#include "core/char16.h"
#include <cstdarg>
#include <cstddef>

// Host text conventions and process lifetime. Kept out of the simulation RNG.
namespace obl::platform {
int PrintText(Char16* out, const Char16* format, ...);
int PrintTextV(Char16* out, const Char16* format, va_list args);
int PrintNarrow(char* out, const char* format, ...);
int PrintNarrowV(char* out, const char* format, va_list args);
double ParseNumber(const Char16* text, Char16** end);
[[noreturn]] void Exit(int code);
void RequestQuit(int code);

// Test probes: whether `size` bytes at `pointer` are committed, readable memory (a wrong layout pointer fails a probe instead
// of crashing), and whether the process has loaded the module `name` (false where there are no such modules).
bool MemoryReadable(const void* pointer, size_t size);
bool ModuleLoaded(const Char16* name);

// Posts WM_CLOSE to the game window (the window's close button), or to the "Beach Life" window before the renderer has one.
// No-op without a message loop (Linux).
void PostGameWindowClose();
} // namespace obl::platform
