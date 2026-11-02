#pragma once

// Test probes of the audio output's suspension around long frames (audio_output.cpp).

namespace obl::platform {

struct AudioOutputStats {
    unsigned suspends = 0; // SuspendAudioOutput calls that suspended
    bool suspended = false;
};
const AudioOutputStats& GetAudioOutputStats();

} // namespace obl::platform
