#include "game/ui/sound_api.h"
#include "platform/sound_lifecycle.h"
#include "game/game/game_accessors.h"
#include "core/log.h"
#include "core/os.h"
#include "game/ui/sample_mixer.h"
#include "game/ui/music_stream.h"

#include <SDL3/SDL.h>

namespace obl::audio {
namespace {

Sound*& SoundSlot()
{
    return game::GetContext().sound;
}

bool ProbeAudioDevice()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        OBL_LOG("sound: SDL audio unavailable: %s", SDL_GetError());
        return false;
    }
    int count = 0;
    SDL_free(SDL_GetAudioPlaybackDevices(&count));
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if (count <= 0) OBL_LOG("sound: no SDL playback device");
    return count > 0;
}
} // namespace

Sound* Sound::Construct(uint8_t enable)
{
    SoundSlot() = this;
    // No driver, provider, sample handle or listener is owned here; they stay empty.
    driver = nullptr;
    provider = 0;
    listener = nullptr;
    for (auto& handle : samples) handle = nullptr;
    for (auto& channel : channels) channel = nullptr;
    for (float& v : position) v = 0;
    stream = nullptr;
    musicVolume = 0;
    effectsScale = 0;
    effectsVolume = 0;
    minDistance = 250.0f;
    maxDistance = 750.0f;
    enabled = enable && ProbeAudioDevice();
    // No provider to open: SDL spatializes the eight 3D channels itself whenever audio works.
    spatial = enabled;
    // The bank opens the converted samples of the pack itself (sample_mixer.cpp).
    bank = (new SoundBankRecord)->Construct();
    return this;
}

void Sound::ReleaseRecords()
{
    for (auto& channel : channels) channel.reset();
    if (bank) {
        bank->Destruct();
        delete bank;
        bank = nullptr;
    }
    SoundSlot() = nullptr;
}

void Sound::Destruct()
{
    CloseSamples(this);
    ShutdownMusic();
    ReleaseRecords();
    if (os::HasEnv("OBL_SCRIPT"))
        OBL_LOG("autotest: %s music-cleanup open=%d audio-init=%d",
                !GetMusicStats().open && !SDL_WasInit(SDL_INIT_AUDIO) ? "PASS" : "FAILED", GetMusicStats().open,
                SDL_WasInit(SDL_INIT_AUDIO) != 0);
}
} // namespace obl::audio

namespace obl::audio {
Sound* CurrentSound()
{
    return game::GetContext().sound;
}
} // namespace obl::audio
