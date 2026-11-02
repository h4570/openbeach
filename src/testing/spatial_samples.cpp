#include "game/ui/sound_api.h"
#include "testing/spatial_samples.h"
#include "game/ui/music_stream.h"
#include "game/ui/sample_mixer.h"
#include "game/game/game_accessors.h"
#include "core/log.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace obl::test {
namespace {
using Sound = audio::Sound;
int g_channel = 16;
Sound* GetSound()
{
    return audio::CurrentSound();
}
int Count(Sound* sound)
{
    int count = 0;
    for (int i = 16; i < 24; ++i) count += sound->channels[i] != nullptr;
    return count;
}
void Contract(Sound* sound)
{
    auto check = [](bool ok, const char* label) { OBL_LOG("autotest: %s spatial-contract %s", ok ? "PASS" : "FAILED", label); };
    auto stop = [](Sound* s, auto... args) { return s->StopAllSamples(args...); };
    auto play = [](Sound* s, auto... args) { return s->Play3DSample(args...); };
    auto owner = [](Sound* s, auto... args) { return s->CountOwnerSamples(args...); };
    auto listener = [](Sound* s, auto... args) { return s->SetListenerPosition(args...); };
    auto volume = [](Sound* s, auto... args) { return s->SetChannelVolume(args...); };
    auto tick = [](Sound* s, auto... args) { return s->Update(args...); };
    auto fade = [](Sound* s, auto... args) { return s->FadeChannelOut(args...); };
    float xyz[3] = {100, 0, 0};
    sound->Mute();
    stop(sound);
    const auto enabled = sound->enabled;
    sound->enabled = 0;
    check(play(sound, "arrested001", 127, xyz, 2, 100) == -1, "disabled");
    check(owner(sound, 100) == 0, "disabled-owner");
    listener(sound, xyz);
    check(!std::memcmp(sound->position, xyz, sizeof(xyz)), "listener-disabled");
    sound->enabled = enabled;
    check(play(sound, "obl_missing", 127, xyz, 2, 100) == 16 && Count(sound) == 0, "missing-first-free");
    sound->SetEffectsVolume(1);
    const auto spatial = sound->spatial;
    sound->spatial = 0;
    check(play(sound, "arrested001", 127, xyz, 2, 100) == 16 && audio::GetSampleStats(16).active, "without-provider");
    stop(sound);
    sound->spatial = spatial;
    for (int i = 16; i < 24; ++i) {
        check(play(sound, "arrested001", 127, xyz, 2, i % 2 ? 101 : 100) == i, "first-free");
        check(sound->channels[i] && sound->channels[i]->owner == (i % 2 ? 101 : 100), "owner-record");
    }
    check(Count(sound) == 8 && play(sound, "arrested001", 127, xyz, 2, 102) == -1, "eight-channel-capacity");
    check(audio::GetSampleStats(16).active, "playing-sample");
    check(owner(sound, 100) == 4 && owner(sound, 101) == 4 && owner(sound, 102) == 0, "owner-counts");
    volume(sound, 16, 0.5f);
    check(audio::GetSampleStats(16).volume == 63, "volume");
    sound->SetEffectsVolume(0.25f);
    check(audio::GetSampleStats(16).volume == 63, "global-volume-preserves-3D");
    fade(sound, 16, 1);
    tick(sound, 0.5f);
    check(audio::GetSampleStats(16).volume == 31 && owner(sound, 100) == 4, "fade-keeps-owner");
    tick(sound, 0.5f);
    check(owner(sound, 100) == 4 && sound->channels[16]->fade == 2, "fade-zero-boundary");
    tick(sound, 0.001f);
    check(!sound->channels[16] && owner(sound, 100) == 3, "fade-release-owner");
    check(play(sound, "arrested001", 127, xyz, 2, 102) == 16 && owner(sound, 102) == 1, "reuse-owner");
    stop(sound);
    check(Count(sound) == 0 && owner(sound, 100) == 0 && owner(sound, 101) == 0 && owner(sound, 102) == 0, "stop-release-owners");
    sound->SetEffectsVolume(1);
}
// Energy handed to SDL must follow the stereo gains. Mono sources have a known base energy (the source energy);
// stereo sources are downmixed, so only the left/right ratio is comparable.
void ExpectMix(bool mono, float left, float right)
{
    const auto s = audio::GetSampleStats(g_channel);
    const double tolerance = 0.02;
    bool ok = s.open && s.energy > 0.001;
    if (mono) {
        ok = ok && std::abs(s.outLeft - s.energy * left * left) <= tolerance * s.energy + 1e-6 &&
                std::abs(s.outRight - s.energy * right * right) <= tolerance * s.energy + 1e-6;
    } else {
        if (left > 0 || right > 0) {
            const double a = s.outLeft * right * right, b = s.outRight * left * left;
            ok = ok && std::abs(a - b) <= tolerance * (a + b) + 1e-9 && (s.outLeft > 0) == (left > 0) && (s.outRight > 0) == (right > 0);
        } else
            ok = ok && s.outLeft == 0 && s.outRight == 0;
    }
    OBL_LOG("autotest: %s spatial-expect-mix %s gains=%.4f/%.4f energy=%.6f out=%.6f/%.6f", ok ? "PASS" : "FAILED",
            mono ? "mono" : "stereo", left, right, s.energy, s.outLeft, s.outRight);
}
std::string DeviceName(unsigned device)
{
    const char* name = device ? SDL_GetAudioDeviceName(device) : nullptr;
    return name ? name : "";
}
// One SDL device under music, a 2D channel and a 3D channel: distinct logical devices of the same physical one.
void MixedReport(Sound* sound, int channel2D, int channel3D)
{
    const auto music = audio::GetMusicStats();
    const auto s2 = audio::GetSampleStats(channel2D), s3 = audio::GetSampleStats(channel3D);
    const auto name = DeviceName(music.device);
    const int available = (music.device != 0) + (s2.device != 0) + (s3.device != 0);
    const bool shared = available == 3 && name == DeviceName(s2.device) && name == DeviceName(s3.device) && music.device != s2.device &&
            music.device != s3.device && s2.device != s3.device;
    const char* driver = SDL_GetCurrentAudioDriver();
    OBL_LOG("autotest: spatial-mixed available=%d shared=%d music-active=%d music-gain=%.6f music-seconds=%.6f "
            "music-volume=%d s2d-active=%d s2d-gain=%.6f s2d-seconds=%.6f s2d-volume=%d s3d-active=%d s3d-gain=%.6f "
            "s3d-seconds=%.6f s3d-volume=%d sound-music=%.6f driver=%s",
            available, shared, music.active, music.gain, music.seconds, music.volume, s2.active, s2.gain, s2.seconds, s2.volume, s3.active,
            s3.gain, s3.seconds, s3.volume, sound->musicVolume, driver ? driver : "-");
}
} // namespace
bool ReadSpatialSamplesProbe(const std::string& name, double& out)
{
    if (name == "spatial-occupied")
        out = GetSound() ? Count(GetSound()) : 0;
    else if (name == "spatial-active")
        out = audio::GetSampleStats(g_channel).active;
    else if (name == "spatial-seconds")
        out = audio::GetSampleStats(g_channel).seconds;
    else if (name == "spatial-out-left")
        out = audio::GetSampleStats(g_channel).outLeft;
    else if (name == "spatial-out-right")
        out = audio::GetSampleStats(g_channel).outRight;
    else
        return false;
    return true;
}
bool ExecuteSpatialSamplesCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (!name.starts_with("spatial-")) return false;
    auto* sound = GetSound();
    if (!sound) {
        OBL_LOG("autotest: FAILED spatial: Sound missing");
        return true;
    }
    if (name == "spatial-contract")
        Contract(sound);
    else if (name == "spatial-listener" && args.size() == 3) {
        float xyz[3];
        for (int i = 0; i < 3; ++i) xyz[i] = static_cast<float>(std::atof(args[i].c_str()));
        sound->SetListenerPosition(xyz);
    } else if ((name == "spatial-play" || name == "spatial-scaled") && args.size() == 6) {
        float xyz[3];
        for (int i = 0; i < 3; ++i) xyz[i] = static_cast<float>(std::atof(args[i + 1].c_str()));
        if (name == "spatial-play")
            g_channel = sound->Play3D(args[0].c_str(), xyz, static_cast<uint8_t>(std::atoi(args[4].c_str())), std::atoi(args[5].c_str()));
        else
            g_channel = sound->Play3DScaled(
                    args[0].c_str(), 0.5f, xyz, static_cast<uint8_t>(std::atoi(args[4].c_str())), std::atoi(args[5].c_str()));
        OBL_LOG("autotest: spatial-play channel=%d", g_channel);
    } else if (name == "spatial-owner" && args.size() == 1) {
        OBL_LOG("autotest: spatial-owner count=%d", sound->CountOwnerSamples(std::atoi(args[0].c_str())));
    } else if (name == "spatial-select" && args.size() == 1)
        g_channel = std::atoi(args[0].c_str());
    else if (name == "spatial-start")
        sound->StartChannel(g_channel);
    else if (name == "spatial-expect-mix" && args.size() == 3)
        ExpectMix(args[0] == "mono", static_cast<float>(std::atof(args[1].c_str())), static_cast<float>(std::atof(args[2].c_str())));
    else if (name == "spatial-bank-refs" && args.size() == 1) {
        int refs = -1;
        uint32_t size = 0;
        const bool available = audio::FindCurrentBankEntry(args[0].c_str(), refs, size);
        OBL_LOG("autotest: spatial-bank available=%d refs=%d size=%u cached=%u", available, refs, size, sound->bank->cached);
    } else if (name == "spatial-shared-data" && args.size() == 2) {
        const int a = std::atoi(args[0].c_str()), b = std::atoi(args[1].c_str());
        const auto* ca = a >= 0 && a < 24 ? sound->channels[a].get() : nullptr;
        const auto* cb = b >= 0 && b < 24 ? sound->channels[b].get() : nullptr;
        OBL_LOG("autotest: spatial-shared-data both=%d same-data=%d same-size=%d", ca && cb, ca && cb && ca->data == cb->data,
                ca && cb && ca->size == cb->size);
    } else if (name == "spatial-mixed-report" && args.size() == 2)
        MixedReport(sound, std::atoi(args[0].c_str()), std::atoi(args[1].c_str()));
    else if (name == "spatial-report") {
        auto s = audio::GetSampleStats(g_channel);
        OBL_LOG("autotest: spatial provider=%d enabled=%d occupied=%d channel=%d active=%d volume=%d gain=%.6f "
                "left=%.6f right=%.6f distance=%.6f seconds=%.6f energy=%.9f out-left=%.9f out-right=%.9f",
                sound->spatial, sound->enabled, Count(sound), g_channel, s.active, s.volume, s.gain, s.left, s.right, s.distance, s.seconds,
                s.energy, s.outLeft, s.outRight);
    } else
        return false;
    return true;
}
} // namespace obl::test
