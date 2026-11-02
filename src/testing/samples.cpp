#include "core/char16.h"
#include "game/ui/sound_api.h"
#include "testing/samples.h"
#include "testing/spatial_samples.h"
#include "platform/runtime.h"
#include "game/ui/music_stream.h"
#include "platform/sound_lifecycle.h"
#include "game/ui/sample_mixer.h"
#include "game/game/game_accessors.h"
#include "core/log.h"
#include <cstdlib>
#include <cstring>
#include <memory>

namespace obl::test {
namespace {
using Sound = audio::Sound;
int g_channel = 0;
Sound* GetSound()
{
    return audio::CurrentSound();
}
int Count()
{
    int count = 0;
    if (auto* sound = GetSound())
        for (int i = 0; i < 16; ++i) count += sound->channels[i] != nullptr;
    return count;
}
void Report()
{
    auto* sound = GetSound();
    auto s = audio::GetSampleStats(g_channel);
    auto* c = g_channel >= 0 && g_channel < 16 ? sound->channels[g_channel].get() : nullptr;
    OBL_LOG("autotest: samples channel=%d occupied=%d active=%d volume=%d gain=%.6f seconds=%.6f "
            "energy=%.9f submitted=%llu fade=%d remaining=%.6f scale=%.6f cached=%u",
            g_channel, Count(), s.active, s.volume, s.gain, s.seconds, s.energy, s.submitted, c ? c->fade : 0, c ? c->remaining : 0,
            c ? c->scale : 0, sound->bank->cached);
}
// Sound::Play/PlayScaled/FindChannel/HasSample/CountOwnerSamples/GetEffectsVolume return the values the scenario expects.
void Queries(Sound* sound)
{
    auto check = [](bool ok, const char* label) { OBL_LOG("autotest: %s samples-contract %s", ok ? "PASS" : "FAILED", label); };
    auto stop = [](Sound* s, auto... args) { return s->StopAllSamples(args...); };
    auto effects = [](Sound* s, auto... args) { return s->SetEffectsVolume(args...); };
    auto scaled = [](Sound* s, auto... args) { return s->PlayScaled(args...); };
    auto plain = [](Sound* s, auto... args) { return s->Play(args...); };
    auto find = [](Sound* s, auto... args) { return s->FindChannel(args...); };
    auto has = [](Sound* s, auto... args) { return s->HasSample(args...); };
    auto count = [](Sound* s, auto... args) { return s->CountOwnerSamples(args...); };
    auto volume = [](Sound* s, auto... args) { return s->GetEffectsVolume(args...); };
    stop(sound);
    effects(sound, 0.5f);
    check(volume(sound) == 0.5f, "get-effects-volume");
    check(find(sound, "waves_loop") == -1 && has(sound, "waves_loop") && !has(sound, "obl_missing"), "query-empty");
    const int a = scaled(sound, "waves_loop", 0.5f, 2, 0);
    const auto* record = a >= 0 ? sound->channels[a].get() : nullptr;
    check(a == 0 && record && record->scale == 0.5f && record->target == 31 && record->fade == 0, "play-scaled-record");
    check(audio::GetSampleStats(a).volume == 31, "play-scaled-volume");
    check(find(sound, "waves_loop") == a && find(sound, "WAVES_LOOP") == a, "find-channel-case");
    const int b = plain(sound, "waves_loop", 2, 1);
    check(b == 1 && sound->channels[b]->scale == 0.5f && sound->channels[b]->fade == 1, "play-effects-volume");
    sound->channels[a]->owner = 7;
    check(count(sound, 7) == 1 && count(sound, -1) == 1 && count(sound, 0) == 0, "count-owner");
    sound->enabled = 0;
    check(scaled(sound, "waves_loop", 1, 2, 0) == -1 && find(sound, "waves_loop") == -1 && count(sound, 7) == 0, "queries-disabled");
    sound->enabled = 1;
    stop(sound);
    effects(sound, 1);
}
void Contract(Sound* sound)
{
    auto check = [](bool ok, const char* label) { OBL_LOG("autotest: %s samples-contract %s", ok ? "PASS" : "FAILED", label); };
    auto stop = [](Sound* s, auto... args) { return s->StopAllSamples(args...); };
    auto effects = [](Sound* s, auto... args) { return s->SetEffectsVolume(args...); };
    auto play = [](Sound* s, auto... args) { return s->Play(args...); };
    auto tick = [](Sound* s, auto... args) { return s->UpdateFades(args...); };
    auto out = [](Sound* s, auto... args) { return s->FadeChannelOut(args...); };
    auto fade = [](Sound* s, auto... args) { return s->FadeChannelTo(args...); };
    sound->Mute();
    stop(sound);
    sound->enabled = 0;
    check(play(sound, "waves_loop", 2, 0) == -1, "disabled");
    effects(sound, -1);
    check(sound->effectsScale == 0 && sound->effectsVolume == 0, "clamp-disabled");
    sound->enabled = 1;
    effects(sound, 2);
    check(sound->effectsScale == 1 && sound->effectsVolume == 127, "clamp-high");
    check(play(sound, "obl_missing", 0, 0) == 0 && Count() == 0, "missing-return-index");
    int i = play(sound, "waves_loop", 2, 2);
    check(i == 0 && Count() == 1 && audio::GetSampleStats(i).volume == 0, "fade-in-start");
    tick(sound, 1);
    check(audio::GetSampleStats(i).volume == 63 && sound->channels[i]->fade == 1, "fade-in-half");
    tick(sound, 1);
    check(audio::GetSampleStats(i).volume == 127 && sound->channels[i]->fade == 1, "fade-in-zero-boundary");
    tick(sound, 0.001f);
    check(sound->channels[i]->fade == 0, "fade-in-finish");
    fade(sound, i, 0.5f, 2);
    effects(sound, 0.5f);
    check(audio::GetSampleStats(i).volume == 127, "global-volume-preserves-active-fade");
    tick(sound, 1);
    check(audio::GetSampleStats(i).volume == 95, "fade-to-half");
    tick(sound, 1.001f);
    check(audio::GetSampleStats(i).volume == 63 && sound->channels[i]->fade == 0, "fade-to-target");
    effects(sound, 0.5f);
    check(audio::GetSampleStats(i).volume == 31, "global-volume-scales-record");
    sound->SetChannelVolume(i, 0.02f);
    check(audio::GetSampleStats(i).volume == 0, "silent-threshold");
    sound->SetChannelVolume(i, 1);
    effects(sound, 1);
    out(sound, i, 2);
    tick(sound, 1);
    check(audio::GetSampleStats(i).volume == 63, "fade-out-half");
    tick(sound, 1);
    check(audio::GetSampleStats(i).volume == 0 && audio::GetSampleStats(i).active && sound->channels[i]->fade == 2,
            "fade-out-zero-boundary");
    tick(sound, 0.001f);
    check(!audio::GetSampleStats(i).active, "fade-out-stop");
    sound->Update(0);
    check(Count() == 0, "finished-channel-release");
    i = play(sound, "waves_loop", 2, 0);
    out(sound, i, 0);
    tick(sound, 0);
    check(audio::GetSampleStats(i).active && audio::GetSampleStats(i).volume == 0 && sound->channels[i]->fade == 2,
            "zero-duration-zero-dt");
    tick(sound, 0.01f);
    sound->Update(0);
    check(Count() == 0, "zero-duration-out");
    for (int n = 0; n < 16; ++n) check(play(sound, "waves_loop", 2, 0) == n, "first-free-channel");
    check(Count() == 16 && play(sound, "waves_loop", 2, 0) == -1, "sixteen-channel-capacity");
    stop(sound);
    check(Count() == 0, "stop-all-release");
    Queries(sound);
}

// Sound::Construct/Destruct on a disabled and an enabled Sound: the fields, the empty channels, the bank and the context sound registration.
void LifecycleContract()
{
    auto check = [](bool ok, const char* label) { OBL_LOG("autotest: %s lifecycle-contract %s", ok ? "PASS" : "FAILED", label); };
    auto*& global = game::GetContext().sound;
    Sound* live = global;
    // Sound owns its channel records, so Construct runs on live objects; the stale values show that it rewrites them.
    auto disabled = std::make_unique<Sound>(), enabled = std::make_unique<Sound>();
    auto* b = disabled.get();
    auto* c = enabled.get();
    for (Sound* s : {b, c}) {
        s->enabled = s->spatial = 0xCC;
        s->driver = s->listener = s;
        s->provider = 0xCCCCCCCC;
        s->minDistance = s->maxDistance = -1.0f;
        s->channels[3] = std::make_unique<audio::SoundChannel>();
    }
    b->Construct(0);
    check(audio::CurrentSound() == b, "sets-current-sound");
    bool empty = true;
    for (int i = 0; i < 24; ++i) empty &= b->channels[i] == nullptr;
    check(empty, "disabled-channels-empty");
    check(b->bank && b->bank->count > 0, "bank-built");
    check(b->enabled == 0 && b->spatial == 0 && b->minDistance == 250.0f && b->maxDistance == 750.0f, "disabled-values");
    global = b;
    b->ReleaseRecords();
    check(audio::CurrentSound() == nullptr && !b->bank, "release-sound");
    c->Construct(1);
    check(c->enabled == 1 && c->spatial == 1 && !c->driver && !c->provider && !c->listener, "enabled-uses-sdl");
    for (const auto& channel : c->channels)
        if (channel) check(false, "channels-empty");
    c->ReleaseRecords();
    global = live;
    check(audio::CurrentSound() == live, "fixture-restores-current-sound");
}
} // namespace
bool ReadSamplesProbe(const std::string& name, double& out)
{
    if (ReadSpatialSamplesProbe(name, out)) return true;
    if (name == "samples-occupied")
        out = Count();
    else if (name == "sample-active")
        out = audio::GetSampleStats(g_channel).active;
    else if (name == "sample-seconds")
        out = audio::GetSampleStats(g_channel).seconds;
    else if (name == "sample-energy")
        out = audio::GetSampleStats(g_channel).energy;
    else
        return false;
    return true;
}
bool ExecuteSamplesCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (ExecuteSpatialSamplesCommand(name, args)) return true;
    if (!name.starts_with("sample") && name != "sound-lifecycle-contract") return false;
    auto* sound = GetSound();
    if (!sound) {
        OBL_LOG("autotest: FAILED samples: Sound missing");
        return true;
    }
    if (name == "samples-contract")
        Contract(sound);
    else if (name == "samples-report")
        Report();
    else if (name == "samples-mss-report") {
        const bool loaded = platform::ModuleLoaded(u"Mss32.dll");
        OBL_LOG("autotest: samples-mss loaded=%d", loaded);
    } else if (name == "sound-lifecycle-contract")
        LifecycleContract();
    else if (name == "samples-stop")
        sound->StopAllSamples();
    else if (name == "samples-volume" && args.size() == 1)
        sound->SetEffectsVolume(static_cast<float>(std::atof(args[0].c_str())));
    else if (name == "samples-disabled" && args.size() == 1)
        sound->enabled = std::atoi(args[0].c_str()) ? 0 : 1;
    else if (name == "sample-select" && args.size() == 1)
        g_channel = std::atoi(args[0].c_str());
    else if (name == "sample-play" && args.size() >= 1) {
        uint8_t flags = args.size() > 1 ? static_cast<uint8_t>(std::atoi(args[1].c_str())) : 0;
        float fade = args.size() > 2 ? static_cast<float>(std::atof(args[2].c_str())) : 0;
        g_channel = sound->Play(args[0].c_str(), flags, fade);
        OBL_LOG("autotest: sample-play channel=%d", g_channel);
    } else if (name == "sample-volume" && args.size() == 1)
        sound->SetChannelVolume(g_channel, static_cast<float>(std::atof(args[0].c_str())));
    else if (name == "sample-fade" && args.size() == 2)
        sound->FadeChannelTo(g_channel, static_cast<float>(std::atof(args[0].c_str())), static_cast<float>(std::atof(args[1].c_str())));
    else if (name == "sample-out" && args.size() == 1)
        sound->FadeChannelOut(g_channel, static_cast<float>(std::atof(args[0].c_str())));
    else if (name == "sample-start")
        sound->StartChannel(g_channel);
    else if (name == "samples-tick" && args.size() == 1)
        sound->Update(static_cast<float>(std::atof(args[0].c_str())));
    else
        return false;
    return true;
}
} // namespace obl::test
