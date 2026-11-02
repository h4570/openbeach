#include "unit_test.h"
#include "game/objects/vehicle_helpers.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"

#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <limits>

namespace {
using obl::object::Vehicle;
using obl::ui::SpriteInstance;
namespace helpers = obl::vehicle::helpers;
namespace life = obl::vehicle::lifecycle;

struct Env {
    obl::map::GridCell cells[8]{};
    std::vector<std::string> calls;
    std::vector<obl::messages::Message> sent;
    float target[3] = {10.75f, -20.75f, 0};
    Vehicle* live = nullptr;
    bool flagAtPost = false;
    int neighbours = 0;
    float WorldTime()
    {
        calls.push_back("time");
        return 100;
    }
    void NeighbourCells(uint32_t x, uint32_t y, obl::map::GridCell* out[8])
    {
        OBL_CHECK(x == 12);
        OBL_CHECK(y == 34);
        ++neighbours;
        for (int i = 0; i < 8; ++i) out[i] = &cells[i];
    }
    int32_t Direction(void* sprite) { return static_cast<SpriteInstance*>(sprite)->m_direction; }
    void SetMoving(Vehicle& vehicle, bool moving)
    {
        calls.push_back("moving");
        vehicle.m_moving = moving;
    }
    void PlayRideAnimation(Vehicle&, float time)
    {
        OBL_CHECK(time == 100);
        calls.push_back("ride");
    }
    void PlayTurnAnimation(Vehicle& vehicle)
    {
        calls.push_back("turn");
        OBL_CHECK(vehicle.m_turnAnimStartTime == 100);
    }
    float StartAnim(void* sprite, const char* name, float start, const float* world)
    {
        OBL_CHECK(world == live->m_WorldXYZ);
        calls.push_back("anim " + std::to_string(static_cast<SpriteInstance*>(sprite)->m_paletteIndex) + " " + (name ? name : "NULL") +
                " " + std::to_string(static_cast<int>(start)));
        return 2.5f;
    }
    void SetAnimType(void* sprite, int32_t type)
    {
        auto* instance = static_cast<SpriteInstance*>(sprite);
        calls.push_back("type " + std::to_string(instance->m_paletteIndex) + " " + std::to_string(type));
        instance->m_animType = type;
    }
    void TeleportGuest(int32_t guest, const float* world)
    {
        OBL_CHECK(world == live->m_WorldXYZ);
        calls.push_back("teleport " + std::to_string(guest));
    }
    float StartGuestAnim(int32_t guest, const char* name, float time, const float* world)
    {
        OBL_CHECK(world == live->m_WorldXYZ);
        OBL_CHECK(time == 100);
        calls.push_back("guest " + std::to_string(guest) + " " + (name ? name : "NULL"));
        return 99;
    }
    void SetGuestAnimType(int32_t guest, int32_t type)
    {
        calls.push_back("guest-type " + std::to_string(guest) + " " + std::to_string(type));
    }
    void TileToWorldPoint(uint32_t x, uint32_t y, float out[3])
    {
        OBL_CHECK(x == 12);
        OBL_CHECK(y == 34);
        std::copy(std::begin(target), std::end(target), out);
    }
    void Post(const obl::messages::Message& message)
    {
        flagAtPost = live->m_moveToTurnPoint != 0;
        sent.push_back(message);
        live->m_moveToTurnPoint = 0; // emulate an immediate receiver changing the flag
    }
};
} // namespace

OBL_TEST_CASE("Vehicle turn step: clock boundary, direction wrapping, continuation and ride message")
{
    struct StepEnv {
        std::vector<std::string> calls;
        obl::messages::Message message{};
        float time = 0;
        int count = 8;
        int32_t Direction(void* sprite) { return static_cast<SpriteInstance*>(sprite)->m_direction; }
        void SetDirection(void* sprite, int32_t direction)
        {
            calls.push_back("direction");
            static_cast<SpriteInstance*>(sprite)->m_direction = direction >= count ? count - 1 : direction;
        }
        void PlayTurnAnimation(Vehicle& vehicle)
        {
            calls.push_back("turn");
            time = vehicle.m_turnAnimStartTime;
            --vehicle.m_numTurns;
        }
        void SetMoving(Vehicle& vehicle, bool moving)
        {
            calls.push_back("moving");
            vehicle.m_moving = moving;
        }
        void Post(const obl::messages::Message& value)
        {
            calls.push_back("post");
            message = value;
        }
        void PlayRideAnimation(Vehicle& vehicle, float now)
        {
            calls.push_back("ride");
            time = now;
            vehicle.m_turnAnim = 0;
        }
    };
    for (int cw : {0, 1})
        for (int direction : {-2, 0, 1, 7, 8})
            for (int turns : {-1, 0, 1, 3})
                for (uint32_t state : {0x42u, 0x45u, 0x46u})
                    for (float start : {-1.0f, 0.0f, 75.0f})
                        for (float elapsed : {1.0f, 2.0f, 3.0f}) {
                            StepEnv env;
                            SpriteInstance sprite{};
                            sprite.m_direction = direction;
                            Vehicle vehicle{};
                            vehicle.m_sprite = &sprite;
                            vehicle.m_turnAnim = 1;
                            vehicle.m_turnCW = static_cast<uint8_t>(cw);
                            vehicle.m_numTurns = turns;
                            vehicle.m_state = state;
                            vehicle.m_id = 123;
                            vehicle.m_turnAnimStartTime = start;
                            vehicle.m_turnAnimLength = 2;
                            vehicle.m_CurrentRoamPoint[0] = 123.75f;
                            vehicle.m_CurrentRoamPoint[1] = -456.75f;
                            helpers::UpdateTurnAnimation(vehicle, start + elapsed, env);
                            if (elapsed < 2) {
                                OBL_CHECK(env.calls.empty());
                                OBL_CHECK_EQ(sprite.m_direction, direction);
                                continue;
                            }
                            int expected = start <= 0 ? direction : cw ? (direction + 1) % 8 : direction == 0 ? 7 : direction - 1;
                            // By value: CHECK_EQ binds a reference, and std::min would return one to the temporary 7.
                            const int clamped = std::min(expected, 7);
                            OBL_CHECK_EQ(sprite.m_direction, clamped);
                            OBL_CHECK_EQ(env.time, start + elapsed);
                            if (turns > 0) {
                                OBL_CHECK(env.calls == std::vector<std::string>({"direction", "turn"}));
                                OBL_CHECK_EQ(vehicle.m_numTurns, turns - 1);
                                OBL_CHECK_EQ(vehicle.m_turnAnimStartTime, start + elapsed);
                                OBL_CHECK(vehicle.m_turnAnim == 1);
                            } else {
                                OBL_CHECK(env.calls ==
                                        (state == 0x45 ? std::vector<std::string>({"direction", "moving", "post", "ride"})
                                                       : std::vector<std::string>({"direction", "moving", "ride"})));
                                OBL_CHECK(vehicle.m_moving == 1);
                                OBL_CHECK(vehicle.m_turnAnim == 0);
                                if (state == 0x45) {
                                    OBL_CHECK_EQ(env.message.sender, 123);
                                    OBL_CHECK_EQ(env.message.receiver, 123);
                                    OBL_CHECK_EQ(env.message.type, obl::messages::MT_MOVE2SEAPOINT);
                                    OBL_CHECK_EQ(env.message.flags, 123);
                                    OBL_CHECK_EQ(env.message.data, -456);
                                }
                            }
                        }
    SpriteInstance sprite{};
    Vehicle vehicle{};
    vehicle.m_sprite = &sprite;
    vehicle.m_turnAnimStartTime = 16777216.0f;
    vehicle.m_turnAnimLength = 1;
    StepEnv env;
    helpers::UpdateTurnAnimation(vehicle, 16777216.0f, env);
    OBL_CHECK(env.calls.empty()); // A float-rounded sum would finish one second too early.
    helpers::UpdateTurnAnimation(vehicle, std::numeric_limits<float>::quiet_NaN(), env);
    OBL_CHECK(!env.calls.empty());
}

OBL_TEST_CASE("Vehicle lanes: golden cell masks, untouched flags, invalid directions and aliasing")
{
    constexpr uint32_t depart[] = {0, 0xf0, 0, 0xc3, 0, 0x0f, 0, 0x3c};
    constexpr uint32_t returning[] = {0, 0xe1, 0, 0x87, 0, 0x1e, 0, 0x78};
    for (bool back : {false, true})
        for (bool block : {false, true})
            for (int dir = -1; dir <= 8; ++dir) {
                Env env;
                for (int i = 0; i < 8; ++i) env.cells[i].m_flags = 0xa5000000u | (block ? 0u : 0x10u) | static_cast<uint32_t>(i);
                helpers::Lane(12, 34, dir, back, block, env);
                OBL_CHECK(env.neighbours == 1); // even invalid directions request the neighbours
                const uint32_t mask = dir >= 0 && dir < 8 ? (back ? returning[dir] : depart[dir]) : 0;
                for (int i = 0; i < 8; ++i) {
                    const bool affected = (mask & (1u << i)) != 0;
                    const bool blocked = affected ? block : !block;
                    OBL_CHECK(env.cells[i].m_flags == (0xa5000000u | static_cast<uint32_t>(i) | (blocked ? 0x10u : 0u)));
                }
            }
    struct AliasedEnv {
        obl::map::GridCell cell{};
        void NeighbourCells(uint32_t, uint32_t, obl::map::GridCell* out[8])
        {
            for (int i = 0; i < 8; ++i) out[i] = &cell;
        }
    } alias;
    alias.cell.m_flags = 0x123;
    helpers::Lane(0, 0, 1, false, true, alias);
    OBL_CHECK(alias.cell.m_flags == 0x133);
    helpers::Lane(0, 0, 1, false, false, alias);
    OBL_CHECK(alias.cell.m_flags == 0x123);
}

OBL_TEST_CASE("Vehicle turns: every direction pair, half-circle ties and unchanged fields on an aligned turn")
{
    for (int current = 0; current < 8; ++current)
        for (int target = 0; target < 8; ++target) {
            Env env;
            SpriteInstance sprite{};
            sprite.m_direction = current;
            Vehicle vehicle{};
            vehicle.m_sprite = &sprite;
            vehicle.m_moving = 1;
            vehicle.m_turnCW = 0x55;
            vehicle.m_turnAnimLength = 9;
            vehicle.m_turnAnimStartTime = 8;
            helpers::TurnToDirection(vehicle, target, env);
            OBL_CHECK(vehicle.m_turnAnim == 1);
            OBL_CHECK(vehicle.m_turnAnimLength == 9);
            if (current == target) {
                OBL_CHECK(vehicle.m_moving == 1);
                OBL_CHECK(vehicle.m_turnCW == 0x55);
                OBL_CHECK(vehicle.m_numTurns == 0);
                OBL_CHECK(vehicle.m_turnAnimStartTime == 8);
                OBL_CHECK((env.calls == std::vector<std::string>{"time", "ride"}));
            } else {
                const int cwSteps = (target - current + 8) % 8;
                OBL_CHECK(vehicle.m_moving == 0);
                OBL_CHECK(vehicle.m_numTurns == static_cast<uint32_t>(std::min(cwSteps, 8 - cwSteps)));
                OBL_CHECK((vehicle.m_turnCW != 0) == (cwSteps == 4 ? current < target : cwSteps < 4));
                OBL_CHECK((env.calls == std::vector<std::string>{"moving", "time", "turn"}));
            }
        }
}

OBL_TEST_CASE("Vehicle turn animations: names, effect order, passengers, countdown and stored length")
{
    constexpr const char* body[4][2] = {{"d_jetski_turn_l", "d_jetski_turn_r"}, {"boat_turn_left", "boat_turn_right"}, {"turn_l", "turn_r"},
            {"boat_turn", "boat_turn"}};
    constexpr const char* driver[4][2] = {{"d_jetski_turn_l", "d_jetski_turn_r"}, {"boat_driver_turn_left", "boat_driver_turn_right"},
            {"d_ped_turn_l_pass", "d_ped_turn_r_pass"}, {"fish_man1_turn", "fish_man1_turn"}};
    constexpr const char* passenger[4][2] = {{"NULL", "NULL"}, {"boat_pass_turn_left", "boat_pass_turn_right"},
            {"d_ped_turn_l", "d_ped_turn_r"}, {"fish_man2_turn", "fish_man2_turn"}};
    constexpr int kTypes[] = {life::Jetski, life::SpeedBoat, life::Pedaloe, life::FishBoat};
    for (int t = 0; t < 4; ++t)
        for (int cw = 0; cw < 2; ++cw)
            for (int cap = 0; cap <= 3; ++cap) {
                Env env;
                SpriteInstance sprites[5]{};
                for (int i = 0; i < 5; ++i) sprites[i].m_paletteIndex = i;
                Vehicle vehicle{};
                env.live = &vehicle;
                vehicle.m_sprite = &sprites[0];
                vehicle.m_vehicleSprite = &sprites[1];
                vehicle.m_auxSprite1 = &sprites[2];
                vehicle.m_auxSprite2 = &sprites[3];
                vehicle.m_auxSprite3 = &sprites[4];
                vehicle.m_vehicleType = kTypes[t];
                vehicle.m_turnCW = static_cast<uint8_t>(cw);
                vehicle.m_turnAnimStartTime = 75;
                vehicle.m_numTurns = 0;
                vehicle.m_currCap = cap;
                vehicle.m_Guests[0] = 50;
                vehicle.m_Guests[1] = 51;
                vehicle.m_Guests[2] = 52;
                helpers::PlayTurnAnimation(vehicle, env);
                OBL_CHECK(vehicle.m_turnAnimStartTime == 100);
                OBL_CHECK(vehicle.m_turnAnimLength == 2.5f);
                OBL_CHECK(vehicle.m_numTurns == 0xffffffffu);
                std::vector<std::string> expected;
                if (t == 0) expected.push_back(std::string("anim 1 d_wake_turn_") + (cw ? "r" : "l") + " 75");
                if (t == 3) expected = {"anim 2 rod01_turn 75", "anim 3 rod02_turn 75", "anim 4 fish_driver_turn 75"};
                expected.push_back("time");
                expected.push_back(std::string("anim 0 ") + body[t][cw] + " 100");
                expected.push_back("type 0 2");
                const bool countdown = t == 3 && cw;
                if (countdown)
                    for (int i : {0, 2, 3, 4}) expected.push_back("type " + std::to_string(i) + " 1");
                if (cap != 0) {
                    expected.push_back("teleport 50");
                    expected.push_back(std::string("guest 50 ") + driver[t][cw]);
                    expected.push_back(std::string("guest-type 50 ") + (countdown ? "1" : "2"));
                    if (cap > 1) {
                        expected.push_back("teleport 51");
                        expected.push_back(std::string("guest 51 ") + passenger[t][cw]);
                        expected.push_back(std::string("guest-type 51 ") + (countdown ? "1" : "2"));
                    }
                }
                OBL_CHECK(env.calls == expected);
            }
}

OBL_TEST_CASE("Vehicle turn area: zero distance, threshold, truncation and flag after immediate delivery")
{
    for (float distance : {0.0f, 199.9f, 200.0f, 200.1f, 1000.0f}) {
        Env env;
        Vehicle vehicle{};
        env.live = &vehicle;
        vehicle.m_id = 7;
        vehicle.m_TurnArea[0] = 12;
        vehicle.m_TurnArea[1] = 34;
        vehicle.m_WorldXYZ[0] = env.target[0] + distance;
        vehicle.m_WorldXYZ[1] = env.target[1];
        helpers::MoveToTurnArea(vehicle, env);
        OBL_REQUIRE(env.sent.size() == 1);
        const auto& message = env.sent[0];
        OBL_CHECK(message.sender == 7);
        OBL_CHECK(message.receiver == 7);
        OBL_CHECK(message.type == obl::messages::MT_MOVE2SEAPOINT);
        OBL_CHECK(message.flags == static_cast<int>(distance < 200 ? env.target[0] : vehicle.m_WorldXYZ[0]));
        OBL_CHECK(message.data == -20);
        OBL_CHECK(message.extra == 0);
        OBL_CHECK(message.floatData == 0);
        OBL_CHECK(message.time == 0);
        OBL_CHECK(!env.flagAtPost);
        OBL_CHECK(vehicle.m_moveToTurnPoint == 1);
    }
}
