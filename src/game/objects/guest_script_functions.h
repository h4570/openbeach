#pragma once

#include "core/ascii_case.h"
#include "game/objects/guest_script_values.h"
#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

// CallFunction dispatches the Function <name> commands of guest object scripts by function id.
// Of the 92 guest actions, 58 delegate to guest methods, 33 execute here and ClearMyDelayedMsgs (0xb2) clears delayed messages.
// Invalid ids report a fatal error. Staff and sharks dispatch ids 92..179 separately. Callers ignore the result;
// delegated actions forward their callee result and other actions return zero.
// The environment supplies sprite, sound, time and messaging services; unit tests use a fake.
//   uint32_t Delegate(Guest&, uint32_t id) invokes the guest method selected by id.
//   void InvalidFunction()                                           FatalError("A script called a function guests do not have")
//   object::Base* Object(int32_t id)                           object lookup by id (callers expect a live object)
//   void FaceTowards(SpriteInstance*, const float* target, const float* from)   turning the sprite towards the target
//   void PlaySound3D(const char* name, float* position)              plays sound at the position with flags 1 and no owner
//   const float* FindSunbathePoint(Guest&)                     FindSunbathePoint (a record of two points; the second one is used)
// const float* FindSeaPoint(Guest&, const float* position) FindNearestSeaPoint (member call with the position by value)
//   void FindRandomWalkPoint(float* out)                             a random walkable point of the world
//   int32_t TextId(const char* key)                                  the text table lookup (-1 = no such text)
//   void Post(const Message&) / PostDelayed(const Message&, float)   Post / PostDelayed on the global message handler
//   void ClearDelayed(int32_t receiver)                              drop the receiver's delayed messages
//   int Rand()                                                       the game's rand()
//   int32_t DayLengthSeconds()                                       reads iDAY_LENGTH_IN_SECONDS
//   float GameDayTime() / GameTime() / GameDeltaTime()               world day time / game time / frame time
//   uint32_t WeatherState()                                          Weather::CurState (0 = heat wave)
//   void AddShag()                                                   ++World::m_shagCount
namespace obl::guest_script {

constexpr uint32_t kClearDelayedFunction = 0xb2;

// The ids handed straight to a guest function.
inline bool Delegated(uint32_t id)
{
    switch (id) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x14:
    case 0x15:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1f:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x26:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2e:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x3b:
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x45:
    case 0x47:
    case 0x4c:
    case 0x4d:
    case 0x4e:
    case 0x4f:
    case 0x50:
    case 0x52:
    case 0x53:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x58:
    case 0x59:
    case 0x5b: return true;
    default: return false;
    }
}

// The ids whose body is in the switch (not the delegated ones and not 0xb2).
inline bool Inline(uint32_t id)
{
    switch (id) {
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x13:
    case 0x16:
    case 0x17:
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x20:
    case 0x21:
    case 0x25:
    case 0x27:
    case 0x28:
    case 0x2c:
    case 0x2d:
    case 0x2f:
    case 0x30:
    case 0x34:
    case 0x38:
    case 0x39:
    case 0x3a:
    case 0x44:
    case 0x46:
    case 0x48:
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x51:
    case 0x54:
    case 0x5a: return true;
    default: return false;
    }
}

inline bool HandlesCall(uint32_t id)
{
    return Delegated(id) || Inline(id) || id == kClearDelayedFunction;
}

// The text a thought is looked up under: the prefix and one digit 1..3 (`_itoa(rand() % 3 + 1)`).
inline std::string ThoughtKey(const char* prefix, int randomValue)
{
    return std::string(prefix) + std::to_string(randomValue % 3 + 1);
}

// A new thought goes to the front of the three the guest remembers (the other texts move back); -1 (no such text) changes nothing.
inline void PushThought(Guest& g, int32_t text)
{
    if (text == -1) return;
    g.m_Thoughts[2] = g.m_Thoughts[1];
    g.m_Thoughts[1] = g.m_Thoughts[0];
    g.m_Thoughts[0] = text;
}

// The five-word message initializer and the seven-word message initializer: the five (seven) words after the sender and receiver, the rest of the record is not written
// (nothing reads it, so it is sent as zeros).
inline messages::Message Init5(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
{
    messages::Message message{};
    message.sender = sender, message.receiver = receiver, message.type = type, message.flags = flags, message.data = data;
    return message;
}
// Init7 also sets the two words after the data: `extra` and `floatData` (a float).
inline messages::Message Init7(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data, float floatData, int32_t extra)
{
    messages::Message message = Init5(sender, receiver, type, flags, data);
    message.extra = extra, message.floatData = floatData;
    return message;
}

// The thought functions: `GUEST_THOUGHT_<what>` + one digit, the text id looked up, the thought pushed.
template<class Env> void Think(Guest& g, const char* prefix, Env& env)
{
    const int32_t text = env.TextId(ThoughtKey(prefix, env.Rand()).c_str());
    PushThought(g, text);
}

// Opinions moved by `delta` and held at 1.0 (Increment/Improve) or 0.0 (Decrement): the sum is taken unrounded, the field is stored rounded and then
// replaced by the bound when the unrounded value passed it (a NaN never passes the upper bound, and always passes the lower one: unordered = "less").
inline void RaiseOpinions(Guest& g, double delta)
{
    for (float& opinion : g.m_opinion) {
        const double sum = delta + static_cast<double>(opinion);
        opinion = static_cast<float>(sum);
        if (sum > 1.0) opinion = 1.0f;
    }
}
inline void LowerOpinions(Guest& g, double delta)
{
    for (float& opinion : g.m_opinion) {
        const double difference = static_cast<double>(opinion) - delta;
        opinion = static_cast<float>(difference);
        if (!(difference >= 0.0)) opinion = 0.0f;
    }
}

// The unrounded `a - b` of two floats, kept to the bound of 0: the comparison uses the double value, not the stored float.
inline void StoreNotBelowZero(float& field, double value)
{
    field = static_cast<float>(value);
    if (!(value >= 0.0)) field = 0.0f;
}

constexpr float kMoodIconSeconds = 2.0f;
constexpr float kSeventh = 0.14285715f; // the share of one opinion in ImproveOpinions

template<class Env> uint32_t CallFunction(Guest& g, uint32_t id, Env& env)
{
    const auto* message = static_cast<const messages::Message*>(g.m_currentMessage);
    const auto* def = g.m_definition;
    auto* sprite = static_cast<ui::SpriteInstance*>(g.m_sprite);
    const int32_t guestId = static_cast<int32_t>(g.m_id);

    if (Delegated(id)) return env.Delegate(g, id);
    switch (id) {
    case 0x08: // FaceSender
        if (object::Base* sender = env.Object(message->sender)) env.FaceTowards(sprite, sender->m_WorldXYZ, g.m_WorldXYZ);
        return 0;
    case 0x09: { // PlayToiletSound
        float position[3] = {g.m_WorldXYZ[0], g.m_WorldXYZ[1], g.m_WorldXYZ[2]};
        env.PlaySound3D("toilet1", position);
        return 0;
    }
    case 0x0a: // AddRide: the destination goes into the ring of the last five, the oldest is dropped when the ring is full
        g.m_rides_start = static_cast<int32_t>((static_cast<uint32_t>(g.m_rides_start) + 1) % 5);
        g.m_rides_last[g.m_rides_start] = g.m_DestObjectID;
        if (g.m_rides_start == g.m_rides_end) g.m_rides_end = static_cast<int32_t>((static_cast<uint32_t>(g.m_rides_end) + 1) % 5);
        return 0;
    case 0x0b: g.m_visible = 0; return id; // SetInvisible
    case 0x0c: g.m_visible = 1; return id; // SetVisible
    case 0x13: { // ImproveOpinions: a seventh of the message's amount to every opinion, then a thought
        RaiseOpinions(g, static_cast<double>(message->floatData) * static_cast<double>(kSeventh));
        Think(g, "GUEST_THOUGHT_TALKED_TO_REP", env);
        return 0;
    }
    case 0x16: // DoIFancySender: one in three says yes (3), else one in three of the rest (8), else 2
        if (env.Rand() % 3 == 0)
            g.m_ActionState = 3;
        else
            g.m_ActionState = env.Rand() % 3 == 0 ? 8 : 2;
        return 0;
    case 0x17: { // WalkToBeach: the second point of the sunbathing record becomes the first walk point, and the guest is sent there
        const float* point = env.FindSunbathePoint(g) + 3;
        g.m_walkpoints[0][0] = point[0];
        g.m_walkpoints[0][1] = point[1];
        g.m_walkpoint = 0;
        env.Post(Init5(guestId, guestId, messages::MT_PAVEMENT2POINT, Truncate(point[0]), Truncate(point[1])));
        return 0;
    }
    case 0x1c: { // FindWalkPoint
        float point[3];
        env.FindRandomWalkPoint(point);
        g.m_walkpoint = 0;
        std::memcpy(g.m_walkpoints[0], point, sizeof(point));
        return 0;
    }
    case 0x1d: sprite->m_lit = 0; return id; // UnLit
    case 0x1e: sprite->m_lit = 1; return id; // Lit
    case 0x20: { // WakeUpTime: the delayed "finish action" arrives when the bed time ends, in game seconds
        const double now = env.GameDayTime();
        const double wake = g.m_WakeTime;
        // `now <= wake`, an unordered pair counting as true, is the bed time of today; otherwise the one after midnight
        const float hours = static_cast<float>(!(now > wake) ? wake - now : (24.0 - now) + wake);
        const int32_t dayLength = env.DayLengthSeconds();
        const float delay =
                static_cast<float>(static_cast<double>(hours) * static_cast<double>(0.041666668f) * static_cast<double>(dayLength));
        env.PostDelayed(Init7(guestId, guestId, messages::MT_FINISH_ACTION, 0, 0, 0.0f, 0x2b), delay);
        return 0;
    }
    case 0x21: // LeaveHotel
        --static_cast<object::Building*>(env.Object(g.m_StayingAt))->m_NumGuests;
        return 0;
    case 0x25: { // SurfPaddlePoint
        float position[3] = {g.m_WorldXYZ[0], g.m_WorldXYZ[1], g.m_WorldXYZ[2]};
        const float* point = env.FindSeaPoint(g, position);
        env.Post(Init5(guestId, guestId, messages::MT_SWIM2SEAPOINT, Truncate(point[3]), Truncate(point[4])));
        return 0;
    }
    case 0x27: ++static_cast<object::Building*>(env.Object(g.m_ActivityPerson))->m_NumBoards; return 0; // GiveBackBoard
    case 0x28: ++static_cast<object::Building*>(env.Object(g.m_ActivityPerson))->m_NumLilos; return 0; // GiveBackLilo
    case 0x2c: { // Sunburn: the protection falls with the hours of the day that pass (twice as fast in a heat wave, half as fast with lotion on)
        double burn = static_cast<double>(env.GameDeltaTime()) * 24.0 / static_cast<double>(env.DayLengthSeconds());
        if (env.WeatherState() == 0) burn += burn;
        if (g.m_AppliedLotion != 0) burn *= 0.5;
        g.m_sunBurnLevel = static_cast<float>(static_cast<double>(g.m_sunBurnLevel) - burn);
        if (!(g.m_sunBurnLevel >= -1.0f)) {
            g.m_sunBurnLevel = -1.0f;
            g.m_desires[3] = 1.0f; // FirstAid
        }
        return 0;
    }
    case 0x2d: env.Post(Init5(guestId, g.m_ActivityPerson, messages::MT_STATECHANGE, 0, 0)); return 0; // ReleaseBin
    case 0x2f: { // HeadForShore: the first point of the sea record this time
        float position[3] = {g.m_WorldXYZ[0], g.m_WorldXYZ[1], g.m_WorldXYZ[2]};
        const float* point = env.FindSeaPoint(g, position);
        const float x = point[0], y = point[1];
        env.Post(Init5(guestId, guestId, messages::MT_SWIM2SEAPOINT, Truncate(x), Truncate(y)));
        return 0;
    }
    case 0x30: // BadSafetyOpinion
        StoreNotBelowZero(g.m_opinion[tables::Safety],
                static_cast<double>(g.m_opinion[tables::Safety]) - static_cast<double>(def->m_OpinionEffect[tables::Safety][1]));
        return 0;
    case 0x34: { // ShowThumbsOverlay: the mood icon follows the last good or bad purchase, at most once in two seconds
        if (g.m_lastGood != 0) {
            g.m_lastGood = 0;
            if (!(g.m_moodIconTime > 0.0f)) {
                g.m_moodIcon->m_frameIndex = 0;
                g.m_moodIconTime = kMoodIconSeconds;
            }
        } else if (g.m_lastBad != 0) {
            g.m_lastBad = 0;
            if (!(g.m_moodIconTime > 0.0f)) {
                g.m_moodIcon->m_frameIndex = 1;
                g.m_moodIconTime = kMoodIconSeconds;
            }
        }
        return 0;
    }
    case 0x38: RaiseOpinions(g, static_cast<double>(message->floatData)); return 0; // IncrementOpinions
    case 0x39: LowerOpinions(g, static_cast<double>(message->floatData)); return 0; // DecrementOpinions
    case 0x3a: { // CheckToiletStench: desperate walkers calm down, and a building cleaned too rarely costs the guest its cleanliness opinion
        if (ascii::EqualsNoCase("D_WALK_DESPERATE", g.m_WalkAnimation)) std::strcpy(g.m_WalkAnimation, "D_WALK_NORMAL");
        auto* building = static_cast<object::Building*>(env.Object(g.m_DestObjectID));
        if (building->m_VisitsSinceCleaned >= 0x19) {
            const float penalty = def->m_OpinionEffect[tables::Cleanliness][1];
            g.m_cleanScore = static_cast<float>(static_cast<double>(penalty) * -3.0);
            StoreNotBelowZero(g.m_opinion[tables::Cleanliness],
                    static_cast<double>(g.m_opinion[tables::Cleanliness]) - static_cast<double>(penalty) * 3.0);
        }
        return 0;
    }
    case 0x44: { // RandomCrowdAnim: one of two animations after 7 to 30 seconds
        const int32_t variant = env.Rand() % 2 + 1;
        const messages::Message animation = Init7(guestId, guestId, messages::MT_PLAYANIMATION, 1, variant, 0.0f, 0x3e);
        const float delay = static_cast<float>(static_cast<double>(env.Rand()) * static_cast<double>(ex::kRandScale) * 23.0 + 7.0);
        env.PostDelayed(animation, delay);
        return 0;
    }
    case 0x46: // ReleaseHotel
        if (g.m_StayingAt != -1) {
            --static_cast<object::Building*>(env.Object(g.m_StayingAt))->m_NumGuests;
            g.m_StayingAt = -1;
        }
        return 0;
    case 0x48:
        Think(g, def->m_Gender != 0 ? "GUEST_THOUGHT_PULLED_FEMALE" : "GUEST_THOUGHT_PULLED_MALE", env);
        return 0; // AddPulledThought
    case 0x49: // AddSexThought
        Think(g, def->m_Gender != 0 ? "GUEST_THOUGHT_SEX_FEMALE" : "GUEST_THOUGHT_SEX_MALE", env);
        env.AddShag();
        return 0;
    case 0x4a: Think(g, "GUEST_THOUGHT_DUMPED", env); return 0; // AddDumpedThought
    case 0x4b: Think(g, "GUEST_THOUGHT_GETLOST", env); return 0; // AddOffThought
    case 0x51: Think(g, "GUEST_THOUGHT_THIS_IS_CRAP", env); return 0; // AddBadLeaveThought
    case 0x54: // SetupFightTime
        g.m_OffenceState = 2;
        g.m_timeOfOffence = env.GameTime();
        return id;
    case 0x5a: // ShowThumbsDown
        g.m_moodIcon->m_frameIndex = 1;
        g.m_moodIconTime = kMoodIconSeconds;
        return 0;
    case kClearDelayedFunction: env.ClearDelayed(guestId); return 0;
    default: env.InvalidFunction(); return 0;
    }
}

} // namespace obl::guest_script
