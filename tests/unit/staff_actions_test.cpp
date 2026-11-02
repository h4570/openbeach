#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "unit_test.h"
#include "game/objects/staff_actions_logic.h"

#include <bit>
#include <cstring>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::staff::actions;
namespace {
struct Env {
    std::vector<std::string> calls;
    std::vector<obl::messages::Message> posted, sent;
    object::Guest guest{};
    int type = 0, animationType = 7;
    float duration = 1.5f, delay = 0;
    const char* messageName = "message-animation";
    void SetState(int state) { calls.push_back("state:" + std::to_string(state)); }
    void EnterState(int state) { calls.push_back("enter:" + std::to_string(state)); }
    int RepairType(int id)
    {
        calls.push_back("type:" + std::to_string(id));
        return type;
    }
    void ReturnRepair(int id, int kind) { calls.push_back("return:" + std::to_string(id) + ":" + std::to_string(kind)); }
    object::Base* Object(int id)
    {
        calls.push_back("object:" + std::to_string(id));
        return &guest;
    }
    void Post(const obl::messages::Message& message)
    {
        posted.push_back(message);
        calls.push_back("post");
    }
    void PostDelayed(const obl::messages::Message& message, float time)
    {
        posted.push_back(message);
        delay = time;
        calls.push_back("delayed");
    }
    void Send(const obl::messages::Message& message)
    {
        sent.push_back(message);
        calls.push_back("send");
    }
    void AnimType(int mode)
    {
        animationType = mode;
        calls.push_back("mode:" + std::to_string(mode));
    }
    float SetAnimation(const char* name)
    {
        calls.push_back(std::string("animation:") + name);
        return duration;
    }
    const char* MessageAnimation() { return messageName; }
    void ScriptSend(script::ScriptProgram*, obl::messages::Message message)
    {
        sent.push_back(message);
        calls.push_back("script");
    }
    void ScriptSendDelayed(script::ScriptProgram*, obl::messages::Message message, float time)
    {
        sent.push_back(message);
        delay = time;
        calls.push_back("script-delayed");
    }
};
} // namespace

OBL_TEST_CASE("Staff leave: mechanic returns only eligible jobs before leaving")
{
    for (int state = -1; state < 0x90; ++state)
        for (bool found : {false, true})
            for (bool refusal : {false, true})
                for (int type = -1; type < 5; ++type) {
                    object::Mechanic staffer{};
                    staffer.m_state = state;
                    staffer.m_cantDoJob = refusal;
                    staffer.m_DestObjectID = 101;
                    Env env;
                    env.type = type;
                    ReturnJobAndLeave(staffer, found, env);
                    const bool asks = state == 0x58 || state == 0x5d || (found && (state == 0x5b || (state == 0x83 && !refusal)));
                    const bool returns = asks && type >= 0 && type <= 3;
                    OBL_CHECK_EQ(env.calls.size(), size_t(2 + asks + returns));
                    OBL_CHECK_EQ(env.calls[env.calls.size() - 2], "state:76");
                    OBL_CHECK_EQ(env.calls.back(), "enter:76");
                    if (returns) OBL_CHECK_EQ(env.calls[1], "return:101:" + std::to_string(type));
                }
}

OBL_TEST_CASE("Staff leave: representative releases guests and retains caller's id")
{
    for (int state : {8, 9, 0x83, 0x4c, -1})
        for (bool sent : {false, true})
            for (bool guest : {false, true}) {
                object::Rep staffer{};
                staffer.m_state = state;
                staffer.m_id = 77;
                uint32_t id = 101;
                Env env;
                env.guest.m_type = guest ? object::ObjectType::Guest : object::ObjectType::Building;
                ReleaseGuestAndLeave(staffer, &id, sent, env);
                OBL_CHECK_EQ(id, 101u);
                const bool asks = state == 9 || (sent && (state == 8 || state == 0x83));
                OBL_CHECK_EQ(env.posted.size(), size_t(asks && guest));
                if (!env.posted.empty()) {
                    OBL_CHECK_EQ(env.posted[0].sender, 77);
                    OBL_CHECK_EQ(env.posted[0].receiver, 101);
                    OBL_CHECK_EQ(env.posted[0].data, 12);
                    OBL_CHECK_EQ(env.posted[0].flags, 0);
                }
                OBL_CHECK_EQ(env.calls.back(), "enter:76");
            }
}

OBL_TEST_CASE("Staff leave: lifeguard interrupts an early rescue and finishes a later one")
{
    for (bool saving : {false, true})
        for (int action : {-1, 0, 1, 2, 3}) {
            object::Lifeguard staffer{};
            staffer.m_saving = saving;
            staffer.m_ActionState = action;
            staffer.m_saving_id = 101;
            staffer.m_id = 77;
            Env env;
            env.guest.m_ActionState = 8;
            LifeguardLeave(staffer, env);
            if (!saving) {
                OBL_CHECK_EQ(env.calls.size(), 2u);
                OBL_CHECK_EQ(env.calls.back(), "enter:76");
            } else if (action == 0 || action == 1) {
                OBL_CHECK_EQ(env.guest.m_ActionState, 2);
                OBL_REQUIRE(env.posted.size() == 1);
                OBL_CHECK_EQ(env.posted[0].receiver, 77);
                OBL_CHECK_EQ(env.posted[0].data, 76);
            } else {
                OBL_CHECK_EQ(staffer.m_RescueThenDeath, 1);
                OBL_CHECK(env.calls.empty());
            }
        }
}

OBL_TEST_CASE("Staff animations: builder preserves sprite mode; cleaner sets it after animation")
{
    object::Builder builder{};
    builder.m_id = 77;
    Env env;
    env.duration = -0.0f;
    OBL_CHECK_EQ(PlayAnimation<Profession::Builder>(builder, "walk", true, 42, false, "stand", env), 1.0f);
    OBL_CHECK_EQ(env.animationType, 7);
    OBL_CHECK_EQ(env.calls.front(), "animation:walk");
    OBL_REQUIRE(env.posted.size() == 1);
    OBL_CHECK_EQ(env.posted[0].flags, 1);
    OBL_CHECK_EQ(env.delay, 1.0f);
    for (bool reverse : {false, true})
        for (bool loop : {false, true}) {
            object::Cleaner cleaner{};
            Env clean;
            PlayAnimation<Profession::Cleaner>(cleaner, "walk", reverse, 42, loop, nullptr, clean);
            OBL_CHECK_EQ(clean.calls[0], "animation:walk");
            OBL_CHECK_EQ(clean.animationType, reverse ? 1 : (loop ? 0 : 2));
            OBL_CHECK_EQ(clean.posted.size(), size_t(!loop));
        }
}

OBL_TEST_CASE("Staff animations: representative finish and exit state issue transition before animation")
{
    object::Rep staffer{};
    staffer.m_id = 77;
    staffer.m_state = 9;
    Env env;
    PlayAnimation<Profession::Rep>(staffer, "MessData", false, 12, false, "stand", env);
    OBL_CHECK_EQ(std::string(staffer.m_CurrAnimation), "message-animation");
    OBL_CHECK_EQ(std::string(staffer.m_ReturnAnimation), "stand");
    OBL_CHECK_EQ(staffer.m_LeaveState, 12);
    OBL_REQUIRE(env.posted.size() == 2);
    OBL_CHECK_EQ(env.posted[0].data, 10);
    OBL_CHECK_EQ(env.posted[1].data, 12);
    OBL_CHECK_EQ(env.calls[0], "mode:2");
    OBL_CHECK_EQ(env.calls[1], "post");
    OBL_CHECK_EQ(env.calls[2], "animation:message-animation");
}

OBL_TEST_CASE("Staff animations: lifeguard uses script send except during state 21; loop preserves return state")
{
    for (int state : {21, 85}) {
        object::Lifeguard staffer{};
        staffer.m_id = 77;
        staffer.m_state = state;
        std::strcpy(staffer.m_ReturnAnimation, "walk");
        Env env;
        PlayAnimation<Profession::Lifeguard>(staffer, "ReturnAnimation", false, -1, false, "stand", env);
        OBL_CHECK_EQ(std::string(staffer.m_CurrAnimation), "stand");
        OBL_CHECK(env.sent.empty());
        Env second;
        PlayAnimation<Profession::Lifeguard>(staffer, "walk", false, 12, false, "stand", second);
        OBL_CHECK_EQ(second.sent.size(), size_t(state != 21));
        if (!second.sent.empty()) OBL_CHECK_EQ(second.sent[0].data, 85);
        Env loop;
        PlayAnimation<Profession::Lifeguard>(staffer, "walk", true, 99, true, nullptr, loop);
        OBL_CHECK_EQ(staffer.m_LeaveState, 12);
        OBL_CHECK(loop.posted.empty());
        OBL_CHECK(loop.sent.empty());
        OBL_CHECK_EQ(loop.animationType, 1);
    }
}

OBL_TEST_CASE("Staff script sends: forward all message words by value, suppress null script")
{
    for (bool present : {false, true}) {
        object::Staff staffer{};
        staffer.m_definition = present ? reinterpret_cast<decltype(staffer.m_definition)>(0x1234) : nullptr;
        obl::messages::Message message{};
        uint32_t words[8] = {77, 101, 3, 1, 0x12345678, 0xffffffff, 0x7fc00000, 0x80000000};
        std::memcpy(&message, words, sizeof(message));
        Env env;
        SendMessage(staffer, message, env);
        SendMessageDelayed(staffer, message, -1.25f, env);
        OBL_CHECK_EQ(env.sent.size(), present ? 2u : 0u);
        for (const auto& sent : env.sent) OBL_CHECK(std::memcmp(&sent, &message, sizeof(message)) == 0);
        if (present) OBL_CHECK_EQ(env.delay, -1.25f);
    }
}
