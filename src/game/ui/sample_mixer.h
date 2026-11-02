#pragma once
#include "core/char16.h"
#include "game/ui/sound_types.h"
#include "game/ui/sample_stream.h"
#include <string>

namespace obl::audio {
// Reads the converted samples from <gameDirectory>/packs/base/sounds (converter/sounds.md).
void ConfigureSamples(const Text16& gameDirectory);
void CloseSamples(audio::Sound* sound);
void MuteSamples(bool muted);
SampleStats GetSampleStats(int channel);
// Reference count and file size of an entry of the current Sound's bank; false when the bank or entry is missing.
bool FindCurrentBankEntry(const char* wavName, int& references, uint32_t& size);
} // namespace obl::audio
