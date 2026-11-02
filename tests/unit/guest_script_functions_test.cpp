#include "object_storage.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"

#include "game/objects/guest_call_table.h"
#include "game/objects/guest_script_functions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using ui::SpriteInstance;
using enum obl::messages::MessageType;
using obl::object::Building;
using obl::object::Guest;
using namespace obl::tables;
namespace gs = obl::guest_script;

namespace {

struct Posted {
    Message message;
    bool delayed = false;
    float delay = 0;
};

struct Fixture {
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();
    std::unique_ptr<obl::tables::GuestDef> def = std::make_unique<obl::tables::GuestDef>();
    std::unique_ptr<Building> building = std::make_unique<Building>();
    std::unique_ptr<Building> hotel = std::make_unique<Building>();
    std::unique_ptr<object::Base> sender = std::make_unique<object::Base>();
    obl::ui::SpriteInstance icon{};
    Message message{};
    SpriteInstance sprite{};

    Fixture()
    {
        obl::unit::FillObject<Guest>(*guest);
        obl::unit::FillObject(*building);
        obl::unit::FillObject(*hotel);
        obl::unit::FillObject<object::Base>(*sender);
        *def = GuestDef{};
        guest->m_definition = def.get();
        guest->m_currentMessage = &message;
        guest->m_sprite = &sprite;
        guest->m_moodIcon = &icon;
        guest->m_id = 99;
        building->m_id = 7;
        hotel->m_id = 8;
        sender->m_id = 5;
        guest->m_DestObjectID = 7;
        guest->m_ActivityPerson = 7;
        guest->m_StayingAt = 8;
        guest->m_WorldXYZ[0] = 1.0f, guest->m_WorldXYZ[1] = 2.0f, guest->m_WorldXYZ[2] = 3.0f;
        sender->m_WorldXYZ[0] = 10.0f, sender->m_WorldXYZ[1] = 20.0f, sender->m_WorldXYZ[2] = 30.0f;
    }
};

struct FakeEnv {
    Fixture& f;
    std::vector<std::string> log;
    std::vector<Posted> posted;
    std::vector<uint32_t> delegated;
    int unrecognized = 0;
    std::vector<int> rands;
    size_t nextRand = 0;
    float sunbathe[6] = {0, 0, 0, 30.9f, -4.5f, 7.0f};
    float sea[6] = {11.9f, -12.5f, 0.0f, 21.5f, 22.5f, 0.0f};
    float walkPoint[3] = {5.0f, 6.0f, 7.0f};
    std::string lastKey;
    int32_t textId = 77;
    int32_t dayLength = 600;
    float dayTime = 10.0f, gameTime = 123.5f, delta = 0.25f;
    uint32_t weather = 1;
    int shags = 0;

    explicit FakeEnv(Fixture& fixture) : f(fixture) {}

    uint32_t Delegate(Guest&, uint32_t id)
    {
        delegated.push_back(id);
        return 0x1234;
    }
    void InvalidFunction() { ++unrecognized; }
    object::Base* Object(int32_t id)
    {
        log.push_back("object " + std::to_string(id));
        if (id == 7) return f.building.get();
        if (id == 8) return f.hotel.get();
        if (id == 5) return f.sender.get();
        return nullptr;
    }
    void FaceTowards(SpriteInstance*, const float* target, const float* from)
    {
        log.push_back("face " + std::to_string(target[0]) + " " + std::to_string(from[0]));
    }
    void PlaySound3D(const char* name, float* position) { log.push_back(std::string("sound ") + name + " " + std::to_string(position[2])); }
    const float* FindSunbathePoint(Guest&) { return sunbathe; }
    const float* FindSeaPoint(Guest&, const float* position)
    {
        log.push_back("sea " + std::to_string(position[1]));
        return sea;
    }
    void FindRandomWalkPoint(float* out) { std::memcpy(out, walkPoint, sizeof(walkPoint)); }
    int32_t TextId(const char* key)
    {
        lastKey = key;
        return textId;
    }
    void Post(const Message& message) { posted.push_back({message, false, 0}); }
    void PostDelayed(const Message& message, float delay) { posted.push_back({message, true, delay}); }
    void ClearDelayed(int32_t receiver) { log.push_back("clear " + std::to_string(receiver)); }
    int Rand() { return nextRand < rands.size() ? rands[nextRand++] : 0; }
    int32_t DayLengthSeconds() { return dayLength; }
    float GameDayTime() { return dayTime; }
    float GameTime() { return gameTime; }
    float GameDeltaTime() { return delta; }
    uint32_t WeatherState() { return weather; }
    void AddShag() { ++shags; }
};

uint32_t Call(Fixture& f, FakeEnv& env, uint32_t id)
{
    return gs::CallFunction(*f.guest, id, env);
}

} // namespace

OBL_TEST_CASE("guest_script.call_classifies_every_number")
{
    int delegated = 0, inlineCases = 0;
    for (uint32_t id = 0; id < 0x100; ++id) {
        Fixture f;
        FakeEnv env(f);
        f.guest->m_walkpoint = 0;
        f.message.sender = 5;
        f.guest->m_rides_start = 0, f.guest->m_rides_end = 1;
        Call(f, env, id);
        // Exactly one of: handed on to a guest function, a case of its own, 0xb2, or the FatalError.
        const bool handled = !env.delegated.empty() || env.unrecognized == 0;
        OBL_CHECK_EQ(handled, gs::HandlesCall(id));
        OBL_CHECK_EQ(env.unrecognized == 1, !gs::HandlesCall(id));
        OBL_CHECK_EQ(env.delegated.size() == 1, gs::Delegated(id));
        OBL_CHECK(!(gs::Delegated(id) && gs::Inline(id)));
        delegated += gs::Delegated(id), inlineCases += gs::Inline(id);
    }
    OBL_CHECK_EQ(delegated, 58);
    OBL_CHECK_EQ(inlineCases, 33);
    OBL_CHECK_EQ(std::size(obl::object::kGuestCallDelegateIds), size_t{58});
    std::set<int> seen;
    for (const uint8_t id : obl::object::kGuestCallDelegateIds) {
        OBL_CHECK(gs::Delegated(id));
        OBL_CHECK(seen.insert(id).second);
    }
    Fixture f;
    FakeEnv env(f);
    OBL_CHECK_EQ(Call(f, env, 0x00), 0x1234u); // the callee's result is handed back
}

OBL_TEST_CASE("guest_script.call_flags_rides_and_sprite")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    Call(f, env, 0x0b);
    OBL_CHECK_EQ(g.m_visible, 0);
    Call(f, env, 0x0c);
    OBL_CHECK_EQ(g.m_visible, 1);
    Call(f, env, 0x1e);
    OBL_CHECK_EQ(f.sprite.m_lit, 1);
    Call(f, env, 0x1d);
    OBL_CHECK_EQ(f.sprite.m_lit, 0);
    // The ring of the last five attractions: the write index runs 1,2,3,4,0 ...; the oldest index follows once the ring is full.
    g.m_rides_start = 0, g.m_rides_end = 0;
    for (int i = 1; i <= 7; ++i) {
        g.m_DestObjectID = 100 + i;
        Call(f, env, 0x0a);
    }
    OBL_CHECK_EQ(g.m_rides_start, 2);
    OBL_CHECK_EQ(g.m_rides_end, 3);
    OBL_CHECK_EQ(g.m_rides_last[0], 105);
    OBL_CHECK_EQ(g.m_rides_last[1], 106);
    OBL_CHECK_EQ(g.m_rides_last[2], 107);
    OBL_CHECK_EQ(g.m_rides_last[3], 103);
    OBL_CHECK_EQ(g.m_rides_last[4], 104);
    f.message.sender = 5;
    Call(f, env, 0x08);
    OBL_CHECK(env.log.back() == "face " + std::to_string(10.0f) + " " + std::to_string(1.0f));
    env.log.clear();
    f.message.sender = 6; // nobody: nothing to face
    Call(f, env, 0x08);
    OBL_CHECK_EQ(env.log.size(), 1u);
    Call(f, env, 0x09);
    OBL_CHECK(env.log.back() == "sound toilet1 " + std::to_string(3.0f));
    g.m_StayingAt = 8;
    Call(f, env, 0x54);
    OBL_CHECK_EQ(g.m_OffenceState, 2u);
    OBL_CHECK_EQ(g.m_timeOfOffence, 123.5f);
}

OBL_TEST_CASE("guest_script.call_opinions_and_thoughts")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    for (float& o : g.m_opinion) o = 0.5f;
    g.m_opinion[2] = 0.99f;
    f.message.floatData = 0.25f;
    Call(f, env, 0x38); // IncrementOpinions
    OBL_CHECK_EQ(g.m_opinion[0], 0.75f);
    OBL_CHECK_EQ(g.m_opinion[2], 1.0f); // held at 1.0
    f.message.floatData = 0.875f;
    Call(f, env, 0x39); // DecrementOpinions
    OBL_CHECK_EQ(g.m_opinion[0], 0.0f); // 0.75 - 0.875 < 0: held at 0
    OBL_CHECK_EQ(g.m_opinion[2], 0.125f);
    g.m_opinion[3] = std::numeric_limits<float>::quiet_NaN();
    Call(f, env, 0x39);
    OBL_CHECK_EQ(g.m_opinion[3], 0.0f); // unordered counts as "below 0"
    g.m_opinion[3] = std::numeric_limits<float>::quiet_NaN();
    Call(f, env, 0x38);
    OBL_CHECK(std::isnan(g.m_opinion[3])); // but not above 1
    // ImproveOpinions: a seventh of the amount, then the thought of the talk with the rep
    for (float& o : g.m_opinion) o = 0.5f;
    f.message.floatData = 0.7f;
    env.rands = {5};
    Call(f, env, 0x13);
    OBL_CHECK_EQ(g.m_opinion[0], static_cast<float>(0.5 + static_cast<double>(0.7f) * static_cast<double>(0.14285715f)));
    OBL_CHECK(env.lastKey == "GUEST_THOUGHT_TALKED_TO_REP3"); // 5 % 3 + 1
    OBL_CHECK_EQ(g.m_Thoughts[0], 77);
    env.textId = 78;
    env.rands = {0};
    env.nextRand = 0;
    Call(f, env, 0x4a);
    env.textId = -1; // no such text: the thoughts stay
    env.nextRand = 0;
    Call(f, env, 0x4b);
    OBL_CHECK_EQ(g.m_Thoughts[0], 78);
    OBL_CHECK_EQ(g.m_Thoughts[1], 77);
    OBL_CHECK_EQ(g.m_Thoughts[2], 0);
    OBL_CHECK(env.lastKey == "GUEST_THOUGHT_GETLOST1");
    env.textId = 79;
    f.def->m_Gender = 1;
    env.nextRand = 0;
    env.rands = {1};
    Call(f, env, 0x48);
    OBL_CHECK(env.lastKey == "GUEST_THOUGHT_PULLED_FEMALE2");
    f.def->m_Gender = 0;
    env.nextRand = 0;
    Call(f, env, 0x49);
    OBL_CHECK(env.lastKey == "GUEST_THOUGHT_SEX_MALE2");
    OBL_CHECK_EQ(env.shags, 1);
    env.nextRand = 0;
    Call(f, env, 0x51);
    OBL_CHECK(env.lastKey == "GUEST_THOUGHT_THIS_IS_CRAP2");
    OBL_CHECK_EQ(g.m_Thoughts[0], 79);
    OBL_CHECK_EQ(g.m_Thoughts[1], 79);
    OBL_CHECK_EQ(g.m_Thoughts[2], 79); // pulled, sex and crap: three pushes of the same id
    // BadSafetyOpinion and the toilet stench
    g.m_opinion[tables::Safety] = 0.5f;
    f.def->m_OpinionEffect[tables::Safety][1] = 0.25f;
    Call(f, env, 0x30);
    OBL_CHECK_EQ(g.m_opinion[tables::Safety], 0.25f);
    Call(f, env, 0x30);
    Call(f, env, 0x30);
    OBL_CHECK_EQ(g.m_opinion[tables::Safety], 0.0f);
    f.def->m_OpinionEffect[tables::Cleanliness][1] = 0.125f;
    g.m_opinion[tables::Cleanliness] = 0.5f;
    std::strcpy(g.m_WalkAnimation, "d_walk_DESPERATE");
    f.building->m_VisitsSinceCleaned = 24;
    Call(f, env, 0x3a);
    OBL_CHECK(std::strcmp(g.m_WalkAnimation, "D_WALK_NORMAL") == 0);
    OBL_CHECK_EQ(g.m_opinion[tables::Cleanliness], 0.5f); // clean enough
    f.building->m_VisitsSinceCleaned = 25;
    Call(f, env, 0x3a);
    OBL_CHECK_EQ(g.m_cleanScore, -0.375f);
    OBL_CHECK_EQ(g.m_opinion[tables::Cleanliness], 0.125f);
}

OBL_TEST_CASE("guest_script.call_decisions_that_draw")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    env.rands = {3};
    Call(f, env, 0x16);
    OBL_CHECK_EQ(g.m_ActionState, 3);
    OBL_CHECK_EQ(env.nextRand, 1u); // the second draw only when the first said no
    env.nextRand = 0;
    env.rands = {1, 6};
    Call(f, env, 0x16);
    OBL_CHECK_EQ(g.m_ActionState, 8);
    env.nextRand = 0;
    env.rands = {1, 5};
    Call(f, env, 0x16);
    OBL_CHECK_EQ(g.m_ActionState, 2);
    // FindWalkPoint: the point goes to walk point 0
    g.m_walkpoint = 2;
    Call(f, env, 0x1c);
    OBL_CHECK_EQ(g.m_walkpoint, 0);
    OBL_CHECK_EQ(g.m_walkpoints[0][0], 5.0f);
    OBL_CHECK_EQ(g.m_walkpoints[0][2], 7.0f);
    // RandomCrowdAnim: 1 + rand % 2, then a delay of 7..30 seconds from the second draw
    env.nextRand = 0;
    env.rands = {3, 32767};
    env.posted.clear();
    Call(f, env, 0x44);
    OBL_REQUIRE(env.posted.size() == 1u);
    OBL_CHECK(env.posted[0].delayed);
    OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(MT_PLAYANIMATION));
    OBL_CHECK_EQ(env.posted[0].message.flags, 1);
    OBL_CHECK_EQ(env.posted[0].message.data, 2);
    OBL_CHECK_EQ(env.posted[0].message.extra, 0x3e);
    OBL_CHECK_EQ(env.posted[0].message.floatData, 0.0f);
    OBL_CHECK_EQ(env.posted[0].message.sender, 99);
    OBL_CHECK_EQ(env.posted[0].message.receiver, 99);
    OBL_CHECK(std::fabs(env.posted[0].delay - 30.0f) < 0.001f);
}

OBL_TEST_CASE("guest_script.call_messages_to_the_world")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    // WalkToBeach: the second point of the sunbathing record, truncated, as a pavement move
    Call(f, env, 0x17);
    OBL_REQUIRE(env.posted.size() == 1u);
    OBL_CHECK(!env.posted[0].delayed);
    OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(MT_PAVEMENT2POINT));
    OBL_CHECK_EQ(env.posted[0].message.flags, 30);
    OBL_CHECK_EQ(env.posted[0].message.data, -4);
    OBL_CHECK_EQ(g.m_walkpoints[0][0], 30.9f);
    OBL_CHECK_EQ(g.m_walkpoints[0][1], -4.5f);
    OBL_CHECK_EQ(g.m_walkpoints[0][2], 0.0f); // the third word is not copied
    OBL_CHECK_EQ(g.m_walkpoint, 0);
    env.posted.clear();
    Call(f, env, 0x25);
    OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(MT_SWIM2SEAPOINT));
    OBL_CHECK_EQ(env.posted[0].message.flags, 21);
    OBL_CHECK_EQ(env.posted[0].message.data, 22);
    env.posted.clear();
    Call(f, env, 0x2f);
    OBL_CHECK_EQ(env.posted[0].message.flags, 11);
    OBL_CHECK_EQ(env.posted[0].message.data, -12);
    // ReleaseBin tells the bin's guest-of-the-moment (the action person) to leave its state
    env.posted.clear();
    Call(f, env, 0x2d);
    OBL_CHECK_EQ(env.posted[0].message.sender, 99);
    OBL_CHECK_EQ(env.posted[0].message.receiver, 7);
    OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(MT_STATECHANGE));
    // WakeUpTime: hours to the bed time, in game seconds (24 hours = the day length)
    env.posted.clear();
    g.m_WakeTime = 12.0f; // from 10:00: two hours
    Call(f, env, 0x20);
    OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(MT_FINISH_ACTION));
    OBL_CHECK_EQ(env.posted[0].message.extra, 0x2b);
    OBL_CHECK(env.posted[0].delayed);
    OBL_CHECK(std::fabs(env.posted[0].delay - 50.0f) < 0.001f);
    env.posted.clear();
    g.m_WakeTime = 8.0f; // already past: tomorrow, 22 hours
    Call(f, env, 0x20);
    OBL_CHECK(std::fabs(env.posted[0].delay - 550.0f) < 0.01f);
    env.posted.clear();
    Call(f, env, 0xb2); // ClearMyDelayedMsgs
    OBL_CHECK_EQ(env.log.back() == "clear 99", true);
    OBL_CHECK_EQ(env.posted.size(), 0u);
}

OBL_TEST_CASE("guest_script.call_counters_icons_and_sunburn")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    f.hotel->m_NumGuests = 5;
    Call(f, env, 0x21);
    OBL_CHECK_EQ(f.hotel->m_NumGuests, 4);
    Call(f, env, 0x46);
    OBL_CHECK_EQ(f.hotel->m_NumGuests, 3);
    OBL_CHECK_EQ(g.m_StayingAt, -1);
    Call(f, env, 0x46); // not staying anywhere: nothing
    OBL_CHECK_EQ(f.hotel->m_NumGuests, 3);
    f.building->m_NumBoards = 1, f.building->m_NumLilos = 2;
    Call(f, env, 0x27);
    Call(f, env, 0x28);
    OBL_CHECK_EQ(f.building->m_NumBoards, 2);
    OBL_CHECK_EQ(f.building->m_NumLilos, 3);
    // The mood icon: at most once in two seconds (the time is counted down elsewhere)
    g.m_lastGood = 1, g.m_moodIconTime = 0.0f;
    Call(f, env, 0x34);
    OBL_CHECK_EQ(f.icon.m_frameIndex, 0);
    OBL_CHECK_EQ(g.m_moodIconTime, 2.0f);
    OBL_CHECK_EQ(g.m_lastGood, 0);
    g.m_lastBad = 1;
    Call(f, env, 0x34); // time still running: the flag is consumed, the icon stays
    OBL_CHECK_EQ(f.icon.m_frameIndex, 0);
    OBL_CHECK_EQ(g.m_lastBad, 0);
    g.m_lastBad = 1, g.m_moodIconTime = std::numeric_limits<float>::quiet_NaN();
    Call(f, env, 0x34); // NaN is "not above 0"
    OBL_CHECK_EQ(f.icon.m_frameIndex, 1);
    f.icon.m_frameIndex = 0;
    g.m_moodIconTime = 0.0f;
    Call(f, env, 0x5a);
    OBL_CHECK_EQ(f.icon.m_frameIndex, 1);
    OBL_CHECK_EQ(g.m_moodIconTime, 2.0f);
    // Sunburn: dt x 24 / day length per call, doubled in a heat wave, halved with lotion
    g.m_sunBurnLevel = 0.5f;
    env.delta = 0.25f, env.dayLength = 600;
    env.weather = 1;
    Call(f, env, 0x2c);
    OBL_CHECK_EQ(g.m_sunBurnLevel, static_cast<float>(0.5 - 0.25 * 24.0 / 600.0));
    env.weather = 0;
    g.m_sunBurnLevel = 0.5f;
    Call(f, env, 0x2c);
    OBL_CHECK_EQ(g.m_sunBurnLevel, static_cast<float>(0.5 - 2 * (0.25 * 24.0 / 600.0)));
    env.weather = 1;
    g.m_AppliedLotion = 1;
    g.m_sunBurnLevel = 0.5f;
    Call(f, env, 0x2c);
    OBL_CHECK_EQ(g.m_sunBurnLevel, static_cast<float>(0.5 - 0.5 * (0.25 * 24.0 / 600.0)));
    g.m_sunBurnLevel = -0.995f; // below -1: held there and the first-aid desire is raised
    env.delta = 100.0f;
    Call(f, env, 0x2c);
    OBL_CHECK_EQ(g.m_sunBurnLevel, -1.0f);
    OBL_CHECK_EQ(g.m_desires[3], 1.0f);
}
