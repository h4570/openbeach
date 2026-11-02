#include "game/ui/scripted_sound_runtime.h"
#include "game/objects/guest.h"
#include "game/objects/object_registry_api.h"
#include "guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/guest_lifecycle.h"
#include "game/objects/guest_visit_opinions.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/ui/text_table.h"
#include "game/ui/scripted_sound.h"
#include "game/objects/commodity_providers.h"
#include "game/world/world_params.h"
#include "game/objects/building_methods.h"

#include <string>

namespace obl::object {
using game::GetObjectManager;
using game::GetScriptedSound;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

namespace {

using object::Building;
using object::Guest;
using enum tables::GuestOpinion;
using guest_opinions::Below;
using guest_visit::Subtract;

// Call counters (`call-report <name>`).
test::CallCounter g_queue("objects.guest-alter-queue-opinion");
test::CallCounter g_safety("objects.guest-alter-safety-opinion");
test::CallCounter g_priceScore("objects.guest-update-price-score");
test::CallCounter g_priceOpinion("objects.guest-alter-price-opinion");
test::CallCounter g_purchase("objects.guest-update-purchase-opinion");
test::CallCounter g_spot("objects.guest-do-spot-opinion");
test::CallCounter g_bedTime("objects.guest-check-bed-time");
test::CallCounter g_leave("objects.guest-check-leave-resort");

object::Base* Object(int id)
{
    return object_manager::ObjectById(GetObjectManager(), id);
}

Building* Destination(Guest& guest)
{
    if (guest.m_DestObjectID == -1) return nullptr;
    auto* object = Object(guest.m_DestObjectID);
    return object->m_type == object::ObjectType::Building ? static_cast<Building*>(object) : nullptr;
}

const char* CommodityName(const tables::BuildingDef& def, int slot)
{
    return world::world_params::GetCommodityName(GetWorldParams(), def.m_Commodity[slot]);
}

// TextTable keys already end in the separator (commodity names carry their trailing separator too).
int Thought(Guest& guest, const std::string& prefix)
{
    std::string key = prefix + std::to_string(Rand() % 3 + 1);
    const int id = static_cast<int>(ui::text_table::FindKey(key.c_str()));
    if (id != -1) {
        guest.m_Thoughts[2] = guest.m_Thoughts[1];
        guest.m_Thoughts[1] = guest.m_Thoughts[0];
        guest.m_Thoughts[0] = id;
    }
    return id;
}

void BuildingThought(Guest& guest, Building* building, int id)
{
    if (!building) return;
    auto* text = ui::text_table::GetString(id);
    BuildingAddThought(building, text, guest.m_id);
}

void Mood(Guest& guest, int icon)
{
    guest.m_moodIcon->m_frameIndex = icon;
    guest.m_moodIconTime = 2.0f;
}

void BadVisitMood(Guest& guest)
{
    guest.m_lastBad = 1;
    const bool wasGood = guest.m_lastGood != 0;
    if (wasGood)
        guest.m_lastGood = 0;
    else
        guest.m_lastBad = 0;
    // Not greater, including unordered.
    if (!(guest.m_moodIconTime > 0.0f)) Mood(guest, wasGood ? 0 : 1);
}

void EventSound(Guest& guest, const char* event)
{
    float xyz[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    GetScriptedSound()->PlayEvent(event, xyz);
}

void BadVisitSound(Guest& guest)
{
    EventSound(
            guest, guest.m_definition->m_Gender == 0 ? scripted_sound::event::kMalePurchaseBad : scripted_sound::event::kFemalePurchaseBad);
}

void StateChange(Guest& guest, int state)
{
    messages::Message message{};
    message.sender = message.receiver = guest.m_id;
    message.type = messages::MT_STATECHANGE;
    message.data = state;
    messages::message_facade::Post(messages::message_facade::Current(), &message);
}

void AlterQueue(Guest& guest)
{
    Building* building = Destination(guest);
    if (!building) return;
    // The tolerance goes through a 64-bit integer, which keeps unsigned values even for a negative tolerance.
    const uint32_t count = building->m_definition->m_MaxQueue == 0
            ? static_cast<uint32_t>(static_cast<int64_t>(guest.m_Tolerance[2]))
            : static_cast<uint32_t>(static_cast<const object::BuildingQueue*>(building->m_queue)->m_qlQueue.size());
    const double length = static_cast<double>(count);
    if (Below(length, guest.m_Tolerance[2])) {
        guest.m_queueScore = static_cast<float>(static_cast<double>(guest.m_definition->m_OpinionEffect[Crowding][0]) + guest.m_queueScore);
        guest.m_ActionState = 1;
    } else if (length > guest.m_Tolerance[3]) {
        const int thought =
                Thought(guest, std::string("GUEST_THOUGHT_LONG_") + CommodityName(*building->m_definition, guest.m_desired_commod));
        guest.m_missedPurchase = 1;
        BadVisitMood(guest);
        BadVisitSound(guest);
        BuildingThought(guest, building, thought);
        guest_visit::QueuePenalty(guest, *guest.m_definition);
        guest.m_ActionState = 0;
        guest.MissedDesire();
        StateChange(guest, 0xc);
    } else {
        guest.m_ActionState = 1;
    }
    // The immediate message can finish the visit before this clamp executes.
    guest_visit::Clamp(guest.m_opinion[Crowding]);
}

void AlterSafety(Guest& guest)
{
    Building* building = Destination(guest);
    if (!building) return;
    const double quality = guest_visit::SafetyQuality(building->m_RepairState);
    if (Below(quality, guest.m_Tolerance[12])) {
        BuildingThought(guest, building,
                Thought(guest, std::string("GUEST_THOUGHT_UNSAFE_") + CommodityName(*building->m_definition, guest.m_desired_commod)));
        BadVisitMood(guest);
        BadVisitSound(guest);
        Subtract(guest.m_curScore, guest.m_definition->m_OpinionEffect[Safety][1]);
        guest.m_missedPurchase = 1;
        guest.m_ActionState = 0;
        Subtract(guest.m_opinion[Safety], guest.m_definition->m_OpinionEffect[Safety][1]);
        guest.MissedDesire();
        if (guest.m_StayingAt == guest.m_DestObjectID) {
            guest.m_leavingEarly = 1;
            Thought(guest, "GUEST_THOUGHT_THIS_IS_CRAP");
            StateChange(guest, 0x29);
        } else {
            StateChange(guest, 0xc);
        }
    } else {
        guest.m_ActionState = 1;
        if (quality > guest.m_Tolerance[13]) guest.m_safetyScore = guest.m_definition->m_OpinionEffect[Safety][0];
    }
    guest_visit::Clamp(guest.m_opinion[Safety]);
}

float ReferencePrice(Building* building, int slot)
{
    return static_cast<float>(commodity_providers::SlotReferencePrice(building, slot));
}

void UpdatePrice(Guest& guest)
{
    auto* building = static_cast<Building*>(Object(guest.m_DestObjectID));
    const float price = static_cast<float>(commodity_providers::SlotPrice(building, guest.m_desired_commod));
    if (!Below(guest_visit::PriceRatio(price, ReferencePrice(building, guest.m_desired_commod)), guest.m_Tolerance[6])) return;
    building = static_cast<Building*>(Object(guest.m_DestObjectID));
    guest.m_priceScore = guest.m_definition->m_OpinionEffect[Price][0];
    guest_opinions::Raise(guest.m_opinion[Price], guest.m_priceScore);
    guest.m_lastGood = 0;
    if (!(guest.m_moodIconTime > 0.0f)) Mood(guest, 0);
    BuildingThought(guest, building,
            Thought(guest, std::string("GUEST_THOUGHT_CHEAP_") + CommodityName(*building->m_definition, guest.m_desired_commod)));
}

void AlterPrice(Guest& guest)
{
    auto* building = static_cast<Building*>(Object(guest.m_DestObjectID));
    const float reference = ReferencePrice(building, guest.m_desired_commod);
    const float paid = static_cast<messages::Message*>(guest.m_currentMessage)->floatData;
    if (!(guest_visit::PriceRatio(paid, reference) > guest.m_Tolerance[7])) return;
    guest.m_missedPurchase = 1;
    Subtract(guest.m_opinion[Price], guest.m_definition->m_OpinionEffect[Price][1]);
    Subtract(guest.m_curScore, guest.m_definition->m_OpinionEffect[Price][1]);
    BadVisitMood(guest);
    BadVisitSound(guest);
    BuildingThought(guest, building,
            Thought(guest, std::string("GUEST_THOUGHT_EXSPENSIVE_") + CommodityName(*building->m_definition, guest.m_desired_commod)));
    if (Below(guest.m_opinion[Price], 0.0)) guest.m_opinion[Price] = 0.0f;
}

void UpdatePurchase(Guest& guest)
{
    if (!guest.m_missedPurchase) {
        Building* building = guest.m_activity ? nullptr : static_cast<Building*>(Object(guest.m_CurID));
        guest.m_curScore = static_cast<float>(
                ((static_cast<double>(guest.m_cleanScore) + guest.m_queueScore) + guest.m_priceScore) + guest.m_safetyScore);
        if (Below(guest.m_cleanScore, 0.0)) {
            if (!guest.m_activity) BuildingThought(guest, building, Thought(guest, "GUEST_THOUGHT_TOILET_STENCH"));
            BadVisitMood(guest);
        } else {
            // The excellent branch emits mood/sound even for activities; the nice branch emits neither.
            const bool excellent = guest.m_queueScore > 0.0f || guest.m_safetyScore > 0.0f;
            if (!guest.m_activity) {
                auto* def = world::world_params::GetBuildingDef(GetWorldParams(), guest.m_CurTypeID);
                const char* prefix = excellent ? "GUEST_THOUGHT_EXCELLENT_" : "GUEST_THOUGHT_NICE_";
                BuildingThought(guest, building, Thought(guest, std::string(prefix) + CommodityName(*def, guest.m_desired_commod)));
            }
            if (excellent) {
                guest.m_lastGood = 0;
                if (!(guest.m_moodIconTime > 0.0f)) Mood(guest, 0);
                EventSound(guest,
                        guest.m_definition->m_Gender == 0 ? scripted_sound::event::kMalePurchaseGood
                                                          : scripted_sound::event::kFemalePurchaseGood);
            }
        }
    }
    guest_visit::FinishPurchase(guest);
}

void DoSpot(Guest& guest)
{
    const uint32_t kind = static_cast<messages::Message*>(guest.m_currentMessage)->data;
    const int state = guest.m_state;
    if ((kind == 5 && guest.m_definition->m_Gender != 0) ||
            ((state == 0x16 || state == 0x36 || state == 0x37 || state == 0x17) && kind < 8))
        return;
    static constexpr const char* kThoughts[] = {"GUEST_THOUGHT_TOILET_STENCH", "GUEST_THOUGHT_VOMIT", "GUEST_THOUGHT_VANDALISM",
            "GUEST_THOUGHT_FIGHT", "GUEST_THOUGHT_TOPLESS", "GUEST_THOUGHT_WET_T_SHIRT", "GUEST_THOUGHT_QUEUE_CLOSED_",
            "GUEST_THOUGHT_QUEUE_BROKEN_", "GUEST_THOUGHT_EATEN", "GUEST_THOUGHT_RUN_FROM_SHARK", "GUEST_THOUGHT_DROWNED"};
    guest_visit::SpotScores(guest, *guest.m_definition, kind);
    std::string prefix;
    if (kind <= 10) {
        Mood(guest, kind == 4 || kind == 5 ? 0 : 1);
        prefix = kThoughts[kind];
        if (kind == 6 || kind == 7) {
            auto* building = static_cast<Building*>(Object(guest.m_ActivityPerson));
            prefix += CommodityName(*building->m_definition, guest.m_desired_commod);
            StateChange(guest, 0xc);
        }
    }
    // An unknown kind still draws and clamps.
    Thought(guest, prefix);
    for (float& opinion : guest.m_opinion) guest_visit::Clamp(opinion);
    // The sound lookup rereads the message after the immediate state-change cascade.
    const uint32_t soundKind = static_cast<messages::Message*>(guest.m_currentMessage)->data;
    using namespace scripted_sound::event;
    static constexpr const char* kMaleSpotted[] = {
            kMaleStench, kMaleVomit, kMaleVandalism, kMaleFight, kMaleTopless, kMaleWetShirt, kMaleQueueClosed};
    static constexpr const char* kFemaleSpotted[] = {kFemaleStench, kFemaleVomit, kFemaleVandalism, kFemaleFight};
    if (guest.m_definition->m_Gender == 0) {
        if (soundKind <= 6) EventSound(guest, kMaleSpotted[soundKind]);
    } else if (soundKind <= 3 || soundKind == 6) {
        EventSound(guest, soundKind == 6 ? kFemaleQueueClosed : kFemaleSpotted[soundKind]);
    }
}

// ---- CheckBedTime / CheckLeaveResort (src/game/objects/guest_lifecycle.h) ------------------------------------------------------------------
// Both run from SearchDesire; the immediate Post of a state change delivers the guest's own script before the rest of the function,
// so the order of the writes, draws and messages below is part of the behavior.

void PostMessage(Guest& guest, int type, int flags, int data)
{
    messages::Message message{};
    message.sender = message.receiver = guest.m_id;
    message.type = type;
    message.flags = flags;
    message.data = data;
    messages::message_facade::Post(messages::message_facade::Current(), &message);
}

bool RunCheckBedTime(Guest& guest)
{
    const layout::World* world = GetWorld();
    if (!guest_lifecycle::BedTimeProceeds(guest.m_WakeTime, guest.m_BedTime, world->m_gameDayTime, world->m_gameDay)) return false;
    if (guest.m_NightsToStay == 0) return false;
    if (--guest.m_NightsToStay == 0) guest.m_TimeToLeave = guest_lifecycle::TimeToLeave(Rand(), guest.m_WakeTime);
    if (guest.m_StayingAt == -1) {
        StateChange(guest, 0x29);
        guest.m_leavingEarly = 1;
        Thought(guest, "GUEST_THOUGHT_NO_SPACE");
        return true;
    }
    // Remembers the hotel as the last place of the two-entry ring, then thinks about it before going.
    guest.m_lastActivity[guest.m_ArrayPos] = 0;
    guest.m_LastID[guest.m_ArrayPos] = guest.m_StayingAt;
    guest.m_ArrayPos = (guest.m_ArrayPos - 1) & 1;
    auto* hotel = static_cast<Building*>(Object(guest.m_StayingAt));
    Thought(guest, std::string("GUEST_THOUGHT_HEAD_") + CommodityName(*hotel->m_definition, 0));
    StateChange(guest, 0xd);
    guest.m_CurID = guest.m_StayingAt;
    hotel = static_cast<Building*>(Object(guest.m_CurID));
    guest.m_CurTypeID = hotel->m_definition->m_id;
    guest.m_desired_commod = 0;
    guest.m_activity = 0;
    PostMessage(guest, messages::MT_PAVEMENT2OBJECT, 1, guest.m_StayingAt);
    return true;
}

bool RunCheckLeaveResort(Guest& guest)
{
    const layout::World* world = GetWorld();
    switch (guest_lifecycle::LeaveDecision(
            guest.m_happiness, guest.m_NightsToStay, guest.m_TimeToLeave, world->m_gameDayTime, guest.m_souvenirsDecided != 0)) {
    case guest_lifecycle::LeaveAction::Unhappy:
        StateChange(guest, 0x29);
        Thought(guest, "GUEST_THOUGHT_THIS_IS_CRAP");
        guest.m_leavingEarly = 1;
        return true;
    case guest_lifecycle::LeaveAction::GoHome:
        Thought(guest, "GUEST_THOUGHT_GO_HOME");
        StateChange(guest, 0x29);
        return true;
    case guest_lifecycle::LeaveAction::Souvenirs:
        guest.m_souvenirsDecided = 1;
        guest.m_desires[10] = 1.0f;
        return false;
    case guest_lifecycle::LeaveAction::Stay: break;
    }
    return false;
}

template<auto Perform> void Dispatch(Guest* guest, test::CallCounter& counter)
{
    counter.Count();
    Perform(*guest);
}

template<auto Perform> unsigned Answer(Guest* guest, test::CallCounter& counter)
{
    counter.Count();
    return Perform(*guest) ? 1u : 0u; // callers read the low byte
}

} // namespace

void Guest::AlterQueueOpinion()
{
    Dispatch<AlterQueue>(this, g_queue);
}

void Guest::AlterSafetyOpinion()
{
    Dispatch<AlterSafety>(this, g_safety);
}

void Guest::UpdatePriceScore()
{
    Dispatch<UpdatePrice>(this, g_priceScore);
}

void Guest::AlterPriceOpinion()
{
    Dispatch<AlterPrice>(this, g_priceOpinion);
}

void Guest::UpdatePurchaseOpinion()
{
    Dispatch<UpdatePurchase>(this, g_purchase);
}

void Guest::ReactToSpottedEvent()
{
    Dispatch<DoSpot>(this, g_spot);
}

bool Guest::CheckBedTime()
{
    return Answer<RunCheckBedTime>(this, g_bedTime);
}

bool Guest::CheckLeaveResort()
{
    return Answer<RunCheckLeaveResort>(this, g_leave);
}

int GuestThought(Guest& guest, const std::string& prefix)
{
    return Thought(guest, prefix);
}
object::Base* FindObjectById(int id)
{
    return Object(id);
}
const char* GuestCommodityName(const tables::BuildingDef& def, int slot)
{
    return CommodityName(def, slot);
}
void GuestBuildingThought(Guest& guest, Building* building, int textId)
{
    BuildingThought(guest, building, textId);
}
void GuestBadVisitMood(Guest& guest)
{
    BadVisitMood(guest);
}
void GuestBadVisitSound(Guest& guest)
{
    BadVisitSound(guest);
}
void GuestStateChange(Guest& guest, int state)
{
    StateChange(guest, state);
}

} // namespace obl::object
