#include "core/char16.h"
#include "game/ui/sound_api.h"
#include "core/ascii_case.h"
#include "core/pack_path.h"
#include "game/ui/sample_mixer.h"
#include "game/game/game_accessors.h"
#include "core/log.h"
#include "game/ui/music_volume.h"
#include "core/os.h"
#include "core/wide_path.h"
#include "game/game/diagnostics.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace obl::audio {
namespace {

using Bank = SoundBankRecord;
using Channel = SoundChannel;

std::unordered_map<Bank*, std::unique_ptr<SoundBank>> g_banks;
// <game dir>/packs/base/sounds, where the converted samples are (converter/sounds.md), and the layer above it: the mod
// layer's sounds/ shadows the converted file of the same name (pack_path.h, docs/build/game-data.md "Layers").
std::filesystem::path g_samplesDirectory;
std::vector<std::filesystem::path> g_samplesLayers;
std::array<SampleStream, 24> g_samples;
struct Source {
    SpatialPosition position;
    float nearDistance = 0, farDistance = 0;
};
std::array<Source, 8> g_sources;
bool g_muted = false;

void SyncBankCounts(Bank* bank, const SoundBank& store)
{
    bank->count = static_cast<uint32_t>(store.Entries().size());
    bank->cached = static_cast<uint32_t>(store.CachedBytes());
}

// Opens the bank's entries from the converted samples; the bank record keeps only the counts.
void OpenBank(Bank* bank)
{
    auto& store = g_banks[bank];
    if (!store) store = std::make_unique<SoundBank>();
    // Without samples the game has no sound effects; the converter writes them with the sound script, so stop as that does.
    if (!store->Open(g_samplesLayers))
        game::FatalError("samples: cannot read the converted .wav files in %s", g_samplesDirectory.string().c_str());
    SyncBankCounts(bank, *store);
}

SpatialPosition ToSpatialPosition(const float* xyz)
{
    return {xyz[0], xyz[1], xyz[2]};
}

// The mix of the spatial channel i (16..23) from its source position and the listener.
SpatialMix SpatialMixFor(const Sound& sound, int i)
{
    const auto& s = g_sources[i - 16];
    return SpatialGains(s.position, ToSpatialPosition(sound.position), s.nearDistance, s.farDistance);
}

// Stops the sample of channel i and frees its record; the bank entry is released with it.
void DropChannel(Sound& sound, int i)
{
    g_samples[i].Close();
    if (auto& c = sound.channels[i]) {
        if (sound.bank) sound.bank->ReleaseSample(c->name.c_str());
        c.reset();
    }
}

std::string WavName(const char* name)
{
    return std::string(name ? name : "") + ".wav";
}

// A channel record with a copy of the file name, owned by DropChannel / Sound::ReleaseRecords.
std::unique_ptr<Channel> NewChannel(const std::string& file, const uint8_t* data, uint32_t size)
{
    auto c = std::make_unique<Channel>();
    c->name = file;
    c->data = data;
    c->size = size;
    return c;
}

} // namespace

SoundBankRecord* SoundBankRecord::Construct()
{
    *this = {};
    OpenBank(this);
    return this;
}

void SoundBankRecord::LoadHeader()
{
    OpenBank(this);
}

void SoundBankRecord::Destruct()
{
    g_banks.erase(this);
    *this = {};
}

const uint8_t* SoundBankRecord::GetSample(const char* name, uint32_t* size)
{
    auto it = g_banks.find(this);
    if (it == g_banks.end()) {
        *size = 0;
        return nullptr;
    }
    const auto* result = it->second->Get(name, *size);
    SyncBankCounts(this, *it->second);
    return result;
}

char SoundBankRecord::HasSample(const char* name)
{
    auto it = g_banks.find(this);
    return it != g_banks.end() && it->second->Contains(name);
}

void SoundBankRecord::ReleaseSample(const char* name)
{
    auto it = g_banks.find(this);
    if (it != g_banks.end()) it->second->Release(name);
}

void SoundBankRecord::Update(float dt)
{
    auto it = g_banks.find(this);
    if (it == g_banks.end()) return;
    it->second->Update(dt);
    SyncBankCounts(this, *it->second);
}

int Sound::PlaySample(const char* name, int volume, uint8_t flags, float seconds, float scale)
{
    if (!enabled) return -1;
    int i = 0;
    while (i < 16 && channels[i]) ++i;
    if (i == 16) return -1;
    const std::string file = WavName(name);
    uint32_t size = 0;
    const auto* data = bank ? bank->GetSample(file.c_str(), &size) : nullptr;
    if (!data) return i;
    audio::Wave wave;
    if (!audio::DecodeWave({data, size}, wave) || !g_samples[i].Open(std::move(wave), flags & 2, seconds > 0 ? 0 : volume, g_muted)) {
        if (bank) bank->ReleaseSample(file.c_str());
        OBL_LOG("samples: cannot play %s: %s", file.c_str(), SampleStreamError());
        return i;
    }
    auto c = NewChannel(file, data, size);
    c->scale = scale;
    c->target = static_cast<float>(volume);
    c->owner = -1;
    if (seconds > 0) {
        c->fade = 1;
        c->total = c->remaining = seconds;
    }
    channels[i] = std::move(c);
    return i;
}

int Sound::Play3DSample(const char* name, int volume, const float* xyz, uint8_t flags, int owner)
{
    if (!enabled) return -1;
    int i = 16;
    while (i < 24 && channels[i]) ++i;
    if (i == 24) return -1;
    if (!xyz) return i;
    const std::string file = WavName(name);
    uint32_t size = 0;
    const auto* data = bank ? bank->GetSample(file.c_str(), &size) : nullptr;
    if (!data) return i;
    g_sources[i - 16] = {ToSpatialPosition(xyz), minDistance, maxDistance};
    const auto mix = SpatialMixFor(*this, i);
    audio::Wave wave;
    if (!audio::DecodeWave({data, size}, wave) || !g_samples[i].Open(std::move(wave), flags & 2, volume, g_muted, &mix)) {
        if (bank) bank->ReleaseSample(file.c_str());
        OBL_LOG("samples: cannot play 3D %s: %s", file.c_str(), SampleStreamError());
        return i;
    }
    auto c = NewChannel(file, data, size);
    c->owner = owner;
    channels[i] = std::move(c);
    return i;
}

int Sound::Play3D(const char* name, const float* xyz, uint8_t flags, int owner)
{
    return Play3DSample(name, effectsVolume, xyz, flags, owner);
}

int Sound::Play3DScaled(const char* name, float scale, const float* xyz, uint8_t flags, int owner)
{
    return Play3DSample(name, audio::MilesVolume(scale * effectsScale), xyz, flags, owner);
}

void Sound::SetListenerPosition(const float* xyz)
{
    std::memcpy(position, xyz, sizeof(position));
    for (int i = 16; i < 24; ++i)
        if (channels[i]) g_samples[i].Spatial(SpatialMixFor(*this, i));
}

void Sound::UpdateFades(float dt)
{
    for (int i = 0; i < 24; ++i) {
        auto* c = channels[i].get();
        if (!c || !c->fade) continue;
        c->remaining -= dt;
        if (c->remaining < 0) {
            g_samples[i].Volume(static_cast<int>(c->target));
            if (c->fade == 2) g_samples[i].Close();
            c->fade = 0;
        } else if (c->total != 0) {
            c->current += (c->target - c->start) / c->total * dt;
            g_samples[i].Volume(static_cast<int>(c->current));
        } else {
            // Miles clamps the game 0/0 -> indefinite integer volume to silence at dt=0.
            g_samples[i].Volume(0);
        }
    }
}

void Sound::Update(float dt)
{
    if (!enabled) return;
    UpdateFades(dt);
    for (int i = 0; i < 24; ++i)
        if (channels[i] && !g_samples[i].Stats().active) DropChannel(*this, i);
    if (bank) bank->Update(dt);
}

void Sound::SetEffectsVolume(float value)
{
    effectsScale = audio::ClampVolume(value);
    effectsVolume = audio::MilesVolume(value);
    for (int i = 0; i < 16; ++i)
        if (auto* c = channels[i].get(); c && !c->fade) g_samples[i].Volume(audio::MilesVolume(c->scale * effectsScale));
}

void Sound::SetChannelVolume(int i, float value)
{
    if (i < 0 || i >= 24) return;
    if (channels[i]) {
        g_samples[i].Volume(audio::MilesVolume(value * effectsScale));
        channels[i]->scale = value;
    }
}

void Sound::FadeChannelTo(int i, float value, float seconds)
{
    if (i < 0 || i >= 24) return;
    if (channels[i]) {
        auto& c = *channels[i];
        c.scale = value;
        c.target = static_cast<float>(audio::MilesVolume(value * effectsScale));
        c.start = c.current = static_cast<float>(g_samples[i].Stats().volume);
        c.total = c.remaining = seconds;
        c.fade = 1;
    }
}

void Sound::FadeChannelOut(int i, float seconds)
{
    if (i < 0 || i >= 24) return;
    if (channels[i]) {
        auto& c = *channels[i];
        c.target = 0;
        c.start = c.current = static_cast<float>(g_samples[i].Stats().volume);
        c.total = c.remaining = seconds;
        c.fade = 2;
    }
}

void Sound::StartChannel(int i)
{
    if (i < 0 || i >= 24) return;
    if (channels[i]) g_samples[i].Start();
}

void Sound::StopAllSamples()
{
    if (!enabled) return;
    for (int i = 0; i < 24; ++i) DropChannel(*this, i);
    Update(0);
}

// Named-sample operations share the playback and channel records.
int Sound::FindChannel(const char* name)
{
    if (!enabled) return -1;
    const std::string file = WavName(name);
    for (int i = 0; i < 24; ++i)
        if (channels[i] && ascii::EqualsNoCase(channels[i]->name.c_str(), file)) return i;
    return -1;
}

char Sound::HasSample(const char* name)
{
    return bank ? bank->HasSample(WavName(name).c_str()) : 0;
}

int Sound::Play(const char* name, uint8_t flags, float seconds)
{
    return PlaySample(name, effectsVolume, flags, seconds, effectsScale);
}

int Sound::PlayScaled(const char* name, float volume, uint8_t flags, float seconds)
{
    return PlaySample(name, audio::MilesVolume(volume * effectsScale), flags, seconds, volume);
}

int Sound::CountOwnerSamples(int owner)
{
    if (!enabled) return 0;
    int count = 0;
    for (const auto& c : channels) count += c && c->owner == owner;
    return count;
}

float Sound::GetEffectsVolume()
{
    return effectsScale;
}

void CloseSamples(audio::Sound* sound)
{
    for (int i = 0; i < 24; ++i) DropChannel(*sound, i);
    if (os::HasEnv("OBL_SCRIPT")) {
        bool closed = true;
        for (int i = 0; i < 24; ++i) closed &= !g_samples[i].Stats().open && !sound->channels[i];
        OBL_LOG("autotest: %s samples-cleanup closed=%d", closed ? "PASS" : "FAILED", closed);
    }
}
void MuteSamples(bool muted)
{
    g_muted = muted;
    for (int i = 0; i < 24; ++i) g_samples[i].Mute(muted);
}
SampleStats GetSampleStats(int i)
{
    if (i < 0 || i >= 24) return {};
    return g_samples[i].Stats();
}
bool FindCurrentBankEntry(const char* wavName, int& references, uint32_t& size)
{
    auto* sound = audio::CurrentSound();
    if (!sound) return false;
    auto it = g_banks.find(sound->bank);
    if (it == g_banks.end()) return false;
    for (const auto& e : it->second->Entries())
        if (ascii::EqualsNoCase(e.name, wavName)) {
            references = e.references;
            size = e.size;
            return true;
        }
    return false;
}

void ConfigureSamples(const Text16& gameDirectory)
{
    const auto pack = WidePath(gameDirectory) / pack_path::Selected();
    g_samplesDirectory = pack / "sounds";
    g_samplesLayers = {pack_path::ModLayer(pack) / "sounds", g_samplesDirectory};
    OBL_LOG("samples: backend SDL3 WAV / pack bank");
    OBL_LOG("samples: 3D backend SDL3 stereo spatializer");
}
} // namespace obl::audio
