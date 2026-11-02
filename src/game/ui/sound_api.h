#pragma once
#include "game/ui/sound_types.h"

namespace obl::audio {
// The Game owns the Sound (Game::m_sound); Context registers it for readers and temporary fixtures.
Sound* CurrentSound();
} // namespace obl::audio
