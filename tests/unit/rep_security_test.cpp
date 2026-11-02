#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/objects/staff_rep_security.h"

#include <bit>
#include <cstring>
#include <limits>
#include <vector>

// The representative and security guard helpers (src/game/objects/staff_rep_security.h) against a fake Env.

using namespace obl;
using namespace obl::staff::repsec;

namespace {

struct Env {
    std::vector<int> draws;
    size_t used = 0;
    std::vector<obl::messages::Message> sent;
    std::vector<std::pair<obl::messages::Message, float>> delayed;
    std::vector<object::Base*> near;
    std::vector<object::Base*> buildings;
    std::vector<object::Base*> byId;
    bool approachable = true;
    bool walkable = true;
    float walkTo[3] = {11.0f, 22.0f, 33.0f};
    std::vector<std::vector<float>> walkQueries;
    int walks = 0;
    int guestCount = 0, binCount = 0;
    std::vector<uint32_t> thoughts;
    std::vector<std::vector<float>> faced;
    float guestAt[3] = {500.0f, 600.0f, 7.0f};

    int Rand() { return used < draws.size() ? draws[used++] : (++used, 0); }
    void Post(const obl::messages::Message& message) { sent.push_back(message); }
    void PostDelayed(const obl::messages::Message& message, float delay) { delayed.push_back({message, delay}); }
    object::Base* Object(int32_t id)
    {
        for (object::Base* object : byId)
            if (static_cast<int32_t>(object->m_id) == id) return object;
        return nullptr;
    }
    uint32_t ObjectsNear(int, object::Base* const*& list)
    {
        list = near.data();
        return static_cast<uint32_t>(near.size());
    }
    uint32_t Buildings(object::Base* const*& list)
    {
        list = buildings.data();
        return static_cast<uint32_t>(buildings.size());
    }
    // The test buildings are plain objects: the rule is the name the catalog gives a jetty (building_behavior.h).
    bool IsJetty(const object::Base* building) { return std::strstr(building->m_Name, "JETTY") != nullptr; }
    bool IsApproachable(object::Base*) { return approachable; }
    bool FindWalkablePointNear(float pos[3], int dx, int dy)
    {
        walkQueries.push_back({pos[0], pos[1], pos[2], static_cast<float>(dx), static_cast<float>(dy)});
        if (walkable) std::memcpy(pos, walkTo, sizeof(walkTo));
        return walkable;
    }
    void PlayWalk() { ++walks; }
    void Position(object::Base*, float out[3]) { std::memcpy(out, guestAt, sizeof(guestAt)); }
    void FaceTowards(ui::SpriteInstance*, const float target[3], const float from[3])
    {
        faced.push_back({target[0], target[1], target[2], from[0], from[1], from[2]});
    }
    void PushThought(uint32_t bits) { thoughts.push_back(bits); }
    uint32_t CountGuests() { return static_cast<uint32_t>(guestCount); }
    uint32_t CountBins() { return static_cast<uint32_t>(binCount); }
};

void Object(object::Base& object, uint32_t id, const char* name = "")
{
    object.m_id = id;
    std::strncpy(object.m_Name, name, sizeof(object.m_Name) - 1);
}

void MakeGuest(object::Guest& guest, uint32_t id, int32_t offence)
{
    guest.m_id = id;
    guest.m_type = object::ObjectType::Guest;
    guest.m_OffenceState = offence;
    guest.m_WorldXYZ[0] = 100.0f;
    guest.m_WorldXYZ[1] = 200.0f;
    guest.m_WorldXYZ[2] = 3.0f;
}

void MakeRep(object::Rep& rep)
{
    rep.m_id = 5;
    rep.m_talking_id = -1;
}

void Guard(object::SecurityGuard& guard)
{
    guard.m_id = 6;
    guard.m_offenderId = -1;
    guard.m_fightWithId = -1;
    guard.m_DestObjectID = -1;
}

uint32_t Bits(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

OBL_TEST_CASE("rep_fns.move_to_next_building_redraws_the_destination_and_jetties")
{
    object::Base a{}, jetty{}, b{};
    Object(a, 10);
    Object(jetty, 11, "MY_JETTY_1");
    Object(b, 12);
    Env env;
    env.buildings = {&a, &jetty, &b};
    env.draws = {0, 1, 0, 2}; // a is the destination, the jetty is skipped, a again, then b
    object::Rep rep{};
    MakeRep(rep);
    rep.m_DestObjectID = 10;
    OBL_CHECK(MoveToNextBuilding(rep, env));
    OBL_CHECK_EQ(env.used, size_t{4});
    OBL_CHECK_EQ(env.walks, 1);
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK_EQ(env.sent[0].sender, 5);
    OBL_CHECK_EQ(env.sent[0].receiver, 5);
    OBL_CHECK_EQ(env.sent[0].type, kMsgPavement2Object);
    OBL_CHECK_EQ(env.sent[0].data, 12);
}

OBL_TEST_CASE("rep_fns.move_to_next_building_declines_when_nothing_is_eligible")
{
    object::Base a{}, jetty{};
    Object(a, 10);
    Object(jetty, 11, "JETTY");
    object::Rep rep{};
    MakeRep(rep);
    rep.m_DestObjectID = 10;
    Env env;
    env.buildings = {&a, &jetty};
    OBL_CHECK(!MoveToNextBuilding(rep, env));
    OBL_CHECK_EQ(env.used, size_t{0});
    OBL_CHECK(env.sent.empty());
    Env none;
    OBL_CHECK(!MoveToNextBuilding(rep, none));
}

OBL_TEST_CASE("rep_fns.look_for_guests_finds_a_guest_and_walks_to_the_point_next_to_it")
{
    object::Guest guest{};
    MakeGuest(guest, 40, 0);
    Env env;
    env.near = {&guest};
    env.walkTo[0] = 120.9f;
    env.walkTo[1] = -3.5f;
    object::Rep rep{};
    MakeRep(rep);
    OBL_CHECK(LookForGuests(rep, env));
    OBL_CHECK_EQ(env.used, size_t{1});
    OBL_CHECK_EQ(rep.m_talking_id, 40);
    OBL_CHECK_EQ(rep.m_sentToGuest, 1);
    OBL_CHECK_EQ(Bits(rep.m_GuestOffSetPosX), Bits(120.9f));
    OBL_CHECK_EQ(Bits(rep.m_GuestOffSetPosZ), Bits(33.0f));
    OBL_CHECK_EQ(env.walks, 1);
    OBL_REQUIRE(env.walkQueries.size() == 1);
    OBL_CHECK_EQ(env.walkQueries[0][0], 100.0f);
    OBL_CHECK_EQ(env.walkQueries[0][3], 20.0f);
    OBL_CHECK_EQ(env.walkQueries[0][4], 10.0f);
    OBL_REQUIRE(env.sent.size() == 2);
    OBL_CHECK_EQ(env.sent[0].sender, 5);
    OBL_CHECK_EQ(env.sent[0].receiver, 40);
    OBL_CHECK_EQ(env.sent[0].type, kMsgStateChange);
    OBL_CHECK_EQ(env.sent[0].data, kStateTalking);
    OBL_CHECK_EQ(env.sent[1].type, kMsgMove2Point);
    OBL_CHECK_EQ(env.sent[1].receiver, 5);
    OBL_CHECK_EQ(env.sent[1].flags, 120);
    OBL_CHECK_EQ(env.sent[1].data, -3); // truncated toward zero
    OBL_CHECK(env.delayed.empty());
}

OBL_TEST_CASE("rep_fns.look_for_guests_looks_again_in_two_seconds_when_nothing_suits")
{
    object::Guest guest{};
    MakeGuest(guest, 40, 0);
    auto expectLookAgain = [](Env& env, size_t draws) {
        OBL_CHECK_EQ(env.used, draws);
        OBL_CHECK(env.sent.empty());
        OBL_REQUIRE(env.delayed.size() == 1);
        OBL_CHECK_EQ(env.delayed[0].first.type, kMsgLookAround);
        OBL_CHECK_EQ(env.delayed[0].first.sender, 5);
        OBL_CHECK_EQ(env.delayed[0].first.receiver, 5);
        OBL_CHECK_EQ(env.delayed[0].second, 2.0f);
    };
    {
        Env env; // nobody near: no draw
        object::Rep rep{};
        MakeRep(rep);
        OBL_CHECK(LookForGuests(rep, env));
        expectLookAgain(env, 0);
    }
    {
        Env env; // the guest it talked to last
        env.near = {&guest};
        object::Rep rep{};
        MakeRep(rep);
        rep.m_talking_id = 40;
        OBL_CHECK(LookForGuests(rep, env));
        expectLookAgain(env, 1);
        OBL_CHECK_EQ(rep.m_sentToGuest, 0);
    }
    {
        Env env;
        env.near = {&guest};
        env.approachable = false;
        object::Rep rep{};
        MakeRep(rep);
        OBL_CHECK(LookForGuests(rep, env));
        expectLookAgain(env, 1);
    }
    {
        Env env;
        env.near = {&guest};
        env.walkable = false;
        object::Rep rep{};
        MakeRep(rep);
        OBL_CHECK(LookForGuests(rep, env));
        expectLookAgain(env, 1);
        OBL_CHECK_EQ(rep.m_talking_id, -1);
    }
}

OBL_TEST_CASE("rep_fns.look_for_guests_stores_the_height_bits_untouched")
{
    object::Guest guest{};
    MakeGuest(guest, 40, 0);
    Env env;
    env.near = {&guest};
    const uint32_t signalling = 0x7fa00001u; // a signalling NaN survives an integer move
    std::memcpy(&env.walkTo[2], &signalling, sizeof(signalling));
    object::Rep rep{};
    MakeRep(rep);
    OBL_CHECK(LookForGuests(rep, env));
    OBL_CHECK_EQ(Bits(rep.m_GuestOffSetPosZ), signalling);
}

OBL_TEST_CASE("rep_fns.arrived_at_guest_faces_the_guest_and_schedules_the_end_of_the_talk")
{
    object::Base guest{};
    Object(guest, 40);
    ui::SpriteInstance sprite{};
    Env env;
    env.byId = {&guest};
    object::Rep rep{};
    MakeRep(rep);
    rep.m_talking_id = 40;
    rep.m_sprite = &sprite;
    rep.m_WorldXYZ[0] = 1.0f;
    rep.m_WorldXYZ[1] = 2.0f;
    rep.m_WorldXYZ[2] = 3.0f;
    rep.m_MinTalkTime = 3;
    rep.m_MaxTalkTime = 10;
    rep.m_timeWithGuests = 0.5f;
    rep.m_sentToGuest = 1;
    OBL_CHECK(ArrivedAtGuest(rep, env));
    OBL_REQUIRE(env.faced.size() == 1);
    OBL_CHECK_EQ(env.faced[0][0], 500.0f);
    OBL_CHECK_EQ(env.faced[0][3], 1.0f);
    OBL_REQUIRE(env.sent.size() == 2);
    OBL_CHECK_EQ(env.sent[0].type, kMsgStateChange);
    OBL_CHECK_EQ(env.sent[0].data, 9);
    OBL_CHECK_EQ(env.sent[0].receiver, 5);
    OBL_CHECK_EQ(env.sent[1].type, kMsgPlayAnimation);
    OBL_CHECK_EQ(env.sent[1].receiver, 40);
    OBL_CHECK_EQ(env.sent[1].extra, kStateTalking);
    OBL_REQUIRE(env.delayed.size() == 1);
    OBL_CHECK_EQ(env.delayed[0].first.type, kMsgFinishAction);
    OBL_CHECK_EQ(env.delayed[0].second, 6.5f); // (10 - 3) * 0.5 + 3
    OBL_CHECK_EQ(rep.m_sentToGuest, 0);
}

OBL_TEST_CASE("rep_fns.arrived_at_guest_reads_the_talk_times_as_unsigned_difference_and_signed_minimum")
{
    object::Base guest{};
    Object(guest, 40);
    ui::SpriteInstance sprite{};
    Env env;
    env.byId = {&guest};
    object::Rep rep{};
    MakeRep(rep);
    rep.m_talking_id = 40;
    rep.m_sprite = &sprite;
    rep.m_MinTalkTime = 10;
    rep.m_MaxTalkTime = 3; // max - min wraps to 4294967289 as an unsigned 64-bit integer
    rep.m_timeWithGuests = 1.0f;
    OBL_CHECK(ArrivedAtGuest(rep, env));
    OBL_REQUIRE(env.delayed.size() == 1);
    OBL_CHECK_EQ(env.delayed[0].second, static_cast<float>(4294967289.0 + 10.0));
}

OBL_TEST_CASE("rep_fns.arrived_at_guest_declines_a_missing_guest_or_sprite")
{
    object::Base guest{};
    Object(guest, 40);
    ui::SpriteInstance sprite{};
    object::Rep rep{};
    MakeRep(rep);
    rep.m_talking_id = 40;
    Env env;
    env.byId = {&guest};
    OBL_CHECK(!ArrivedAtGuest(rep, env)); // no sprite
    rep.m_sprite = &sprite;
    rep.m_talking_id = 41;
    OBL_CHECK(!ArrivedAtGuest(rep, env)); // no guest
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("security_fns.look_for_offender_picks_the_first_matching_guest_by_offence_state")
{
    object::Guest quiet{}, seriousButKnown{}, vandal{}, later{};
    MakeGuest(quiet, 30, 0);
    MakeGuest(seriousButKnown, 31, 2);
    MakeGuest(vandal, 32, 3);
    MakeGuest(later, 33, 1);
    Env env;
    env.near = {&quiet, &seriousButKnown, &vandal, &later};
    env.byId = {&quiet, &seriousButKnown, &vandal, &later};
    vandal.m_WorldXYZ[0] = 321.0f;
    object::SecurityGuard guard{};
    Guard(guard);
    guard.m_moving = 1;
    guard.m_fightWithId = 31; // the fight partner is skipped for state 2
    guard.m_doubleFine = 1;
    uint8_t found = 7;
    OBL_CHECK(LookForOffender(guard, env, found));
    OBL_CHECK_EQ(found, 1);
    OBL_CHECK_EQ(guard.m_doubleFine, 0);
    OBL_CHECK_EQ(guard.m_offenderId, 32);
    OBL_CHECK_EQ(guard.m_severity, 2); // state 3 counts as severity 2
    OBL_CHECK_EQ(guard.m_MisdemeanourPos[0], 321.0f);
    OBL_CHECK_EQ(guard.m_MisdemeanourPos[1], 200.0f);
    OBL_CHECK_EQ(env.used, size_t{0});
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("security_fns.look_for_offender_state_one_is_severity_one_and_the_last_offender_is_skipped")
{
    object::Guest known{}, fresh{};
    MakeGuest(known, 30, 1);
    MakeGuest(fresh, 31, 1);
    Env env;
    env.near = {&known, &fresh};
    env.byId = {&known, &fresh};
    object::SecurityGuard guard{};
    Guard(guard);
    guard.m_moving = 1;
    guard.m_offenderId = 30;
    uint8_t found = 0;
    OBL_CHECK(LookForOffender(guard, env, found));
    OBL_CHECK_EQ(found, 1);
    OBL_CHECK_EQ(guard.m_offenderId, 31);
    OBL_CHECK_EQ(guard.m_severity, 1);
}

OBL_TEST_CASE("security_fns.look_for_offender_without_a_match_resets_the_severity_per_object_and_answers_zero")
{
    object::Guest a{}, b{};
    MakeGuest(a, 30, 0);
    MakeGuest(b, 31, 4);
    object::Base scenery{};
    Object(scenery, 32);
    scenery.m_type = object::ObjectType::Building;
    Env env;
    env.near = {&a, &b, &scenery};
    object::SecurityGuard guard{};
    Guard(guard);
    guard.m_moving = 1;
    guard.m_severity = 2;
    uint8_t found = 9;
    OBL_CHECK(LookForOffender(guard, env, found));
    OBL_CHECK_EQ(found, 0);
    OBL_CHECK_EQ(guard.m_severity, 0);
    Env empty; // nobody near: the severity is not touched
    guard.m_severity = 2;
    OBL_CHECK(LookForOffender(guard, empty, found));
    OBL_CHECK_EQ(found, 0);
    OBL_CHECK_EQ(guard.m_severity, 2);
}

OBL_TEST_CASE("security_fns.look_for_offender_walks_to_a_random_building_when_standing_still")
{
    object::Base a{}, jetty{}, b{};
    Object(a, 10);
    Object(jetty, 11, "JETTY");
    Object(b, 12);
    Env env;
    env.buildings = {&a, &jetty, &b};
    env.draws = {1, 0, 2}; // the jetty, the destination, then b
    object::SecurityGuard guard{};
    Guard(guard);
    guard.m_DestObjectID = 10;
    uint8_t found = 5;
    OBL_CHECK(LookForOffender(guard, env, found));
    OBL_CHECK_EQ(env.used, size_t{3});
    OBL_CHECK_EQ(env.walks, 1);
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK_EQ(env.sent[0].type, kMsgPavement2Object);
    OBL_CHECK_EQ(env.sent[0].data, 12);
}

OBL_TEST_CASE("security_fns.look_for_offender_declines_with_no_building_to_walk_to")
{
    object::SecurityGuard guard{};
    Guard(guard);
    Env env;
    uint8_t found = 5;
    OBL_CHECK(!LookForOffender(guard, env, found));
    OBL_CHECK_EQ(found, 5);
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("security_fns.look_for_offender_keeps_a_signalling_nan_x_quiet_and_y_z_raw")
{
    object::Guest guest{};
    MakeGuest(guest, 30, 1);
    const uint32_t signalling = 0x7fa00001u;
    std::memcpy(&guest.m_WorldXYZ[0], &signalling, sizeof(signalling));
    std::memcpy(&guest.m_WorldXYZ[1], &signalling, sizeof(signalling));
    Env env;
    env.near = {&guest};
    env.byId = {&guest};
    object::SecurityGuard guard{};
    Guard(guard);
    guard.m_moving = 1;
    uint8_t found = 0;
    OBL_CHECK(LookForOffender(guard, env, found));
    OBL_CHECK_EQ(Bits(guard.m_MisdemeanourPos[0]), signalling | 0x00400000u);
    OBL_CHECK_EQ(Bits(guard.m_MisdemeanourPos[1]), signalling);
}

OBL_TEST_CASE("security_fns.choose_action_by_tolerance_class_and_severity")
{
    struct Row {
        int32_t tolerance, severity, action, level;
    };
    // level -1: not written (the guard starts at 7)
    const Row rows[] = {{0, 2, 1, 0}, {0, 1, 0, -1}, {0, 0, 0, -1}, {1, 2, 1, 1}, {1, 1, 1, 0}, {2, 2, 2, -1}, {2, 1, 1, 1}, {3, 2, 0, -1},
            {-1, 1, 0, -1}};
    for (const Row& row : rows) {
        object::Guest guest{};
        MakeGuest(guest, 30, 0);
        Env env;
        env.byId = {&guest};
        object::SecurityGuard guard{};
        Guard(guard);
        guard.m_offenderId = 30;
        guard.m_toleranceClass = row.tolerance;
        guard.m_severity = row.severity;
        guard.m_FineLevel = 7;
        guard.m_action = 9;
        OBL_CHECK(ChooseAction(guard, env));
        OBL_CHECK_EQ(guard.m_action, row.action);
        OBL_CHECK_EQ(guard.m_FineLevel, row.level < 0 ? 7 : row.level);
        OBL_CHECK_EQ(Bits(guard.m_FineOffsetX), Bits(11.0f));
        OBL_CHECK_EQ(Bits(guard.m_FineOffsetY), Bits(22.0f));
        OBL_CHECK_EQ(Bits(guard.m_FineOffsetY_7f0), Bits(33.0f));
        OBL_CHECK(env.thoughts.empty()); // offence state 0
    }
}

OBL_TEST_CASE("security_fns.choose_action_tolerance_two_searches_again_with_a_smaller_reach_and_keeps_the_answer")
{
    object::Guest guest{};
    MakeGuest(guest, 30, 0);
    Env env;
    env.byId = {&guest};
    object::SecurityGuard guard{};
    Guard(guard);
    guard.m_offenderId = 30;
    guard.m_toleranceClass = 2;
    guard.m_severity = 2;
    OBL_CHECK(ChooseAction(guard, env));
    OBL_REQUIRE(env.walkQueries.size() == 2);
    OBL_CHECK_EQ(env.walkQueries[1][3], 13.0f);
    OBL_CHECK_EQ(env.walkQueries[1][4], 8.0f);
    OBL_CHECK_EQ(env.walkQueries[1][0], 100.0f); // searched again from the guest's own position
    // The second search failing still stores its (unchanged) point: simulate by a first success, then a failing env of the same shape.
    struct TwoStep : Env {
        int calls = 0;
        bool FindWalkablePointNear(float pos[3], int dx, int dy)
        {
            return ++calls == 1 ? Env::FindWalkablePointNear(pos, dx, dy) : (Env::walkQueries.push_back({pos[0], pos[1], pos[2]}), false);
        }
    } two;
    two.byId = {&guest};
    object::SecurityGuard other{};
    Guard(other);
    other.m_offenderId = 30;
    other.m_toleranceClass = 2;
    other.m_severity = 2;
    OBL_CHECK(ChooseAction(other, two));
    OBL_CHECK_EQ(Bits(other.m_FineOffsetX), Bits(100.0f));
    OBL_CHECK_EQ(other.m_action, 2);
}

OBL_TEST_CASE("security_fns.choose_action_stops_early_for_an_unapproachable_or_unreachable_offender")
{
    object::Guest guest{};
    MakeGuest(guest, 30, 2);
    {
        Env env;
        env.byId = {&guest};
        env.approachable = false;
        object::SecurityGuard guard{};
        Guard(guard);
        guard.m_offenderId = 30;
        guard.m_action = 2;
        OBL_CHECK(ChooseAction(guard, env));
        OBL_CHECK_EQ(guard.m_action, 0);
        OBL_CHECK(env.thoughts.empty());
        OBL_CHECK(env.walkQueries.empty());
    }
    {
        Env env;
        env.byId = {&guest};
        env.walkable = false;
        object::SecurityGuard guard{};
        Guard(guard);
        guard.m_offenderId = 30;
        guard.m_action = 2;
        OBL_CHECK(ChooseAction(guard, env));
        OBL_CHECK_EQ(guard.m_action, 0);
        OBL_CHECK(env.thoughts.empty());
    }
    Env missing;
    object::SecurityGuard guard{};
    Guard(guard);
    OBL_CHECK(!ChooseAction(guard, missing));
    OBL_CHECK_EQ(guard.m_action, 0);
}

OBL_TEST_CASE("security_fns.choose_action_thoughts_and_counters_by_offence_state")
{
    {
        object::Guest guest{}; // litter: guests against bins
        MakeGuest(guest, 30, 1);
        Env env;
        env.byId = {&guest};
        env.guestCount = 31;
        env.binCount = 2; // 30 < 31
        object::SecurityGuard guard{};
        Guard(guard);
        guard.m_offenderId = 30;
        guard.m_toleranceClass = 0;
        guard.m_severity = 1;
        guard.m_RequiredFeedback = 5;
        guard.m_FeedbackStatus = 0xff;
        OBL_CHECK(ChooseAction(guard, env));
        OBL_REQUIRE(env.thoughts.size() == 1);
        OBL_CHECK_EQ(env.thoughts[0], uint32_t{0x40});
        OBL_CHECK_EQ(guard.m_RequiredFeedback, 0);
        OBL_CHECK_EQ(guard.m_FeedbackStatus, 0xfd);
        env.guestCount = 30; // exactly fifteen per bin: nothing cleared
        guard.m_RequiredFeedback = 5;
        guard.m_FeedbackStatus = 0xff;
        env.thoughts.clear();
        OBL_CHECK(ChooseAction(guard, env));
        OBL_CHECK_EQ(guard.m_RequiredFeedback, 5);
        OBL_CHECK_EQ(guard.m_FeedbackStatus, 0xff);
    }
    {
        object::Guest guest{}; // fight
        MakeGuest(guest, 30, 2);
        Env env;
        env.byId = {&guest};
        object::SecurityGuard guard{};
        Guard(guard);
        guard.m_offenderId = 30;
        guard.m_toleranceClass = 2;
        guard.m_severity = 2;
        guard.m_numFights = 5;
        guard.m_FeedbackStatus = 0xff;
        guard.m_RequiredFeedback = 5;
        OBL_CHECK(ChooseAction(guard, env));
        OBL_CHECK_EQ(guard.m_numFights, 6);
        OBL_CHECK_EQ(guard.m_FeedbackStatus, 0xf7);
        OBL_CHECK_EQ(guard.m_RequiredFeedback, 0);
        OBL_REQUIRE(env.thoughts.size() == 1);
        OBL_CHECK_EQ(env.thoughts[0], uint32_t{0x800}); // action 2
    }
    {
        object::Guest guest{}; // vandal
        MakeGuest(guest, 30, 3);
        Env env;
        env.byId = {&guest};
        object::SecurityGuard guard{};
        Guard(guard);
        guard.m_offenderId = 30;
        guard.m_toleranceClass = 1;
        guard.m_severity = 1;
        guard.m_numVandals = 2;
        guard.m_FeedbackStatus = 0xff;
        OBL_CHECK(ChooseAction(guard, env));
        OBL_CHECK_EQ(guard.m_numVandals, 3);
        OBL_CHECK_EQ(guard.m_FeedbackStatus, 0xff);
        OBL_REQUIRE(env.thoughts.size() == 1);
        OBL_CHECK_EQ(env.thoughts[0], uint32_t{0x80});
        guard.m_action = 0;
        guard.m_toleranceClass = 2;
        guard.m_severity = 2;
        guard.m_numVandals = 5;
        env.thoughts.clear();
        OBL_CHECK(ChooseAction(guard, env));
        OBL_CHECK_EQ(guard.m_FeedbackStatus, 0xfb);
        OBL_CHECK_EQ(env.thoughts[0], uint32_t{0x400});
    }
}
