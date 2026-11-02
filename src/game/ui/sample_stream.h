#pragma once
#include "game/ui/sound_bank.h"
#include "game/ui/spatial_audio.h"
#include <cstdint>
#include <memory>

namespace obl::audio {
struct SampleStats {
    bool open = false, active = false, loop = false, failed = false;
    int volume = 0;
    float gain = 0;
    double seconds = 0, energy = 0;
    uint64_t submitted = 0;
    float left = 1, right = 1, distance = 0;
    // Energy of the PCM handed to SDL (after spatialization, before gain); stereo output only splits L/R.
    double outLeft = 0, outRight = 0;
    unsigned device = 0; // logical SDL device of the stream, 0 when closed
};
// The audio backend (src/platform/sample_stream.cpp) behind one sample channel.
class SampleStream {
public:
    SampleStream();
    ~SampleStream();
    bool Open(audio::Wave wave, bool loop, int volume, bool muted, const audio::SpatialMix* spatial = nullptr);
    void Close();
    void Start();
    void Volume(int volume);
    void Mute(bool muted);
    void Spatial(audio::SpatialMix mix);
    SampleStats Stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// The backend's message for the last failed Open, for the log.
const char* SampleStreamError();
} // namespace obl::audio
