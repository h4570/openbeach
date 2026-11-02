#include "game/ui/sound_api.h"
#include "game/ui/music_stream.h"
#include "game/ui/sample_mixer.h"

#include "core/log.h"
#include "game/ui/music_volume.h"

#include <cstdint>

namespace obl::audio {
namespace {

// The one music stream of the process; Sound keeps only the handle and the enabled/volume state.
MusicStream g_music;

} // namespace

char Sound::PlayStream(const char* path)
{
    if (!enabled) return 0;
    g_music.Close();
    stream = nullptr;
    if (!g_music.Open(path, musicVolume)) {
        OBL_LOG("music: cannot play %s", path ? path : "(null)");
        return 0;
    }
    stream = g_music.Handle();
    return 1;
}

char Sound::IsStreamActive()
{
    return enabled && stream && g_music.Stats().active;
}

char Sound::IsStreamStopped()
{
    return enabled && stream && g_music.Stats().paused;
}

void Sound::PauseStream()
{
    if (enabled && stream) g_music.Pause();
}

void Sound::ResumeStream()
{
    if (enabled && stream) g_music.Resume();
}

void Sound::CloseStream()
{
    if (enabled && stream) {
        g_music.Close();
        stream = nullptr;
    }
}

void Sound::SetMusicVolume(float value)
{
    musicVolume = audio::ClampVolume(value);
    if (enabled && stream) g_music.SetVolume(musicVolume);
}

void Sound::Mute()
{
    MuteSamples(true);
    if (enabled) g_music.Mute(true);
}

void Sound::Unmute()
{
    MuteSamples(false);
    if (enabled) g_music.Mute(false);
}

void Sound::ShutdownMusic()
{
    g_music.Close();
    stream = nullptr;
}

MusicStats GetMusicStats()
{
    return g_music.Stats();
}

bool SeekMusicForTest(double seconds)
{
    return g_music.SeekForTest(seconds);
}

} // namespace obl::audio
