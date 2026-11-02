#pragma once

#include "game/game/game_host.h"

namespace obl::platform {

// IntroMovieStart/Frame/Finish: game/game/game_host.h.

enum class IntroMoviePlayer { None = 0, MpegSdl = 1 };

struct IntroMovieStats {
    IntroMoviePlayer player = IntroMoviePlayer::None; // None: not played (PlayMovie=0, unreadable file)
    bool complete = false; // shown to the end (not skipped, aborted or unrenderable)
    bool skipped = false; // skip key/click
    unsigned frames = 0; // decoded frames (pl_mpeg only)
    double seconds = 0, duration = 0; // playback wall time / stream duration (pl_mpeg only)
    bool audio = false; // SDL audio stream played the sound track (pl_mpeg only)
    bool audioClock = false; // the audio position paced the video to the end (else the wall clock)
    double audioSeconds = 0; // sound consumed by the device
    bool audioOpen = false; // SDL audio subsystem still initialised after the movie (must be false)
    unsigned destructCalls = 0; // IntroMovieFinish calls (Game construction, after the movie)
};

const IntroMovieStats& GetIntroMovieStats();

} // namespace obl::platform
