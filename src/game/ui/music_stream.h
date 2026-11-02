#pragma once

#include "game/ui/sound_types.h"

#include <cstdint>
#include <memory>

namespace obl::audio {

struct MusicStats {
    bool open = false, active = false, paused = false, eof = false, failed = false;
    double seconds = 0, duration = 0;
    int queued = 0, volume = 0;
    float gain = 0;
    uint64_t decoded = 0;
    double energy = 0;
    unsigned device = 0; // logical SDL device of the stream, 0 when closed or on Miles
};

// Owns its SDL audio reference, device, callback and incremental MP3 decoder.
class MusicStream {
public:
    MusicStream();
    ~MusicStream();
    MusicStream(const MusicStream&) = delete;
    MusicStream& operator=(const MusicStream&) = delete;
    bool Open(const char* path, float volume);
    void Close();
    void Pause();
    void Resume();
    void SetVolume(float volume);
    void Mute(bool muted);
    MusicStats Stats();
    // Test-only fast EOF coverage through the real decoder and real SDL queue.
    bool SeekForTest(double seconds);
    void* Handle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

MusicStats GetMusicStats();
bool SeekMusicForTest(double seconds);

} // namespace obl::audio
