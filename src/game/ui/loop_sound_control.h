#pragma once
#include "game/ui/loop_sound_types.h"
namespace obl::audio {
// The Game owns the LoopSounds (Game::m_loopSounds); game::Context::loopSounds mirrors it.
audio::LoopSounds* CurrentLoopSounds();
// The Construct and Destruct of the owner set the current sounds (game::Context); a scratch object does not bind them.
void SetCurrentLoopSounds(audio::LoopSounds* sound);
// Registers the scripted check behind `call-drive ui.loop-sounds`.
void ConfigureLoopSounds();
} // namespace obl::audio
