#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "unit_test.h"

#include "game/objects/staff_direct_logic.h"

#include <bit>
#include <cstring>
#include <vector>

// The orders of the player to staff (src/game/objects/staff_direct_logic.h) against a fake Env.

using namespace obl;
using namespace obl::staff::direct;

namespace {

struct Env {
    std::vector<obl::messages::Message> posted, sent;
    std::vector<object::Base*> objects;
    bool approachable = true;
    bool walkable = true;
    float walkTo[3] = {11.0f, 22.0f, 33.0f};
    float secondWalkTo[3] = {44.0f, 55.0f, 66.0f};
    std::vector<std::vector<float>> walkQueries;
    int32_t builderAnswer = -1, cleanerAnswer = -1, mechanicAnswer = -1;
    int32_t mechanicType = 5;
    std::vector<int32_t> asked;

    void Post(const obl::messages::Message& message) { posted.push_back(message); }
    void Send(const obl::messages::Message& message) { sent.push_back(message); }
    object::Base* Object(int32_t id)
    {
        for (object::Base* object : objects)
            if (static_cast<int32_t>(object->m_id) == id) return object;
        return nullptr;
    }
    bool IsApproachable(object::Base*) { return approachable; }
    bool FindWalkablePointNear(float pos[3], int dx, int dy)
    {
        walkQueries.push_back({pos[0], pos[1], pos[2], static_cast<float>(dx), static_cast<float>(dy)});
        const float* to = walkQueries.size() == 2 ? secondWalkTo : walkTo;
        if (walkable) std::memcpy(pos, to, 3 * sizeof(float));
        return walkable;
    }
    int32_t AssignBuilder(int32_t builderId, uint32_t jobId)
    {
        asked = {builderId, static_cast<int32_t>(jobId)};
        return builderAnswer;
    }
    int32_t ClaimForCleaner(uint32_t objectId, int32_t cleanerId)
    {
        asked = {static_cast<int32_t>(objectId), cleanerId};
        return cleanerAnswer;
    }
    int32_t ClaimForMechanic(uint32_t objectId, int32_t mechanicId, int32_t* type)
    {
        asked = {static_cast<int32_t>(objectId), mechanicId};
        *type = mechanicType;
        return mechanicAnswer;
    }
};

void MakeGuest(object::Guest& guest, uint32_t id, int32_t state, int32_t action)
{
    guest.m_id = id;
    guest.m_type = object::ObjectType::Guest;
    guest.m_state = state;
    guest.m_ActionState = action;
    guest.m_WorldXYZ[0] = 100.0f;
    guest.m_WorldXYZ[1] = 200.0f;
    guest.m_WorldXYZ[2] = 3.0f;
}

template<class Member> void Staff(Member& member, uint32_t id)
{
    member.m_id = id;
    member.m_cantDoJob = 7; // a marker: every path must write it
}

uint32_t Bits(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void ExpectMessage(const obl::messages::Message& message, int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data,
        int32_t extra = 0)
{
    OBL_CHECK_EQ(message.sender, sender);
    OBL_CHECK_EQ(message.receiver, receiver);
    OBL_CHECK_EQ(message.type, type);
    OBL_CHECK_EQ(message.flags, flags);
    OBL_CHECK_EQ(message.data, data);
    OBL_CHECK_EQ(message.extra, extra);
}

} // namespace

OBL_TEST_CASE("staff_direct.point_copies_the_raw_words_and_posts_to_itself")
{
    object::Staff staff{};
    Staff(staff, 9);
    const uint32_t nan = 0x7f800001u; // a signalling NaN survives a raw copy
    float point[3];
    std::memcpy(&point[0], &nan, sizeof(nan));
    point[1] = -2.5f;
    point[2] = 99.0f;
    Env env;
    DirectToPoint(staff, point, env);
    OBL_CHECK_EQ(staff.m_DirectStaff, 1);
    OBL_CHECK_EQ(Bits(staff.m_DirectPos[0]), nan);
    OBL_CHECK_EQ(staff.m_DirectPos[1], -2.5f);
    OBL_CHECK_EQ(staff.m_directedToObject, 0);
    OBL_REQUIRE(env.posted.size() == 1);
    ExpectMessage(env.posted[0], 9, 9, kMsgDirectToPoint, 0, 0);
}

OBL_TEST_CASE("staff_direct.target_sets_the_flags_and_the_goto_object")
{
    object::Staff staff{};
    Staff(staff, 9);
    Env env;
    DirectToTarget(staff, 1234, env);
    OBL_CHECK_EQ(staff.m_DirectStaff, 1);
    OBL_CHECK_EQ(staff.m_directedToObject, 1);
    OBL_CHECK_EQ(staff.m_GotoObject, 1234);
    OBL_REQUIRE(env.posted.size() == 1);
    ExpectMessage(env.posted[0], 9, 9, kMsgDirectToPoint, 0, 0);
}

OBL_TEST_CASE("staff_direct.builder_takes_the_job_through_its_own_send")
{
    for (int32_t answer : {0, 1, 3}) {
        object::Builder builder{};
        Staff(builder, 4);
        Env env;
        env.builderAnswer = answer;
        OBL_CHECK(BuilderDirectToObject(builder, 77, env));
        OBL_CHECK_EQ(env.asked[0], 4);
        OBL_CHECK_EQ(env.asked[1], 77);
        OBL_CHECK_EQ(builder.m_DestObjectID, 77);
        OBL_CHECK_EQ(builder.m_cantDoJob, 0);
        OBL_CHECK(env.posted.empty());
        OBL_REQUIRE(env.sent.size() == 1);
        ExpectMessage(env.sent[0], 4, 4, kMsgStateChange, 0, answer == 0 ? kBuilderJobTaken : kBuilderJobShared);
    }
}

OBL_TEST_CASE("staff_direct.builder_refusal_keeps_the_destination_and_clears_the_feedback")
{
    object::Builder builder{};
    Staff(builder, 4);
    builder.m_FeedbackStatus = 0xff;
    builder.m_RequiredFeedback = 6;
    Env env;
    OBL_CHECK(BuilderDirectToObject(builder, 77, env));
    OBL_CHECK_EQ(builder.m_DestObjectID, 77); // written before the answer is looked at
    OBL_CHECK_EQ(builder.m_cantDoJob, 1);
    OBL_CHECK_EQ(builder.m_FeedbackStatus, 0xfb);
    OBL_CHECK_EQ(builder.m_RequiredFeedback, 0);
    OBL_CHECK(env.sent.empty());
    OBL_REQUIRE(env.posted.size() == 1);
    ExpectMessage(env.posted[0], 4, 4, kMsgPlayAnimation, 0, 0, kReportMember14);
}

OBL_TEST_CASE("staff_direct.cleaner_keeps_the_kind_only_for_known_kinds")
{
    struct Row {
        int32_t answer;
        int32_t kindAfter;
    };
    for (Row row : {Row{0, 0}, Row{1, 1}, Row{2, 2}, Row{3, 9}}) {
        object::Cleaner cleaner{};
        Staff(cleaner, 5);
        cleaner.m_UrgentType = 9;
        Env env;
        env.cleanerAnswer = row.answer;
        OBL_CHECK(CleanerDirectToObject(cleaner, 31, env));
        OBL_CHECK_EQ(env.asked[0], 31);
        OBL_CHECK_EQ(env.asked[1], 5);
        OBL_CHECK_EQ(cleaner.m_DestObjectID, 31);
        OBL_CHECK_EQ(cleaner.m_UrgentType, row.kindAfter);
        OBL_CHECK_EQ(cleaner.m_cantDoJob, 0);
        OBL_REQUIRE(env.posted.size() == 1);
        ExpectMessage(env.posted[0], 5, 5, kMsgPlayAnimation, 0, 1, kReportMember14);
    }
}

OBL_TEST_CASE("staff_direct.cleaner_refusal_does_not_touch_the_destination")
{
    object::Cleaner cleaner{};
    Staff(cleaner, 5);
    cleaner.m_DestObjectID = 8;
    cleaner.m_UrgentType = 2;
    cleaner.m_FeedbackStatus = static_cast<int32_t>(0xffffffffu);
    cleaner.m_RequiredFeedback = 3;
    Env env;
    OBL_CHECK(CleanerDirectToObject(cleaner, 31, env));
    OBL_CHECK_EQ(cleaner.m_DestObjectID, 8);
    OBL_CHECK_EQ(cleaner.m_UrgentType, 2);
    OBL_CHECK_EQ(cleaner.m_cantDoJob, 1);
    OBL_CHECK_EQ(cleaner.m_RequiredFeedback, 0);
    OBL_CHECK_EQ(static_cast<uint32_t>(cleaner.m_FeedbackStatus), 0xffbfffffu);
    OBL_REQUIRE(env.posted.size() == 1);
    ExpectMessage(env.posted[0], 5, 5, kMsgPlayAnimation, 0, 0, kReportMember14);
}

OBL_TEST_CASE("staff_direct.mechanic_accepts_and_refuses")
{
    object::Mechanic mechanic{};
    Staff(mechanic, 6);
    Env env;
    env.mechanicAnswer = 0;
    OBL_CHECK(MechanicDirectToObject(mechanic, 52, env));
    OBL_CHECK_EQ(env.asked[0], 52);
    OBL_CHECK_EQ(env.asked[1], 6);
    OBL_CHECK_EQ(mechanic.m_repairType, 5); // written by the manager
    OBL_CHECK_EQ(mechanic.m_DestObjectID, 52);
    OBL_CHECK_EQ(mechanic.m_foundRepair, 1);
    OBL_CHECK_EQ(mechanic.m_cantDoJob, 0);
    OBL_REQUIRE(env.posted.size() == 1);
    ExpectMessage(env.posted[0], 6, 6, kMsgPlayAnimation, 0, 1, kReportMember14);

    object::Mechanic refused{};

    Staff(refused, 6);
    refused.m_DestObjectID = 3;
    refused.m_foundRepair = 1;
    refused.m_FeedbackStatus = static_cast<int32_t>(0xffffffffu);
    refused.m_RequiredFeedback = 4;
    Env no;
    OBL_CHECK(MechanicDirectToObject(refused, 52, no));
    OBL_CHECK_EQ(refused.m_DestObjectID, 3);
    OBL_CHECK_EQ(refused.m_foundRepair, 0);
    OBL_CHECK_EQ(refused.m_cantDoJob, 1);
    OBL_CHECK_EQ(refused.m_RequiredFeedback, 0);
    OBL_CHECK_EQ(static_cast<uint32_t>(refused.m_FeedbackStatus), 0xffbfffffu);
    OBL_REQUIRE(no.posted.size() == 1);
    ExpectMessage(no.posted[0], 6, 6, kMsgPlayAnimation, 0, 0, kReportMember14);
}

OBL_TEST_CASE("staff_direct.lifeguard_rescues_a_free_guest_in_the_right_state")
{
    object::Guest guest{};
    MakeGuest(guest, 40, kStateGuestNeedsRescue, 0);
    object::Lifeguard lifeguard{};
    Staff(lifeguard, 7);
    lifeguard.m_saving_id = 3;
    Env env;
    env.objects = {&guest};
    OBL_CHECK(LifeguardDirectToObject(lifeguard, 40, env));
    OBL_CHECK_EQ(lifeguard.m_saving_id, 40);
    OBL_CHECK_EQ(lifeguard.m_saving, 1);
    OBL_CHECK_EQ(lifeguard.m_cantDoJob, 0);
    OBL_REQUIRE(env.posted.size() == 1);
    ExpectMessage(env.posted[0], 7, 7, kMsgPlayAnimation, 0, 1, kReportMember14);
}

OBL_TEST_CASE("staff_direct.lifeguard_refusals_clear_their_own_bit")
{
    struct Row {
        bool object;
        int32_t state;
        int32_t action;
        uint32_t bit;
        int32_t savingId;
    };
    for (Row row : {Row{false, 0, 0, 8u, 3}, Row{true, kStateGuestNeedsRescue, 1, 0x10u, -1}, Row{true, 12, 0, 0x20u, 3}}) {
        object::Guest guest{};
        MakeGuest(guest, 40, row.state, row.action);
        object::Lifeguard lifeguard{};
        Staff(lifeguard, 7);
        lifeguard.m_saving_id = 3;
        lifeguard.m_saving = 1;
        lifeguard.m_FeedbackStatus = static_cast<int32_t>(0xffffffffu);
        lifeguard.m_RequiredFeedback = 9;
        Env env;
        if (row.object) env.objects = {&guest};
        OBL_CHECK(LifeguardDirectToObject(lifeguard, 40, env));
        OBL_CHECK_EQ(lifeguard.m_saving_id, row.savingId);
        OBL_CHECK_EQ(lifeguard.m_saving, row.state == kStateGuestNeedsRescue && row.object ? 0 : 1);
        OBL_CHECK_EQ(lifeguard.m_cantDoJob, 1);
        OBL_CHECK_EQ(lifeguard.m_RequiredFeedback, 0);
        OBL_CHECK_EQ(static_cast<uint32_t>(lifeguard.m_FeedbackStatus), ~row.bit);
        OBL_REQUIRE(env.posted.size() == 1);
        ExpectMessage(env.posted[0], 7, 7, kMsgPlayAnimation, 0, 0, kReportMember14);
    }
}

OBL_TEST_CASE("staff_direct.rep_walks_to_the_point_next_to_the_guest")
{
    object::Guest guest{};
    MakeGuest(guest, 40, 12, 0);
    object::Rep rep{};
    Staff(rep, 8);
    rep.m_talking_id = -1;
    Env env;
    env.objects = {&guest};
    OBL_CHECK(RepDirectToObject(rep, 40, env));
    OBL_REQUIRE(env.walkQueries.size() == 1);
    OBL_CHECK_EQ(env.walkQueries[0][0], 100.0f);
    OBL_CHECK_EQ(env.walkQueries[0][3], 20.0f);
    OBL_CHECK_EQ(env.walkQueries[0][4], 10.0f);
    OBL_CHECK_EQ(rep.m_talking_id, 40);
    OBL_CHECK_EQ(rep.m_sentToGuest, 1);
    OBL_CHECK_EQ(rep.m_GuestOffSetPosX, 11.0f);
    OBL_CHECK_EQ(rep.m_GuestOffSetPosY, 22.0f);
    OBL_CHECK_EQ(rep.m_GuestOffSetPosZ, 33.0f);
    OBL_CHECK_EQ(rep.m_cantDoJob, 0);
    OBL_REQUIRE(env.posted.size() == 2);
    ExpectMessage(env.posted[0], 8, 40, kMsgStateChange, 0, 0x1a);
    ExpectMessage(env.posted[1], 8, 8, kMsgPlayAnimation, 0, 1, kReportMember14);
}

OBL_TEST_CASE("staff_direct.rep_refuses_an_unreachable_guest_and_declines_a_missing_one")
{
    object::Guest guest{};
    MakeGuest(guest, 40, 5, 0);
    for (int variant = 0; variant < 2; ++variant) {
        object::Rep rep{};
        Staff(rep, 8);
        rep.m_talking_id = -1;
        rep.m_FeedbackStatus = static_cast<int32_t>(0xffffffffu);
        rep.m_RequiredFeedback = 2;
        Env env;
        env.objects = {&guest};
        (variant == 0 ? env.approachable : env.walkable) = false;
        OBL_CHECK(RepDirectToObject(rep, 40, env));
        OBL_CHECK_EQ(rep.m_talking_id, -1);
        OBL_CHECK_EQ(rep.m_sentToGuest, 0);
        OBL_CHECK_EQ(rep.m_cantDoJob, 1);
        OBL_CHECK_EQ(rep.m_RequiredFeedback, 0);
        OBL_CHECK_EQ(static_cast<uint32_t>(rep.m_FeedbackStatus), 0xffbfffffu);
        OBL_REQUIRE(env.posted.size() == 1);
        ExpectMessage(env.posted[0], 8, 8, kMsgPlayAnimation, 0, 0, kReportMember14);
    }
    object::Rep rep{};
    Staff(rep, 8);
    Env none;
    OBL_CHECK(!RepDirectToObject(rep, 41, none));
    OBL_CHECK(none.posted.empty());
    OBL_CHECK_EQ(rep.m_cantDoJob, 7);
}

OBL_TEST_CASE("staff_direct.security_acts_by_tolerance_class")
{
    struct Row {
        int32_t tolerance;
        int32_t fineLevel;
        int32_t data;
        bool sends;
    };
    for (Row row :
            {Row{0, 0, kSecurityArrest, true}, Row{1, 1, kSecurityArrest, true}, Row{2, 8, kSecurityFight, true}, Row{3, 8, 0, false}}) {
        object::Guest guest{};
        MakeGuest(guest, 40, 12, 0);
        object::SecurityGuard guard{};
        Staff(guard, 6);
        guard.m_toleranceClass = row.tolerance;
        guard.m_FineLevel = 8;
        guard.m_offenderId = -1;
        Env env;
        env.objects = {&guest};
        OBL_CHECK(SecurityDirectToObject(guard, 40, env));
        OBL_CHECK_EQ(guard.m_FineLevel, row.fineLevel);
        OBL_CHECK_EQ(guard.m_cantDoJob, 0);
        OBL_CHECK(env.posted.empty());
        OBL_CHECK_EQ(guard.m_offenderId, row.sends ? 40 : -1);
        // Tolerance 2 looks a second time (+-13, +-8) and keeps that point whatever it answers.
        OBL_CHECK_EQ(env.walkQueries.size(), row.tolerance == 2 ? size_t{2} : size_t{1});
        OBL_CHECK_EQ(guard.m_FineOffsetX, row.tolerance == 2 ? 44.0f : 11.0f);
        OBL_CHECK_EQ(guard.m_FineOffsetY, row.tolerance == 2 ? 55.0f : 22.0f);
        OBL_CHECK_EQ(guard.m_FineOffsetY_7f0, row.tolerance == 2 ? 66.0f : 33.0f);
        if (row.tolerance == 2) {
            OBL_CHECK_EQ(env.walkQueries[1][3], 13.0f);
            OBL_CHECK_EQ(env.walkQueries[1][4], 8.0f);
        }
        OBL_REQUIRE(env.sent.size() == (row.sends ? 1u : 0u));
        if (row.sends) ExpectMessage(env.sent[0], 6, 6, kMsgStateChange, 1, row.data);
    }
}

OBL_TEST_CASE("staff_direct.security_keeps_the_second_point_even_when_the_search_fails")
{
    object::Guest guest{};
    MakeGuest(guest, 40, 12, 0);
    object::SecurityGuard guard{};
    Staff(guard, 6);
    guard.m_toleranceClass = 2;
    struct FailsSecond : Env {
        bool FindWalkablePointNear(float pos[3], int dx, int dy)
        {
            Env::FindWalkablePointNear(pos, dx, dy);
            return walkQueries.size() == 1;
        }
    } env;
    env.objects = {&guest};
    OBL_CHECK(SecurityDirectToObject(guard, 40, env));
    OBL_CHECK_EQ(guard.m_FineOffsetX, 44.0f); // the fake wrote the point before answering no
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK_EQ(env.sent[0].data, kSecurityFight);
}

OBL_TEST_CASE("staff_direct.security_refuses_without_an_object_or_a_reachable_guest")
{
    object::Guest guest{};
    MakeGuest(guest, 40, 5, 0);
    for (int variant = 0; variant < 3; ++variant) {
        object::SecurityGuard guard{};
        Staff(guard, 6);
        guard.m_toleranceClass = 1;
        guard.m_offenderId = -1;
        guard.m_FeedbackStatus = static_cast<int32_t>(0xffffffffu);
        guard.m_RequiredFeedback = 5;
        Env env;
        if (variant != 0) env.objects = {&guest};
        if (variant == 1) env.approachable = false;
        if (variant == 2) env.walkable = false;
        OBL_CHECK(SecurityDirectToObject(guard, 40, env));
        OBL_CHECK_EQ(guard.m_offenderId, -1);
        OBL_CHECK_EQ(guard.m_cantDoJob, 1);
        OBL_CHECK_EQ(guard.m_RequiredFeedback, 0);
        OBL_CHECK_EQ(static_cast<uint32_t>(guard.m_FeedbackStatus), 0xffbfffffu);
        OBL_CHECK(env.sent.empty());
        OBL_REQUIRE(env.posted.size() == 1);
        ExpectMessage(env.posted[0], 6, 6, kMsgPlayAnimation, 0, 0, kReportMember14);
    }
}
