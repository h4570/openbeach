#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/guest_update_desires.h"
#include "game/objects/guest_update_opinions.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>

namespace obl::object {
using game::GetWeather;
using game::GetWorld;
using game::GuestAttractionCounts;
using game::GuestPopulation;
using game::Rand;

namespace {

using object::Guest;

// Call counters (`call-report <name>`).
test::CallCounter g_desiresCalls("objects.guest-update-desires");
test::CallCounter g_weatherCalls("objects.guest-apply-weather-modifiers");
test::CallCounter g_opinionsCalls("objects.guest-update-opinions");

// ---- ApplyWeatherModifiers --------------------------------------------------------------------------------------------------

void ApplyWeatherModifiers(Guest* guest)
{
    g_weatherCalls.Count();
    ApplyGuestWeatherModifiers(*guest, GetWeather()->m_desireModifiers);
}

// ---- UpdateOpinions --------------------------------------------------------------------------------------------------

constexpr float kOpinionPeriod = 2.0f; // the guest asks itself for the next update (MT_UPDATEOPINIONS) every two seconds

GuestOpinionWorld OpinionWorld()
{
    const world_data::World* world = GetWorld();
    return {world->m_scenicVal, static_cast<uint32_t>(world->m_numLitter),
            reinterpret_cast<const uint32_t*>(GuestAttractionCounts().data()), GuestPopulation()};
}

void RunUpdateOpinions(Guest& guest)
{
    UpdateGuestOpinions(guest, *guest.m_definition, OpinionWorld(), [] { return Rand(); });
    // Sent for every state; the fields past data are unused.
    messages::Message message{};
    message.sender = guest.m_id;
    message.receiver = guest.m_id;
    message.type = messages::MT_UPDATEOPINIONS;
    messages::message_facade::PostDelayed(messages::message_facade::Current(), &message, kOpinionPeriod);
}

} // namespace

void GuestApplyWeatherModifiers(Guest* guest)
{
    g_guestSeams.applyWeatherModifiers(guest);
}

void Guest::UpdateDesires()
{
    g_desiresCalls.Count();
    UpdateGuestDesires(*this, *m_definition, GetWorld()->m_gameDeltaTime);
}

void Guest::UpdateOpinions()
{
    g_opinionsCalls.Count();
    RunUpdateOpinions(*this);
}

GuestSeams g_guestSeams{
        [](Guest* guest) { guest->UpdateDesires(); },
        [](Guest* guest) { guest->UpdateOpinions(); },
        [](Guest* guest) { guest->AlterQueueOpinion(); },
        [](Guest* guest) { guest->AlterSafetyOpinion(); },
        [](Guest* guest) { guest->UpdatePriceScore(); },
        [](Guest* guest) { guest->AlterPriceOpinion(); },
        [](Guest* guest) { guest->UpdatePurchaseOpinion(); },
        [](Guest* guest) { guest->ReactToSpottedEvent(); },
        [](Guest* guest) { return guest->CheckBedTime(); },
        [](Guest* guest) { return guest->CheckLeaveResort(); },
        [](Guest* guest) { guest->UpdateDrowning(); },
        [](Guest* guest) { guest->DecideSwimAction(); },
        [](Guest* guest) { return guest->StartStomachIllness(); },
        [](Guest* guest) { guest->LookForVandalItem(); },
        [](Guest* guest) { guest->VandaliseBin(); },
        [](Guest* guest) { guest->FindFightPoint(); },
        [](Guest* guest) { guest->FindFightPerson(); },
        [](Guest* guest) { guest->TellNearbyOfFight(); },
        [](Guest* guest, int desire, float value) { guest->SetRepeatAction(desire, value); },
        [](Guest* guest) { return guest->FindSunbathePoint(); },
        [](float x, float y, float) { return GuestFindSwimPoint(x, y); },
        &ApplyWeatherModifiers,
};

// The call-drive drivers of the guest modules and the object manager's clear-selection probe.
bool RegisterGuestTestDrivers()
{
    RegisterGuestLifecycleDriver();
    RegisterGuestDrawDrivers();
    RegisterGuestSoundDriver();
    SetGuestClearSelectionProbe();
    return true;
}

} // namespace obl::object
