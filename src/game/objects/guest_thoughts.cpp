#include "game/ui/loop_sound_control.h"
#include "game/objects/guest.h"
#include "game/feedback/feedback_manager.h"
#include "game/ui/text_table.h"
#include "game/ui/scripted_sound.h"
#include "game/world/world_params.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/objects/guest_thoughts.h"
#include "game/ui/loop_sound_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/ui/scripted_sound_runtime.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The seven thought functions of a guest (src/game/objects/guest_thoughts.h, guest thoughts, docs/architecture/objects.md): AddIllPurchaseThought,
// AddCommodBeachClosed, AddThoughtIDidnt, AddThoughtIDidIt, NoRouteToLodging, AddBuildingFullThought and
// AddDrowningThought, and the two that ask the same of other objects: DemolishedAsleep (thought, opinion sound) and GenerateBlockedMessage
// (ticker line). The script calls them through the guest's CallFunction. An input a function cannot handle stops the game with
// fatal::UnhandledInput. The driver of the random sound PlayRandomSound checks the LoopSounds calls it makes.
namespace obl::object {
using audio::CurrentLoopSounds;
using feedback::Feedback;
using game::GetWorld;
using game::GetWorldParams;
using game::LastNoPathTickerTime;
using game::Rand;

namespace {

using object::Building;
using object::Guest;
using namespace guest_thoughts;

test::CallCounter g_illPurchase("objects.guest-add-ill-purchase-thought");
test::CallCounter g_beachClosed("objects.guest-add-commod-beach-closed");
test::CallCounter g_didnt("objects.guest-add-thought-i-didnt");
test::CallCounter g_didIt("objects.guest-add-thought-i-did-it");
test::CallCounter g_noPath("objects.guest-no-path-to-accomadation");
test::CallCounter g_buildingFull("objects.guest-add-building-full-thought");
test::CallCounter g_drowning("objects.guest-add-drowning-thought");
test::CallCounter g_demolished("objects.guest-demolished-asleep");
test::CallCounter g_blocked("objects.guest-generate-blocked-message");

test::CallCounter g_randomSound("objects.guest-play-random-sound");
test::CallCounter g_randomSoundContract("objects.guest-play-random-sound.contract");

struct SoundEffect {
    audio::LoopSounds* loops;
    const char* name;
    uint32_t x, y, z, volume;
    int limit, group;
    bool copiedPosition;
};
// Set only by the random sound driver: CaptureOrPlaySpriteSound records the enqueue arguments of its round.
bool g_captureSound = false;
const float* g_soundPosition = nullptr;
std::vector<SoundEffect> g_sounds;

void CaptureOrPlaySpriteSound(audio::LoopSounds* loops, const char* name, float* xyz, float volume, int limit, int group)
{
    if (!g_captureSound) return loops->PlaySpriteSound(name, xyz, volume, limit, group);
    g_sounds.push_back({loops, name, std::bit_cast<uint32_t>(xyz[0]), std::bit_cast<uint32_t>(xyz[1]), std::bit_cast<uint32_t>(xyz[2]),
            std::bit_cast<uint32_t>(volume), limit, group, xyz != g_soundPosition});
}

void PlayRandomSound(audio::ScriptedSound& scripted, const char* event, const float* xyz)
{
    const auto* sound = scripted_sound::FindEvent(scripted, event);
    if (!sound || sound->m_count == 0) return;
    const int index = Rand() % sound->m_count;
    float position[3];
    std::memcpy(position, xyz, sizeof(position));
    const int limit = sound->m_group == -1 ? -1 : scripted.m_groupLimits[sound->m_group];
    CaptureOrPlaySpriteSound(CurrentLoopSounds(), sound->m_sounds[index], position, sound->m_volumes[index], limit, sound->m_group);
}

} // namespace

} // namespace obl::object

namespace obl::audio {
void ScriptedSound::PlayEvent(const char* event, const float* position)
{
    object::PlayRandomSound(*this, event, position);
    object::g_randomSound.Count();
}
} // namespace obl::audio

namespace obl::object {

namespace {
void DriveRandomSound(int count)
{
    audio::ScriptedSound scripted{};
    char names[7][16] = {"sound0", "sound1", "sound2", "sound3", "sound4", "sound5", "sound6"};
    char* sounds[7];
    float volumes[7] = {
            0.0f, -0.0f, 0.25f, -1.5f, 1.0f, std::bit_cast<float>(uint32_t{0x7f800000}), std::bit_cast<float>(uint32_t{0x7fc04321})};
    for (int i = 0; i < 7; ++i) sounds[i] = names[i];
    int limits[20];
    for (int i = 0; i < 20; ++i) limits[i] = i * 3 - 2;
    audio::SoundEvent events[16]{};
    char eventNames[16][16];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(eventNames[i], sizeof(eventNames[i]), "event%d", i);
        events[i].m_name = eventNames[i];
    }
    scripted.m_events = events;
    scripted.m_numEvents = 16;
    scripted.m_groupLimits = limits;
    scripted.m_numGroups = 20;
    for (int i = 0; i < count; ++i) {
        const int category = i % 16;
        const int size = (i / 16) % 8;
        const int group = (i / 128) % 21 - 1;
        events[category] = {eventNames[category], size, group, size ? volumes : nullptr, size ? sounds : nullptr};
        float xyz[3] = {-0.0f, static_cast<float>(i) - 1000.25f, std::bit_cast<float>(uint32_t{0x7fc01234})};
        const auto before = scripted;
        g_sounds.clear();
        g_captureSound = true;
        g_soundPosition = xyz;
        const uint64_t draws = game::RandCalls();
        scripted.PlayEvent(eventNames[category], size ? xyz : nullptr);
        g_captureSound = false;
        bool match = std::memcmp(&before, &scripted, sizeof(scripted)) == 0 && g_sounds.size() == (size != 0) &&
                game::RandCalls() - draws == (size != 0);
        if (size && match) {
            const auto& effect = g_sounds[0];
            int index = 0;
            while (index < size && effect.name != sounds[index]) ++index;
            match = index < size && effect.loops == CurrentLoopSounds() && effect.x == std::bit_cast<uint32_t>(xyz[0]) &&
                    effect.y == std::bit_cast<uint32_t>(xyz[1]) && effect.z == std::bit_cast<uint32_t>(xyz[2]) &&
                    effect.volume == std::bit_cast<uint32_t>(volumes[index]) && effect.group == group &&
                    effect.limit == (group == -1 ? -1 : limits[group]) && effect.copiedPosition;
        }
        g_randomSoundContract.Record(match, "category=" + std::to_string(category) + " count=" + std::to_string(size));
    }
}

// ---- the functions -----------------------------------------------------------------------------------------------------------------------

struct Env {
    int Rand() const { return game::Rand(); }
    int32_t TextId(const char* key) const { return static_cast<int32_t>(ui::text_table::FindKey(key)); }
};

// The building the guest is heading for (m_DestObjectID) or has just been refused by (m_CurID). Null when there is none or it has no
// definition; the caller then ends in fatal::UnhandledInput, before anything was drawn or written.
Building* BuildingOf(int32_t id)
{
    auto* building = static_cast<Building*>(FindObjectById(id));
    return building && building->m_definition ? building : nullptr;
}

bool IllPurchase(Guest& guest)
{
    Building* building = BuildingOf(guest.m_DestObjectID);
    if (!building) return false;
    Env env;
    const int32_t text =
            ThinkOf(guest, std::string(kIllPurchase) + GuestCommodityName(*building->m_definition, guest.m_desired_commod), env);
    GuestBuildingThought(guest, building, text);
    return true;
}

bool BeachClosed(Guest& guest)
{
    Building* building = BuildingOf(guest.m_DestObjectID);
    if (!building) return false;
    Env env;
    ThinkOf(guest, std::string(kBeachClosed) + GuestCommodityName(*building->m_definition, guest.m_desired_commod), env);
    PenaliseBeachClosed(guest, *guest.m_definition);
    return true;
}

bool Didnt(Guest& guest)
{
    Env env;
    ThinkOf(guest, kDetainedForNothing, env);
    PenaliseDetainedForNothing(guest);
    return true;
}

bool DidIt(Guest& guest)
{
    Env env;
    ThinkOf(guest, kDetained, env);
    return true;
}

bool Drowning(Guest& guest)
{
    Env env;
    ThinkOf(guest, DrowningPrefix(*guest.m_definition), env);
    return true;
}

bool NoPath(Guest& guest)
{
    Env env;
    ThinkOf(guest, kNoPathToAccommodation, env);
    float& last = LastNoPathTickerTime();
    world_data::World* world = GetWorld();
    if (NoPathTickerDue(world->m_gameTime, last))
        feedback::Feedback(world->m_feedbackManager)
                .AddText(ui::text_table::Lookup(kNoPathTicker), static_cast<int32_t>(guest.m_id), kNoPathTickerType);
    ShowSadFace(guest);
    // The world pointer is read again after the feedback call.
    last = GetWorld()->m_gameTime;
    return true;
}

bool BuildingFull(Guest& guest)
{
    Building* building = BuildingOf(guest.m_CurID);
    if (!building) return false;
    Env env;
    const int32_t text = ThinkOf(guest, std::string(kBuildingFull) + building->m_definition->m_key, env);
    // The same refusal as a long queue in AlterQueueOpinion, in the same order.
    guest.m_missedPurchase = 1;
    GuestBadVisitMood(guest);
    GuestBadVisitSound(guest);
    GuestBuildingThought(guest, building, text);
    guest_visit::QueuePenalty(guest, *guest.m_definition);
    guest.m_ActionState = 0;
    guest.MissedDesire();
    GuestStateChange(guest, 0xc);
    return true;
}

bool RunDemolishedAsleep(Guest& guest)
{
    PenaliseDemolishedScore(guest);
    GuestBadVisitMood(guest);
    GuestBadVisitSound(guest);
    Env env;
    ThinkOf(guest, kDemolishedAsleep, env);
    PenaliseDemolishedOpinions(guest);
    return true;
}

// The "no path" ticker line of a guest that cannot reach its target: one draw, the building's key only for a guest on foot, no thought and no guard on the time.
bool BlockedMessage(Guest& guest)
{
    Env env;
    const int roll = env.Rand();
    const char* buildingKey = nullptr;
    if (guest.m_activity == 0) buildingKey = world::world_params::GetBuildingDef(GetWorldParams(), guest.m_CurTypeID)->m_key.c_str();
    const std::string key = BlockedKey(guest, buildingKey, roll);
    world_data::World* world = GetWorld();
    feedback::Feedback(world->m_feedbackManager)
            .AddText(ui::text_table::Lookup(key.c_str()), static_cast<int32_t>(guest.m_id), kBlockedTickerType);
    return true;
}

// ---- entries ------------------------------------------------------------------------------------------------------------------------

template<bool (*Perform)(Guest&)> void Dispatch(Guest* guest, test::CallCounter& counter, const char* name)
{
    if (!Perform(*guest)) fatal::UnhandledInput(name, "the input cannot be handled");
    counter.Count();
}

} // namespace

void Guest::AddIllPurchaseThought()
{
    Dispatch<IllPurchase>(this, g_illPurchase, "objects.guest-add-ill-purchase-thought");
}

void Guest::AddCommodBeachClosed()
{
    Dispatch<BeachClosed>(this, g_beachClosed, "objects.guest-add-commod-beach-closed");
}

void Guest::AddThoughtIDidnt()
{
    Dispatch<Didnt>(this, g_didnt, "objects.guest-add-thought-i-didnt");
}

void Guest::AddThoughtIDidIt()
{
    Dispatch<DidIt>(this, g_didIt, "objects.guest-add-thought-i-did-it");
}

void Guest::NoRouteToLodging()
{
    Dispatch<NoPath>(this, g_noPath, "objects.guest-no-path-to-accomadation");
}

void Guest::AddBuildingFullThought()
{
    Dispatch<BuildingFull>(this, g_buildingFull, "objects.guest-add-building-full-thought");
}

void Guest::AddDrowningThought()
{
    Dispatch<Drowning>(this, g_drowning, "objects.guest-add-drowning-thought");
}

void Guest::DemolishedAsleep()
{
    Dispatch<RunDemolishedAsleep>(this, g_demolished, "objects.guest-demolished-asleep");
}

void Guest::GenerateBlockedMessage()
{
    Dispatch<BlockedMessage>(this, g_blocked, "objects.guest-generate-blocked-message");
}

// The random-sound contract driver records its enqueue arguments before invoking the loop-sound facade.
void RegisterGuestSoundDriver()
{
    g_randomSound.SetDriver(DriveRandomSound);
}

} // namespace obl::object
