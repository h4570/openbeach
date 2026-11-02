#pragma once

#include "core/char16.h"

namespace obl::game {

// FatalError and ShowErrorAndExit (`owner` is the game window the dialog belongs to). Both log the
// message and end the process (not marked [[noreturn]]: a test trap may jump out of FatalError).
void FatalError(const char* format, ...);
void ShowErrorAndExit(void* owner, const Char16* text);
// A message box owned by `owner` (an opaque window handle) that the player confirms; the game continues afterwards.
// `information` adds the information icon. Hosts without dialogs (the Linux obl-sim) only log it.
void ShowNotice(void* owner, const char* text, const char* caption, bool information);
// Ends the process with a write access violation at address 0 (Windows: a raised exception the crash handler
// reports like the real one).
[[noreturn]] void RaiseAccessViolation();

// Test support: while a trap is set, FatalError logs and hands the formatted message to it instead of ending the game. The trap must not
// return.
using FatalErrorTrap = void (*)(const char* message);
void SetFatalErrorTrap(FatalErrorTrap trap);

} // namespace obl::game
