#pragma once
#include "core/char16.h"
#include "game/ui/audio_types.h"
#include <string>
namespace obl::audio {
// Selects the pack the sound script is read from and registers the scripted runtime check.
void ConfigureScriptedSound(const Text16& gameDirectory);
} // namespace obl::audio
