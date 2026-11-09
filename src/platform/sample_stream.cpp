#include "game/ui/sample_stream.h"
#include "core/log.h"
#include <SDL3/SDL.h>
#include <utility>
#include <algorithm>
#include <array>

namespace obl::audio {
struct SampleStream::Impl {
    audio::Wave wave;
    SDL_AudioStream* stream = nullptr;
    size_t cursor = 0;
    uint64_t submitted = 0;
    double energy = 0, outEnergy[2] = {0, 0};
    int volume = 0;
    bool spatial = false;
    audio::SpatialMix mix;
    bool audio = false, loop = false, eof = false, failed = false, muted = false;
    static void SDLCALL Fill(void* data, SDL_AudioStream* stream, int additional, int)
    {
        auto& s = *static_cast<Impl*>(data);
        if (additional <= 0 || s.eof || s.failed) return;
        int frameBytes = (s.spatial ? 2 : s.wave.channels) * sizeof(int16_t);
        size_t wanted = static_cast<size_t>(std::min(additional, s.wave.rate * frameBytes / 4)) / frameBytes * s.wave.channels;
        while (wanted) {
            if (s.cursor == s.wave.pcm.size()) {
                if (s.loop)
                    s.cursor = 0;
                else {
                    s.eof = true;
                    s.failed = !SDL_FlushAudioStream(stream);
                    break;
                }
            }
            size_t count = std::min({wanted, s.wave.pcm.size() - s.cursor, size_t(4096 * s.wave.channels)});
            std::array<int16_t, 4096 * 2> stereo;
            const int16_t* pcm = s.wave.pcm.data() + s.cursor;
            size_t outputCount = count;
            if (s.spatial) {
                outputCount = count / s.wave.channels * 2;
                for (size_t frame = 0; frame < count / s.wave.channels; ++frame) {
                    int sum = 0;
                    for (int c = 0; c < s.wave.channels; ++c) sum += pcm[frame * s.wave.channels + c];
                    const float mono = static_cast<float>(sum) / s.wave.channels;
                    stereo[frame * 2] = static_cast<int16_t>(mono * s.mix.left);
                    stereo[frame * 2 + 1] = static_cast<int16_t>(mono * s.mix.right);
                }
                pcm = stereo.data();
            }
            if (!SDL_PutAudioStreamData(stream, pcm, static_cast<int>(outputCount * sizeof(int16_t)))) {
                s.failed = true;
                break;
            }
            for (size_t i = 0; i < count; ++i) {
                double value = s.wave.pcm[s.cursor + i] / 32768.0;
                s.energy += value * value;
            }
            const int outChannels = s.spatial ? 2 : s.wave.channels;
            for (size_t i = 0; i < outputCount; ++i) {
                double value = pcm[i] / 32768.0;
                s.outEnergy[outChannels > 1 && i % outChannels == 1] += value * value;
            }
            s.cursor += count;
            s.submitted += count / s.wave.channels;
            wanted -= count;
        }
    }
    void Gain() { SDL_SetAudioStreamGain(stream, muted ? 0.0f : volume / 127.0f); }
};
SampleStream::SampleStream() : impl_(std::make_unique<Impl>()) {}
SampleStream::~SampleStream()
{
    Close();
}
bool SampleStream::Open(audio::Wave wave, bool loop, int volume, bool muted, const audio::SpatialMix* spatial)
{
    Close();
    auto& s = *impl_;
    if (wave.pcm.empty()) return false;
    s.wave = std::move(wave);
    s.loop = loop;
    s.volume = std::clamp(volume, 0, 127);
    s.muted = muted;
    s.spatial = spatial != nullptr;
    if (spatial) s.mix = *spatial;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        Close();
        return false;
    }
    s.audio = true;
    SDL_AudioSpec spec{SDL_AUDIO_S16, s.spatial ? 2 : s.wave.channels, s.wave.rate};
    s.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::Fill, &s);
    if (!s.stream) {
        OBL_LOG("samples: SDL stream failed: %s (audio driver %s)", SDL_GetError(),
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");
        Close();
        return false;
    }
    // Once: the driver SDL picked shows at once when it fell back to ALSA (no sound server reachable).
    static bool driverLogged = false;
    if (!std::exchange(driverLogged, true)) OBL_LOG("samples: SDL audio driver %s", SDL_GetCurrentAudioDriver());
    s.Gain();
    Start();
    if (Stats().failed) {
        Close();
        return false;
    }
    return true;
}
void SampleStream::Close()
{
    auto& s = *impl_;
    if (s.stream) {
        SDL_SetAudioStreamGetCallback(s.stream, nullptr, nullptr);
        SDL_DestroyAudioStream(s.stream);
    }
    if (s.audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    s = Impl{};
}
void SampleStream::Start()
{
    auto& s = *impl_;
    if (!s.stream) return;
    SDL_LockAudioStream(s.stream);
    SDL_ClearAudioStream(s.stream);
    s.cursor = 0;
    s.submitted = 0;
    s.energy = 0;
    s.outEnergy[0] = s.outEnergy[1] = 0;
    s.eof = s.failed = false;
    Impl::Fill(&s, s.stream, s.wave.rate * (s.spatial ? 2 : s.wave.channels) * sizeof(int16_t) / 10, 0);
    SDL_UnlockAudioStream(s.stream);
    // Resuming takes the device lock, which the audio thread holds while it waits for the stream lock: calling it
    // under the stream lock was an ABBA deadlock (world_script_s12 hang).
    if (!SDL_ResumeAudioStreamDevice(s.stream)) {
        SDL_LockAudioStream(s.stream);
        s.failed = true;
        SDL_UnlockAudioStream(s.stream);
    }
}
void SampleStream::Volume(int volume)
{
    auto& s = *impl_;
    if (!s.stream) return;
    SDL_LockAudioStream(s.stream);
    s.volume = std::clamp(volume, 0, 127);
    s.Gain();
    SDL_UnlockAudioStream(s.stream);
}
void SampleStream::Mute(bool muted)
{
    auto& s = *impl_;
    if (!s.stream) return;
    SDL_LockAudioStream(s.stream);
    s.muted = muted;
    s.Gain();
    SDL_UnlockAudioStream(s.stream);
}
void SampleStream::Spatial(audio::SpatialMix mix)
{
    auto& s = *impl_;
    if (!s.stream) return;
    SDL_LockAudioStream(s.stream);
    s.mix = mix;
    SDL_UnlockAudioStream(s.stream);
}
SampleStats SampleStream::Stats()
{
    auto& s = *impl_;
    if (!s.stream) return {};
    SDL_LockAudioStream(s.stream);
    SampleStats result;
    result.open = true;
    result.failed = s.failed;
    result.active = !s.failed && !(s.eof && SDL_GetAudioStreamQueued(s.stream) == 0 && SDL_GetAudioStreamAvailable(s.stream) == 0);
    result.loop = s.loop;
    result.volume = s.volume;
    result.gain = SDL_GetAudioStreamGain(s.stream);
    result.submitted = s.submitted;
    result.energy = s.energy;
    result.outLeft = s.outEnergy[0];
    result.outRight = s.outEnergy[1];
    result.device = SDL_GetAudioStreamDevice(s.stream);
    result.left = s.mix.left;
    result.right = s.mix.right;
    result.distance = s.mix.distance;
    result.seconds = std::max(0.0,
            (static_cast<double>(s.submitted) -
                    SDL_GetAudioStreamQueued(s.stream) / static_cast<double>((s.spatial ? 2 : s.wave.channels) * sizeof(int16_t))) /
                    s.wave.rate);
    SDL_UnlockAudioStream(s.stream);
    return result;
}
const char* SampleStreamError()
{
    return SDL_GetError();
}
} // namespace obl::audio
