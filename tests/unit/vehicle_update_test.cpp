#include "unit_test.h"
#include "game/objects/vehicle_update.h"
#include "game/messages/message_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {
using obl::map::GridCell;
using obl::messages::Message;
using obl::object::Vehicle;
using obl::tables::VehicleDef;
namespace update = obl::vehicle::update;
namespace life = obl::vehicle::lifecycle;
namespace msg = obl::vehicle::messages;

std::string Join(const std::vector<std::string>& calls)
{
    std::string text;
    for (const std::string& call : calls) text += call + " | ";
    return text;
}

struct Env {
    std::vector<std::string> calls;
    float now = 100;
    float delta = 0.5f;
    bool baseResult = true;
    float doorLength = 2.5f;
    uint32_t tile[2] = {7, 9};
    GridCell cells[8]{};
    int spriteDirection = 5;
    // The sprites are told apart by their address inside this array.
    char sprites[4]{};

    static std::string Number(float value) { return std::to_string(value); }
    std::string Name(void* sprite) const
    {
        if (sprite == &sprites[0]) return "body";
        if (sprite == &sprites[1]) return "jetski";
        if (sprite == &sprites[2]) return "door";
        if (sprite == &sprites[3]) return "guest";
        return "other";
    }

    float WorldTime() { return now; }
    float DeltaTime() { return delta; }
    void NeighbourCells(uint32_t x, uint32_t y, GridCell* out[8])
    {
        calls.push_back("cells " + std::to_string(x) + "," + std::to_string(y));
        for (int i = 0; i < 8; ++i) out[i] = &cells[i];
    }
    bool BaseUpdate(Vehicle&)
    {
        calls.push_back("base");
        return baseResult;
    }
    void FatalError(const Vehicle&) { calls.push_back("fatal"); }
    void Post(const Message& m)
    {
        calls.push_back("post " + std::to_string(m.sender) + " " + std::to_string(m.receiver) + " " + std::to_string(m.type) + " " +
                std::to_string(m.flags) + " " + std::to_string(m.data));
    }
    void PostDelayed(const Message& m, float delay)
    {
        calls.push_back("post-delayed " + std::to_string(m.type) + " " + std::to_string(m.data) + " " + Number(delay));
    }
    void PlaySound(const char* name, const float position[3])
    {
        calls.push_back(std::string("sound ") + name + " " + Number(position[0]) + " " + Number(position[1]) + " " + Number(position[2]));
    }
    void SetAnimType(void* sprite, int32_t type) { calls.push_back("type " + Name(sprite) + " " + std::to_string(type)); }
    float StartAnim(void* sprite, const char* name, float start, const float*)
    {
        calls.push_back("anim " + Name(sprite) + " " + name + " " + Number(start));
        return doorLength;
    }
    void SetDirection(void* sprite, int32_t direction) { calls.push_back("direction " + Name(sprite) + " " + std::to_string(direction)); }
    int32_t Direction(void* sprite)
    {
        calls.push_back("read-direction " + Name(sprite));
        return spriteDirection;
    }
    void TeleportGuest(int32_t guest, const float position[3])
    {
        calls.push_back("teleport " + std::to_string(guest) + " " + Number(position[0]) + " " + Number(position[1]));
    }
    void* GuestSprite(int32_t guest)
    {
        calls.push_back("sprite " + std::to_string(guest));
        return &sprites[3];
    }
    void StepTurnAnimation(Vehicle&, float time) { calls.push_back("step-turn " + Number(time)); }
    void TeleportSelf(Vehicle&, const float position[3])
    {
        calls.push_back("teleport-self " + Number(position[0]) + " " + Number(position[1]));
    }
    void MapTile(const float world[3], uint32_t out[2])
    {
        calls.push_back("tile " + Number(world[0]) + " " + Number(world[1]));
        out[0] = tile[0];
        out[1] = tile[1];
    }
};

struct Boat {
    explicit Boat(int32_t type, int32_t state)
    {
        definition.m_MoveType = 1;
        object.m_definition = &definition;
        object.m_vehicleType = type;
        object.m_state = state;
        object.m_id = 17;
        object.m_sprite = &env.sprites[0];
        object.m_vehicleSprite = &env.sprites[1];
        object.m_doorSprite = &env.sprites[2];
        object.m_rideTime = 90;
        std::strcpy(object.m_Name, "x");
        object.m_WorldXYZ[0] = 5;
        object.m_WorldXYZ[1] = 6;
    }
    // The calls without the ones every Update makes at its start and end.
    std::vector<std::string> Middle()
    {
        std::vector<std::string> middle(env.calls.begin(), env.calls.end());
        const size_t base = std::find(middle.begin(), middle.end(), "base") - middle.begin();
        middle.erase(middle.begin(), middle.begin() + base + 1);
        while (!middle.empty() && middle.back().rfind("tile", 0) != 0) middle.pop_back();
        middle.pop_back();
        return middle;
    }
    Vehicle object{};
    VehicleDef definition{};
    Env env;
};

OBL_TEST_CASE("Update: the base update first, a failure is the game's fatal error, the cell follows the position")
{
    Boat boat(life::Jetski, 1);
    OBL_CHECK(update::Update(boat.object, boat.env));
    OBL_CHECK(boat.env.calls == (std::vector<std::string>{"base", "tile 5.000000 6.000000"}));
    OBL_CHECK_EQ(boat.object.m_CurrGridXY[0], 7u);
    OBL_CHECK_EQ(boat.object.m_CurrGridXY[1], 9u);

    Boat broken(life::Jetski, 1);
    broken.env.baseResult = false;
    update::Update(broken.object, broken.env);
    OBL_CHECK(broken.env.calls[1] == "fatal");

    Boat flagged(life::Jetski, 1);
    flagged.object.m_debugBreak = 1;
    update::Update(flagged.object, flagged.env);
    OBL_CHECK_EQ(flagged.object.m_debugBreak, 0);
}

OBL_TEST_CASE("Update: the fishing boat frees its cells before the base update and blocks the ones around the new tile after it")
{
    Boat boat(life::FishBoat, 1);
    boat.object.m_gotGrid = 1;
    boat.object.m_CurrGridXY[0] = 3;
    boat.object.m_CurrGridXY[1] = 4;
    for (GridCell& cell : boat.env.cells) cell.m_flags = 0x10 | 0x4;
    update::Update(boat.object, boat.env);
    OBL_CHECK(boat.env.calls == (std::vector<std::string>{"cells 3,4", "base", "tile 5.000000 6.000000", "cells 7,9"}));
    for (const GridCell& cell : boat.env.cells) OBL_CHECK_EQ(cell.m_flags, 0x14u);
    OBL_CHECK_EQ(boat.object.m_gotGrid, 1);

    // Without the flag nothing is freed first, but the boat still blocks its new cells and takes the flag.
    Boat fresh(life::FishBoat, 1);
    update::Update(fresh.object, fresh.env);
    OBL_CHECK(fresh.env.calls == (std::vector<std::string>{"base", "tile 5.000000 6.000000", "cells 7,9"}));
    OBL_CHECK_EQ(fresh.object.m_gotGrid, 1);
    for (const GridCell& cell : fresh.env.cells) OBL_CHECK_EQ(cell.m_flags & 0x10u, 0x10u);
}

OBL_TEST_CASE("Update: a vehicle named BOAT (any case) faces direction 0, other names keep theirs")
{
    Boat boat(life::Pedaloe, 1);
    std::strcpy(boat.object.m_Name, "bOaT");
    update::Update(boat.object, boat.env);
    OBL_CHECK(boat.env.calls == (std::vector<std::string>{"base", "direction body 0", "tile 5.000000 6.000000"}));
    Boat other(life::Pedaloe, 1);
    std::strcpy(other.object.m_Name, "BOATS");
    update::Update(other.object, other.env);
    OBL_CHECK(other.env.calls == (std::vector<std::string>{"base", "tile 5.000000 6.000000"}));
}

OBL_TEST_CASE("Idle: a ferry or a coach with nobody to wait for sends the departure when the definition moves by points")
{
    {
        Boat ferry(life::Ferry, msg::StateIdle);
        update::Update(ferry.object, ferry.env);
        OBL_CHECK(ferry.Middle() == (std::vector<std::string>{"post 17 17 3 0 68"}));
    }
    {
        Boat ferry(life::Ferry, msg::StateIdle);
        ferry.object.m_guestsToWaitFor = 1;
        update::Update(ferry.object, ferry.env);
        OBL_CHECK(ferry.Middle().empty());
    }
    {
        Boat ferry(life::Ferry, msg::StateIdle);
        ferry.definition.m_MoveType = 2;
        update::Update(ferry.object, ferry.env);
        OBL_CHECK(ferry.Middle().empty());
    }
    {
        // A crowd carrier ignores the guests aboard (the count is not asked).
        Boat ferry(life::Ferry, msg::StateIdle);
        ferry.object.m_currCap = 4;
        update::Update(ferry.object, ferry.env);
        OBL_CHECK(ferry.Middle() == (std::vector<std::string>{"post 17 17 3 0 68"}));
    }
    {
        // An empty small boat does not send anything either: only the crowd carriers do.
        Boat jetski(life::Jetski, msg::StateIdle);
        update::Update(jetski.object, jetski.env);
        OBL_CHECK(jetski.Middle().empty());
    }
}

OBL_TEST_CASE("Idle: the coach opens its doors first, once, and leaves when the animation ends")
{
    Boat coach(life::Coach, msg::StateIdle);
    update::Update(coach.object, coach.env);
    OBL_CHECK(coach.Middle() ==
            (std::vector<std::string>{"sound doorhissopen 5.000000 6.000000 0.000000", "type door 1", "anim door coach_doors 100.000000",
                    "post-delayed 3 68 2.500000"}));
    OBL_CHECK_EQ(coach.object.m_playingDoorAnim, 1);

    Boat busy(life::Coach, msg::StateIdle);
    busy.object.m_playingDoorAnim = 1;
    update::Update(busy.object, busy.env);
    OBL_CHECK(busy.Middle().empty());
}

OBL_TEST_CASE("Idle: a boat with guests leaves more than a second after its ride time unless a guest is still boarding")
{
    const struct {
        float ride;
        bool elapsed;
    } rides[] = {{98.0f, true}, {98.999f, true}, {99.0f, false}, {99.5f, false}, {101.0f, false}};
    for (const auto& ride : rides) {
        for (bool boarding : {false, true}) {
            for (int32_t moveType : {1, 2}) {
                Boat boat(life::Pedaloe, msg::StateIdle);
                boat.object.m_currCap = 2;
                boat.object.m_rideTime = ride.ride;
                boat.object.m_waitForGuestToBoard = boarding;
                boat.definition.m_MoveType = static_cast<uint8_t>(moveType);
                update::Update(boat.object, boat.env);
                std::vector<std::string> expected;
                if (ride.elapsed && !boarding) expected.push_back(moveType == 1 ? "post 17 17 3 0 68" : "post 17 17 3 0 69");
                OBL_CHECK(boat.Middle() == expected);
            }
        }
    }
    // NaN never counts as elapsed.
    Boat nan(life::Pedaloe, msg::StateIdle);
    nan.object.m_currCap = 1;
    nan.object.m_rideTime = std::nanf("");
    update::Update(nan.object, nan.env);
    OBL_CHECK(nan.Middle().empty());
}

OBL_TEST_CASE("Moving: the sprite turns to the sixteenth of the velocity, odd sixteenths take the offset animation")
{
    struct Case {
        float x, y;
        int32_t direction;
        bool odd;
    };
    const double s = 0.38268343236508984, c = 0.92387953251128674; // sin and cos of 22.5 degrees
    const Case cases[] = {
            {0.0f, -2.0f, 0, false},
            {2.0f, 0.0f, 2, false},
            {static_cast<float>(2 * s), static_cast<float>(-2 * c), 0, true}, // sixteenth 1
            {static_cast<float>(2 * c), static_cast<float>(-2 * s), 1, true}, // sixteenth 3
            {static_cast<float>(-2 * s), static_cast<float>(2 * c), 4, true}, // sixteenth 9
    };
    for (int32_t type : {life::Jetski, life::SpeedBoat}) {
        for (const Case& test : cases) {
            Boat boat(type, msg::StateDepart);
            boat.object.m_moving = 1;
            boat.object.m_vVelocity[0] = test.x;
            boat.object.m_vVelocity[1] = test.y;
            update::Update(boat.object, boat.env);
            const char* name = type == life::Jetski ? (test.odd ? "d_jetski_normal_offset" : "d_jetski_normal")
                                                    : (test.odd ? "boat_fast_rot" : "boat_normal");
            std::vector<std::string> expected = {
                    std::string("anim body ") + name + " 0.000000", "direction body " + std::to_string(test.direction)};
            if (type == life::Jetski) expected.insert(expected.end(), {"read-direction body", "direction jetski 5"});
            OBL_CHECK_EQ(Join(boat.Middle()), Join(expected));
        }
    }
}

OBL_TEST_CASE("Moving: a boat that is almost still aims at its target, a moving flag or a running turn skips the heading")
{
    Boat boat(life::SpeedBoat, msg::StateRide);
    boat.object.m_moving = 1;
    boat.object.m_vVelocity[0] = 1e-5f;
    boat.object.m_vPosition[0] = 10;
    boat.object.m_vPosition[1] = 10;
    boat.object.m_vTarget[0] = 10;
    boat.object.m_vTarget[1] = 30; // due south of the boat: sixteenth 8
    update::Update(boat.object, boat.env);
    OBL_CHECK(boat.Middle() == (std::vector<std::string>{"anim body boat_normal 0.000000", "direction body 4"}));

    Boat idle(life::SpeedBoat, msg::StateRide);
    idle.object.m_vVelocity[1] = -3;
    update::Update(idle.object, idle.env);
    OBL_CHECK(idle.Middle().empty());

    Boat turning(life::Jetski, msg::StateReturn);
    turning.object.m_moving = 1;
    turning.object.m_turnAnim = 1;
    turning.object.m_currCap = 2;
    update::Update(turning.object, turning.env);
    // The jet ski's own sprite still follows the body; the guests are left to the turn animation.
    OBL_CHECK(turning.Middle() == (std::vector<std::string>{"read-direction body", "direction jetski 5", "step-turn 100.000000"}));
}

OBL_TEST_CASE("Moving: every guest follows the boat, in the order of the list")
{
    Boat boat(life::Pedaloe, msg::StateDepart);
    boat.object.m_currCap = 2;
    boat.object.m_Guests[0] = 31;
    boat.object.m_Guests[1] = 32;
    update::Update(boat.object, boat.env);
    OBL_CHECK(boat.Middle() ==
            (std::vector<std::string>{"teleport 31 5.000000 6.000000", "read-direction body", "sprite 31", "direction guest 5",
                    "teleport 32 5.000000 6.000000", "read-direction body", "sprite 32", "direction guest 5"}));
}

OBL_TEST_CASE("Turning: the next unload state and way point once the turn is over, the turn step while it runs")
{
    Boat boat(life::Jetski, msg::StateTurn);
    boat.object.m_numWayPts = 3;
    boat.object.m_currentWayPoint = 2;
    update::Update(boat.object, boat.env);
    OBL_CHECK(boat.Middle() == (std::vector<std::string>{"post 17 17 3 0 67"}));
    OBL_CHECK_EQ(boat.object.m_currentWayPoint, 0u);

    Boat running(life::Jetski, msg::StateTurn);
    running.object.m_turnAnim = 1;
    running.object.m_numWayPts = 3;
    running.object.m_currentWayPoint = 1;
    update::Update(running.object, running.env);
    OBL_CHECK(running.Middle() == (std::vector<std::string>{"step-turn 100.000000"}));
    OBL_CHECK_EQ(running.object.m_currentWayPoint, 1u);
}

OBL_TEST_CASE(
        "Ferry entering: the offset slides to zero by speed * dt along its direction, the crossing teleports to the way point and sends "
        "0x42")
{
    Boat boat(life::Ferry, msg::StateWait);
    boat.object.m_maxVel = 8;
    boat.object.m_ferryEntryDir = 4; // y grows by speed * dt = 4 per tick: the offset goes down
    boat.object.m_FerryEntryOffset[0] = 0;
    boat.object.m_FerryEntryOffset[1] = 10;
    boat.object.m_WayPoints[0][0] = 11;
    boat.object.m_WayPoints[0][1] = 12;
    update::Update(boat.object, boat.env);
    OBL_CHECK_EQ(boat.object.m_FerryEntryOffset[1], 6.0f);
    OBL_CHECK(boat.Middle().empty());
    // (0, 6): the next ticks bring y to 2 and to -2; zero in x counts as crossed, so the second tick arrives.
    update::Update(boat.object, boat.env);
    boat.env.calls.clear();
    update::Update(boat.object, boat.env);
    OBL_CHECK_EQ(boat.object.m_FerryEntryOffset[1], -2.0f);
    OBL_CHECK(boat.Middle() == (std::vector<std::string>{"teleport-self 11.000000 12.000000", "post 17 17 3 0 66"}));
}

OBL_TEST_CASE("Ferry entering: every direction of the compass, half steps for the diagonals")
{
    const struct {
        int32_t direction;
        float dx, dy;
    } steps[] = {{0, 0, -4}, {1, 2, -2}, {2, 4, 0}, {3, 2, 2}, {4, 0, 4}, {5, -2, 2}, {6, -4, 0}, {7, -2, -2}};
    for (const auto& step : steps) {
        Boat boat(life::Ferry, msg::StateWait);
        boat.object.m_maxVel = 8;
        boat.object.m_ferryEntryDir = static_cast<uint32_t>(step.direction);
        boat.object.m_FerryEntryOffset[0] = 100;
        boat.object.m_FerryEntryOffset[1] = 100;
        update::Update(boat.object, boat.env);
        OBL_CHECK_EQ(boat.object.m_FerryEntryOffset[0], 100.0f - step.dx);
        OBL_CHECK_EQ(boat.object.m_FerryEntryOffset[1], 100.0f - step.dy);
    }
}

OBL_TEST_CASE("Ferry leaving: the exit offset slides, the removal flag follows the entry offsets, not the exit ones")
{
    {
        Boat boat(life::Ferry, msg::StateRemove);
        boat.object.m_maxVel = 8;
        boat.object.m_ferryExitDir = 2;
        boat.object.m_FerryExitOffset[0] = 9;
        boat.object.m_FerryExitOffset[1] = 9;
        update::Update(boat.object, boat.env);
        OBL_CHECK_EQ(boat.object.m_FerryExitOffset[0], 5.0f);
        OBL_CHECK_EQ(boat.object.m_FerryExitOffset[1], 9.0f);
        // Entry offsets are zero in a fresh object: the ferry is marked for removal at once.
        OBL_CHECK_EQ(boat.object.m_waitingForDeath, 1);
    }
    {
        Boat boat(life::Ferry, msg::StateRemove);
        boat.object.m_FerryEntryOffset[0] = 0.5f;
        update::Update(boat.object, boat.env);
        OBL_CHECK_EQ(boat.object.m_waitingForDeath, 0);
    }
    {
        Boat boat(life::Ferry, msg::StateRemove);
        boat.object.m_FerryEntryOffset[1] = -0.0f;
        update::Update(boat.object, boat.env);
        OBL_CHECK_EQ(boat.object.m_waitingForDeath, 1);
    }
}

OBL_TEST_CASE("Update: states without a branch (created, unload, fall, remount) only do the common part")
{
    for (int32_t state : {1, 0x43, 0x49, 0x4a, 0x50}) {
        Boat boat(life::Jetski, state);
        boat.object.m_moving = 1;
        update::Update(boat.object, boat.env);
        OBL_CHECK(boat.env.calls == (std::vector<std::string>{"base", "tile 5.000000 6.000000"}));
    }
}

OBL_TEST_CASE("Crossed: the compare chains, NaN counts as 'not above zero' in the second alternative")
{
    using update::detail::Crossed;
    OBL_CHECK(Crossed(1, -1));
    OBL_CHECK(Crossed(1, 0));
    OBL_CHECK(Crossed(0, 0));
    OBL_CHECK(Crossed(-1, 1));
    OBL_CHECK(Crossed(-1, 0));
    OBL_CHECK(!Crossed(1, 1));
    OBL_CHECK(!Crossed(-1, -1));
    OBL_CHECK(Crossed(std::nanf(""), 1));
    OBL_CHECK(Crossed(1, std::nanf("")));
}
} // namespace
