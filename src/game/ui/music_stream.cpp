#include "game/ui/music_stream.h"

#include "game/ui/music_volume.h"
#include "core/log.h"

#include <SDL3/SDL.h>
#include <dr_mp3.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace obl::audio {

struct MusicStream::Impl {
    drmp3 decoder{};
    SDL_AudioStream* stream = nullptr;
    bool audio = false, initialized = false, paused = false, eof = false, failed = false, muted = false;
    uint64_t submitted = 0;
    int volume = 0;
    double duration = 0, energy = 0;
    std::array<float, 4096 * 2> pcm{};

    // SDL invokes this with the stream locked. Decoder and counters share that lock with the main thread.
    static void SDLCALL Fill(void* userdata, SDL_AudioStream* stream, int additional, int)
    {
        auto& impl = *static_cast<Impl*>(userdata);
        if (impl.eof || impl.failed || additional <= 0) return;
        const int frameBytes = static_cast<int>(impl.decoder.channels * sizeof(float));
        // Bound each request even if the device asks for an unexpectedly large buffer.
        int wanted = std::min(additional, static_cast<int>(impl.decoder.sampleRate) * frameBytes / 4);
        while (wanted > 0) {
            uint64_t frames =
                    drmp3_read_pcm_frames_f32(&impl.decoder, std::min(4096, (wanted + frameBytes - 1) / frameBytes), impl.pcm.data());
            if (!frames) {
                impl.eof = true;
                impl.failed = !SDL_FlushAudioStream(stream);
                break;
            }
            int bytes = static_cast<int>(frames) * frameBytes;
            if (!SDL_PutAudioStreamData(stream, impl.pcm.data(), bytes)) {
                impl.failed = true;
                break;
            }
            impl.submitted += frames;
            for (size_t i = 0; i < frames * impl.decoder.channels; ++i) impl.energy += static_cast<double>(impl.pcm[i]) * impl.pcm[i];
            wanted -= bytes;
        }
    }

    bool Finished() const { return failed || (eof && SDL_GetAudioStreamQueued(stream) == 0 && SDL_GetAudioStreamAvailable(stream) == 0); }

    void ApplyGain() const
    {
        if (stream) SDL_SetAudioStreamGain(stream, muted ? 0.0f : static_cast<float>(volume) / 127.0f);
    }
};

MusicStream::MusicStream() : impl_(std::make_unique<Impl>()) {}
MusicStream::~MusicStream()
{
    Close();
}

bool MusicStream::Open(const char* path, float volume)
{
    Close();
    auto& s = *impl_;
    if (!path || !drmp3_init_file(&s.decoder, path, nullptr)) return false;
    s.initialized = true;
    if (s.decoder.channels < 1 || s.decoder.channels > 2 || !s.decoder.sampleRate) {
        Close();
        return false;
    }
    // The frame-count scan restores the decoder position and never retains the song's PCM.
    s.duration = static_cast<double>(drmp3_get_pcm_frame_count(&s.decoder)) / s.decoder.sampleRate;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        OBL_LOG("music: SDL audio init failed: %s", SDL_GetError());
        Close();
        return false;
    }
    s.audio = true;
    SDL_AudioSpec spec{SDL_AUDIO_F32, static_cast<int>(s.decoder.channels), static_cast<int>(s.decoder.sampleRate)};
    s.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::Fill, &s);
    if (!s.stream) {
        OBL_LOG("music: SDL stream failed: %s (audio driver %s)", SDL_GetError(),
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");
        Close();
        return false;
    }
    SetVolume(volume);
    // Prime while paused: a file with no decodable frames must fail PlayStream, like an open error.
    SDL_LockAudioStream(s.stream);
    Impl::Fill(&s, s.stream, static_cast<int>(s.decoder.sampleRate * s.decoder.channels * sizeof(float) / 10), 0);
    bool valid = s.submitted > 0 && !s.failed;
    SDL_UnlockAudioStream(s.stream);
    if (!valid || !SDL_ResumeAudioStreamDevice(s.stream)) {
        Close();
        return false;
    }
    OBL_LOG("music: %s via SDL3/dr_mp3 (%u Hz, %u channels, %.3f s)", path, s.decoder.sampleRate, s.decoder.channels, s.duration);
    return true;
}

void MusicStream::Close()
{
    auto& s = *impl_;
    if (s.stream) {
        // Detaching the callback takes its lock and waits for the decoder's last read to finish.
        SDL_SetAudioStreamGetCallback(s.stream, nullptr, nullptr);
        SDL_DestroyAudioStream(s.stream);
        s.stream = nullptr;
    }
    if (s.initialized) drmp3_uninit(&s.decoder);
    if (s.audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    bool muted = s.muted;
    s = Impl{};
    s.muted = muted;
}

void MusicStream::Pause()
{
    auto& s = *impl_;
    if (!s.stream || !Stats().active) return;
    if (SDL_PauseAudioStreamDevice(s.stream)) s.paused = true;
}

void MusicStream::Resume()
{
    auto& s = *impl_;
    if (s.stream && s.paused && SDL_ResumeAudioStreamDevice(s.stream)) s.paused = false;
}

void MusicStream::SetVolume(float volume)
{
    impl_->volume = audio::MilesVolume(volume);
    impl_->ApplyGain();
}

void MusicStream::Mute(bool muted)
{
    impl_->muted = muted;
    impl_->ApplyGain();
}

MusicStats MusicStream::Stats()
{
    auto& s = *impl_;
    if (!s.stream) return {};
    SDL_LockAudioStream(s.stream);
    MusicStats result;
    result.open = true;
    result.active = !s.Finished();
    result.paused = result.active && s.paused;
    result.eof = s.eof;
    result.failed = s.failed;
    result.queued = SDL_GetAudioStreamQueued(s.stream);
    result.volume = s.volume;
    result.gain = SDL_GetAudioStreamGain(s.stream);
    result.duration = s.duration;
    result.decoded = s.submitted;
    result.energy = s.energy;
    result.device = SDL_GetAudioStreamDevice(s.stream);
    result.seconds = std::max(0.0,
            (static_cast<double>(s.submitted) - result.queued / static_cast<double>(s.decoder.channels * sizeof(float))) /
                    s.decoder.sampleRate);
    SDL_UnlockAudioStream(s.stream);
    return result;
}

bool MusicStream::SeekForTest(double seconds)
{
    auto& s = *impl_;
    if (!s.stream || seconds < 0 || !std::isfinite(seconds)) return false;
    SDL_LockAudioStream(s.stream);
    auto frame = static_cast<uint64_t>(std::min(seconds, s.duration) * s.decoder.sampleRate);
    bool ok = drmp3_seek_to_pcm_frame(&s.decoder, frame) != 0;
    if (ok) {
        SDL_ClearAudioStream(s.stream);
        s.submitted = frame;
        s.eof = s.failed = false;
    }
    SDL_UnlockAudioStream(s.stream);
    return ok;
}

void* MusicStream::Handle() const
{
    return impl_->stream;
}

} // namespace obl::audio
