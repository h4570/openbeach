#pragma once
#include "game/ui/audio_types.h"
#include <string>
#include <string_view>
#include <vector>

namespace obl::scripted_sound {
// The events the game fires. A sound script binds samples to any event names it likes (`events`, data-formats/assets.md#sounds);
// a name nothing fires stays unused, and a fired name the script does not bind is silent.
namespace event {
inline constexpr const char* kMaleStench = "guest_male_stench";
inline constexpr const char* kMaleVomit = "guest_male_vomit";
inline constexpr const char* kMaleVandalism = "guest_male_vandalism";
inline constexpr const char* kMaleFight = "guest_male_fight";
inline constexpr const char* kMaleTopless = "guest_male_topless";
inline constexpr const char* kMaleWetShirt = "guest_male_wet_shirt";
inline constexpr const char* kMaleQueueClosed = "guest_male_queue_closed";
inline constexpr const char* kMalePurchaseGood = "guest_male_purchase_good";
inline constexpr const char* kMalePurchaseBad = "guest_male_purchase_bad";
inline constexpr const char* kFemaleStench = "guest_female_stench";
inline constexpr const char* kFemaleVomit = "guest_female_vomit";
inline constexpr const char* kFemaleVandalism = "guest_female_vandalism";
inline constexpr const char* kFemaleFight = "guest_female_fight";
inline constexpr const char* kFemaleQueueClosed = "guest_female_queue_closed";
inline constexpr const char* kFemalePurchaseGood = "guest_female_purchase_good";
inline constexpr const char* kFemalePurchaseBad = "guest_female_purchase_bad";
} // namespace event

// The host owns allocations. Production uses the game heap; unit tests own scratch arenas.
struct Host {
    virtual ~Host() = default;
    virtual void* Allocate(size_t bytes) = 0;
    virtual bool AcquireSprite(const char* name, std::vector<std::string>& animations) = 0;
    virtual void ReleaseSprite() = 0;
    virtual bool HasSound(const char* name) = 0;
};
struct RuntimeHost {
    virtual ~RuntimeHost() = default;
    virtual int AnimationCount(const char* name) = 0;
    virtual void ReleaseSprite() = 0;
    virtual void Free(void* memory) = 0;
    virtual void Play(const char* name, float* position, float volume, int limit, int group) = 0;
    virtual const char* ObjectName(int objectId) = 0;
    virtual void Start(const char* name, int objectId, float volume, bool loop) = 0;
    virtual void Stop(int objectId) = 0;
};
audio::ScriptedSound* Construct(audio::ScriptedSound& out);
void Destruct(audio::ScriptedSound& out, RuntimeHost& host);
bool PlaySpriteSound(audio::ScriptedSound& out, const char* name, int animation, const float* position, RuntimeHost& host);
void StartBuildingSound(const audio::ScriptedSound& out, int objectId, RuntimeHost& host);
void StopBuildingSound(int objectId, RuntimeHost& host);
// The event of that exact name, or null.
const audio::SoundEvent* FindEvent(const audio::ScriptedSound& sound, const char* name);
// Builds the records of `sounds/sound_script.json` (scripted_sound_pack.cpp) in the document's order, allocating through the host.
bool LoadDocument(audio::ScriptedSound& out, std::string_view json, Host& host, std::string& error);
} // namespace obl::scripted_sound
