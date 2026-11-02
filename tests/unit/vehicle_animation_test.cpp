#include "unit_test.h"
#include "game/objects/vehicle_helpers.h"
#include "game/ui/sprite.h"

#include <string>
#include <vector>

namespace {
using obl::object::Vehicle;
using obl::ui::SpriteInstance;
namespace helpers = obl::vehicle::helpers;
namespace life = obl::vehicle::lifecycle;

struct Env {
    Vehicle* live = nullptr;
    std::vector<std::string> calls;
    std::vector<int> rolls; // values returned by Rand(), consumed in order
    size_t nextRoll = 0;
    float now = 100;
    float length = 2.5f;
    float guestLength = 3.5f;
    bool bag = false;
    int32_t direction = 6;

    static std::string Label(void* sprite) { return std::to_string(reinterpret_cast<uintptr_t>(sprite) % 1000003); }
    float WorldTime()
    {
        calls.push_back("time");
        return now;
    }
    int Rand()
    {
        calls.push_back("rand");
        return nextRoll < rolls.size() ? rolls[nextRoll++] : 1;
    }
    float StartAnim(void* sprite, const char* name, float start, const float* world)
    {
        calls.push_back(
                "anim " + Label(sprite) + " " + (name ? name : "NULL") + " " + std::to_string(start) + (world ? " world" : " nullworld"));
        return length;
    }
    void SetAnimType(void* sprite, int32_t type) { calls.push_back("type " + Label(sprite) + " " + std::to_string(type)); }
    void TeleportGuest(int32_t guest, const float* world)
    {
        OBL_CHECK(world == live->m_WorldXYZ);
        calls.push_back("teleport " + std::to_string(guest));
    }
    float StartGuestAnim(int32_t guest, const char* name, float start, const float* world)
    {
        OBL_CHECK(world == live->m_WorldXYZ);
        calls.push_back("guest " + std::to_string(guest) + " " + (name ? name : "NULL") + " " + std::to_string(start));
        return guestLength;
    }
    void SetGuestAnimType(int32_t guest, int32_t type)
    {
        calls.push_back("guest-type " + std::to_string(guest) + " " + std::to_string(type));
    }
    void* GuestSprite(int32_t guest)
    {
        calls.push_back("sprite " + std::to_string(guest));
        return &guestSprite;
    }
    void* GuestAttachedSprite(int32_t guest)
    {
        calls.push_back("bag " + std::to_string(guest));
        return bag ? &bagSprite : nullptr;
    }
    int32_t Direction(void*)
    {
        calls.push_back("direction");
        return direction;
    }
    void SetDirection(void* sprite, int32_t value)
    {
        calls.push_back(std::string("set-direction ") + (sprite == &bagSprite ? "bag " : "guest ") + std::to_string(value));
    }
    void SetGuestVisible(int32_t guest, bool visible)
    {
        calls.push_back("visible " + std::to_string(guest) + " " + std::to_string(visible));
    }
    void SetGuestPosition(int32_t guest, float position[3])
    {
        calls.push_back("place " + std::to_string(guest) + " " + std::to_string(position[0]) + " " + std::to_string(position[1]) + " " +
                std::to_string(position[2]));
    }
    bool WharfWalkPoint(float out[3])
    {
        calls.push_back("wharf");
        if (!wharf) return false;
        out[0] = 7;
        out[1] = 9;
        out[2] = 0;
        return true;
    }
    bool wharf = true;

    SpriteInstance guestSprite{}, bagSprite{};
};

// Sprites are labelled by their address; the vehicle gets a distinct slot for each part.
struct Boat {
    SpriteInstance sprites[5]{};
    Vehicle object{};
    Env env;
    std::string Name(int i) const { return Env::Label(const_cast<SpriteInstance*>(&sprites[i])); }
    explicit Boat(int32_t type, int32_t cap = 0)
    {
        object.m_vehicleType = type;
        object.m_sprite = &sprites[0];
        // As in the game: only the jet-ski has a wake sprite and only the fishing boat has rod and driver sprites.
        if (type == life::Jetski) object.m_vehicleSprite = &sprites[1];
        if (type == life::FishBoat) {
            object.m_auxSprite1 = &sprites[2];
            object.m_auxSprite2 = &sprites[3];
            object.m_auxSprite3 = &sprites[4];
        }
        object.m_currCap = cap;
        object.m_Guests[0] = 11;
        object.m_Guests[1] = 22;
        object.m_Guests[2] = 33;
        object.m_WorldXYZ[0] = 1;
        object.m_WorldXYZ[1] = 2;
        object.m_WorldXYZ[2] = 3;
        env.live = &object;
    }
};

std::string T(float value)
{
    return std::to_string(value);
}
} // namespace

OBL_TEST_CASE("Ride animation: names, order and the cleared turn bookkeeping per type")
{
    for (int32_t cap : {0, 1, 2}) {
        {
            Boat boat(life::SpeedBoat, cap);
            boat.object.m_numTurns = 5;
            boat.object.m_turnAnimStartTime = 6;
            boat.object.m_turnAnimLength = 7;
            boat.object.m_turnAnim = 1;
            boat.object.m_turnCW = 1;
            helpers::PlayRide(boat.object, 12.5f, boat.env);
            std::vector<std::string> expected = {
                    "anim " + boat.Name(0) + " boat_fast " + T(12.5f) + " world", "type " + boat.Name(0) + " 0"};
            if (cap >= 1)
                for (const char* line : {"teleport 11", "guest 11 boat_driver_normal 12.500000", "guest-type 11 0"})
                    expected.push_back(line);
            if (cap >= 2)
                for (const char* line : {"teleport 22", "guest 22 boat_pass_normal 12.500000", "guest-type 22 0"}) expected.push_back(line);
            OBL_CHECK(boat.env.calls == expected);
            OBL_CHECK(boat.object.m_numTurns == 0);
            OBL_CHECK(boat.object.m_turnAnimStartTime == 0);
            OBL_CHECK(boat.object.m_turnAnimLength == 0);
            OBL_CHECK(boat.object.m_turnAnim == 0);
            OBL_CHECK(boat.object.m_turnCW == 0);
        }
        {
            Boat boat(life::Jetski, cap);
            helpers::PlayRide(boat.object, 4, boat.env);
            std::vector<std::string> expected = {
                    "anim " + boat.Name(0) + " d_jetski_normal " + T(4) + " world", "type " + boat.Name(0) + " 0"};
            if (cap >= 1)
                for (const char* line : {"teleport 11", "guest 11 d_jetski_normal 4.000000", "guest-type 11 0"}) expected.push_back(line);
            if (cap >= 2)
                for (const char* line : {"teleport 22", "guest 22 NULL 4.000000", "guest-type 22 0"}) expected.push_back(line);
            if (cap >= 1)
                for (const std::string& line :
                        {"anim " + boat.Name(1) + " d_wake_normal " + T(4) + " world", "type " + boat.Name(1) + " 0"})
                    expected.push_back(line);
            OBL_CHECK(boat.env.calls == expected);
        }
    }
    {
        Boat boat(life::Pedaloe, 2);
        helpers::PlayRide(boat.object, 1, boat.env);
        OBL_CHECK(boat.env.calls[0] == "anim " + boat.Name(0) + " ride " + T(1) + " world");
        OBL_CHECK(boat.env.calls[3] == "guest 11 d_ped_ride_pass 1.000000");
        OBL_CHECK(boat.env.calls[6] == "guest 22 d_ped_ride 1.000000");
        OBL_CHECK(boat.env.calls.size() == 8);
    }
    {
        // Fishing boat: rods by name, the fish_driver part by the pointer; every part gets type 0 on itself.
        Boat boat(life::FishBoat, 1);
        helpers::PlayRide(boat.object, 2, boat.env);
        const std::vector<std::string> expected = {"anim " + boat.Name(0) + " boat_bob " + T(2) + " world", "type " + boat.Name(0) + " 0",
                "teleport 11", "guest 11 fish_man1_fidget 2.000000", "guest-type 11 0",
                "anim " + boat.Name(2) + " rod01_bob " + T(2) + " world", "type " + boat.Name(2) + " 0",
                "anim " + boat.Name(3) + " rod02_bob " + T(2) + " world", "type " + boat.Name(3) + " 0",
                "anim " + boat.Name(4) + " fish_driver " + T(2) + " world", "type " + boat.Name(4) + " 0"};
        OBL_CHECK(boat.env.calls == expected);
    }
    {
        // A part without a sprite pointer is skipped, but the fish_driver name is only looked at through that pointer.
        Boat boat(life::SpeedBoat);
        boat.object.m_auxSprite3 = &boat.sprites[4];
        helpers::PlayRide(boat.object, 2, boat.env);
        OBL_CHECK(boat.env.calls.size() == 4);
        OBL_CHECK(boat.env.calls[2] == "anim " + boat.Name(4) + " NULL " + T(2) + " world");
        boat.object.m_auxSprite3 = nullptr;
        boat.env.calls.clear();
        helpers::PlayRide(boat.object, 2, boat.env);
        OBL_CHECK(boat.env.calls.size() == 2);
    }
}

OBL_TEST_CASE("Idle animation: one draw per looping boat, double-precision offset, rods typed on the hull")
{
    {
        Boat boat(life::Jetski, 2);
        boat.env.rolls = {37};
        helpers::PlayIdle(boat.object, boat.env);
        const float start = static_cast<float>(37.0 * static_cast<double>(0.01f) + 100.0);
        OBL_CHECK(boat.env.calls ==
                (std::vector<std::string>{"time", "rand", "anim " + boat.Name(0) + " d_jetski_normal " + T(start) + " world",
                        "type " + boat.Name(0) + " 0"}));
    }
    {
        Boat boat(life::SpeedBoat);
        boat.env.rolls = {99};
        helpers::PlayIdle(boat.object, boat.env);
        OBL_CHECK(boat.env.calls[2] ==
                "anim " + boat.Name(0) + " boat_normal " + T(static_cast<float>(99.0 * static_cast<double>(0.01f) + 100.0)) + " world");
    }
    {
        // The rand() result is taken modulo 100 with the sign (a negative draw cannot happen, 0 and 100 start at the world time).
        Boat boat(life::SpeedBoat);
        boat.env.rolls = {200};
        helpers::PlayIdle(boat.object, boat.env);
        OBL_CHECK(boat.env.calls[2] == "anim " + boat.Name(0) + " boat_normal " + T(100.0f) + " world");
    }
    {
        // The pedal boat starts the loop twice: exactly at the world time (no animation type), then with the offset.
        Boat boat(life::Pedaloe);
        boat.env.rolls = {50};
        helpers::PlayIdle(boat.object, boat.env);
        OBL_CHECK(boat.env.calls ==
                (std::vector<std::string>{"time", "anim " + boat.Name(0) + " ride " + T(100.0f) + " world", "time", "rand",
                        "anim " + boat.Name(0) + " ride " + T(static_cast<float>(50.0 * static_cast<double>(0.01f) + 100.0)) + " world",
                        "type " + boat.Name(0) + " 0"}));
    }
    {
        Boat boat(life::FishBoat, 2);
        boat.env.rolls = {10};
        helpers::PlayIdle(boat.object, boat.env);
        const std::string start = T(static_cast<float>(10.0 * static_cast<double>(0.01f) + 100.0));
        const std::vector<std::string> expected = {"time", "rand", "anim " + boat.Name(0) + " boat_bob " + start + " world",
                "type " + boat.Name(0) + " 0", "anim " + boat.Name(2) + " rod01_bob " + start + " world", "type " + boat.Name(0) + " 0",
                "anim " + boat.Name(3) + " rod02_bob " + start + " world", "type " + boat.Name(0) + " 0",
                "anim " + boat.Name(4) + " fish_driver " + start + " world", "type " + boat.Name(0) + " 0"};
        OBL_CHECK(boat.env.calls == expected);
    }
    for (int32_t type : {int32_t{life::Ferry}, int32_t{life::Banana}, int32_t{life::Coach}, int32_t{7}}) {
        // No looping parts: nothing is started and nothing is drawn.
        Boat boat(type, 1);
        helpers::PlayIdle(boat.object, boat.env);
        OBL_CHECK(boat.env.calls.empty());
    }
}

OBL_TEST_CASE("Catch animation: variants by draw, short-circuit second roll, null-world rods, length of the first rod")
{
    {
        Boat boat(life::FishBoat, 0);
        OBL_CHECK(helpers::PlayCatch(boat.object, boat.env) == 0.0f);
        OBL_CHECK(boat.env.calls == (std::vector<std::string>{"time"}));
    }
    struct Case {
        std::vector<int> rolls;
        const char* man1;
        const char* rod1;
        const char* man2;
        const char* rod2;
        size_t rollsUsed;
    };
    const Case cases[] = {
            {{1, 1}, "fish_man1_catch_a", "rod01_catch_a", "fish_man2_catch_a", "rod02_catch_a", 2},
            {{1, 5}, "fish_man1_catch_a", "rod01_catch_a", "fish_man2_catch_b", "rod02_catch_b", 2},
            {{5}, "fish_man1_catch_b", "rod01_catch_b", "fish_man2_catch_a", "rod02_catch_a",
                    1}, // the second fisherman does not roll after a "b"
    };
    for (const Case& c : cases) {
        Boat boat(life::FishBoat, 2);
        boat.env.rolls = c.rolls;
        const float result = helpers::PlayCatch(boat.object, boat.env);
        OBL_CHECK(result == boat.env.length);
        OBL_CHECK(boat.env.nextRoll == c.rollsUsed);
        // The second fisherman rolls (only after variant a), then teleport, animation, rod, type.
        std::vector<std::string> all = {"time", "teleport 11", "rand", std::string("guest 11 ") + c.man1 + " 100.000000", "guest-type 11 0",
                std::string("anim ") + boat.Name(2) + " " + c.rod1 + " 100.000000 nullworld"};
        if (c.rollsUsed == 2) all.push_back("rand");
        for (const std::string& line : {std::string("teleport 22"), std::string("guest 22 ") + c.man2 + " 100.000000",
                     std::string("anim ") + boat.Name(3) + " " + c.rod2 + " 100.000000 nullworld", std::string("guest-type 22 0")})
            all.push_back(line);
        OBL_CHECK(boat.env.calls == all);
    }
    {
        Boat boat(life::FishBoat, 1);
        boat.env.rolls = {0};
        helpers::PlayCatch(boat.object, boat.env);
        OBL_CHECK(boat.env.calls.size() == 6); // no second fisherman, no roll for him
    }
}

OBL_TEST_CASE("Boarding animation: small boats, riders chosen by name, animation type by direction")
{
    for (bool boarding : {false, true})
        for (bool pair : {false, true}) {
            const std::string type = std::to_string(boarding ? 2 : 1);
            {
                Boat boat(life::SpeedBoat, 2);
                const float result = helpers::PlayBoarding(boat.object, 8, boarding, pair, boat.env);
                std::vector<std::string> expected = {"anim " + boat.Name(0) + " boat_mount " + T(8) + " world",
                        "type " + boat.Name(0) + " " + type, "teleport 11",
                        std::string("guest 11 ") + (pair ? "boat_driver_pass_mount" : "boat_driver_mount") + " 8.000000",
                        "guest-type 11 " + type};
                if (pair)
                    for (const char* line : {"teleport 22", "guest 22 boat_pass_mount 8.000000"}) expected.push_back(line);
                if (pair) expected.push_back("guest-type 22 " + type);
                OBL_CHECK(boat.env.calls == expected);
                OBL_CHECK(result == boat.env.guestLength);
            }
            {
                Boat boat(life::Pedaloe, 2);
                helpers::PlayBoarding(boat.object, 8, boarding, pair, boat.env);
                OBL_CHECK(boat.env.calls[0] == "anim " + boat.Name(0) + (pair ? " climb_on " : " climb_across ") + T(8) + " world");
                OBL_CHECK(boat.env.calls[3] ==
                        std::string("guest 11 ") + (pair ? "d_ped_climb_on_pass" : "d_ped_climb_across") + " 8.000000");
                if (pair) OBL_CHECK(boat.env.calls[6] == "guest 22 d_ped_climb_on 8.000000");
                OBL_CHECK(boat.env.calls.size() == (pair ? 8u : 5u));
            }
            {
                Boat boat(life::Jetski, 1);
                const float result = helpers::PlayBoarding(boat.object, 8, boarding, pair, boat.env);
                OBL_CHECK(boat.env.calls ==
                        (std::vector<std::string>{"anim " + boat.Name(0) + " d_stand_mount_jetski " + T(8) + " world",
                                "type " + boat.Name(0) + " " + type, "teleport 11", "guest 11 d_stand_mount_jetski 8.000000",
                                "guest-type 11 " + type}));
                OBL_CHECK(result == boat.env.guestLength);
            }
        }
    {
        // Riders are reached by name, not by the passenger count: an empty speed boat still uses guest slot 0.
        Boat boat(life::SpeedBoat, 0);
        helpers::PlayBoarding(boat.object, 1, true, false, boat.env);
        OBL_CHECK(boat.env.calls[2] == "teleport 11");
    }
    {
        // Without riders' names (banana boat) the hull is still started, with a null name, and its length is returned.
        Boat boat(life::Banana, 0);
        OBL_CHECK(helpers::PlayBoarding(boat.object, 1, true, false, boat.env) == boat.env.length);
        OBL_CHECK(boat.env.calls ==
                (std::vector<std::string>{"anim " + boat.Name(0) + " NULL " + T(1) + " world", "type " + boat.Name(0) + " 2"}));
    }
}

OBL_TEST_CASE("Boarding animation: fishing boat keeps type 2, rods are not placed, the length is the last rider's")
{
    for (bool boarding : {false, true})
        for (bool pair : {false, true}) {
            Boat boat(life::FishBoat, 2);
            const float result = helpers::PlayBoarding(boat.object, 3, boarding, pair, boat.env);
            const std::string start = " " + T(3);
            std::vector<std::string> expected = {
                    "anim " + boat.Name(0) + " boat_bob" + start + " world", "type " + boat.Name(0) + " 2", "teleport 11"};
            const char* man1 = pair ? "fish_man1_fidget" : boarding ? "fish_man1_onboat" : "fish_man1_outboat";
            expected.push_back(std::string("guest 11 ") + man1 + " 3.000000");
            expected.push_back("guest-type 11 2");
            if (pair) {
                expected.push_back("teleport 22");
                expected.push_back(std::string("guest 22 ") + (boarding ? "fish_man2_onboat" : "fish_man2_outboat") + " 3.000000");
                expected.push_back("guest-type 22 2");
            }
            expected.push_back("anim " + boat.Name(2) + " rod01_bob" + start + " nullworld");
            expected.push_back("anim " + boat.Name(3) + " rod02_bob" + start + " nullworld");
            expected.push_back("anim " + boat.Name(4) + " fish_driver" + start + " nullworld");
            OBL_CHECK(boat.env.calls == expected);
            OBL_CHECK(result == boat.env.guestLength);
        }
}

OBL_TEST_CASE("Boarding animation: ferry shows or hides the last passenger and returns half a second")
{
    {
        Boat boat(life::Ferry, 3);
        OBL_CHECK(helpers::PlayBoarding(boat.object, 5, false, false, boat.env) == 0.5f);
        OBL_CHECK(boat.env.calls == (std::vector<std::string>{"wharf", "place 33 7.000000 9.000000 0.000000", "visible 33 1"}));
    }
    {
        Boat boat(life::Ferry, 3);
        OBL_CHECK(helpers::PlayBoarding(boat.object, 5, true, true, boat.env) == 0.5f);
        OBL_CHECK(boat.env.calls == (std::vector<std::string>{"visible 33 0"}));
    }
    {
        // Bug fix: with the wharf demolished the passenger steps off at the ferry, not at a stale query result.
        Boat boat(life::Ferry, 3);
        boat.env.wharf = false;
        boat.object.m_WorldXYZ[0] = 11;
        boat.object.m_WorldXYZ[1] = 13;
        boat.object.m_WorldXYZ[2] = 2;
        OBL_CHECK(helpers::PlayBoarding(boat.object, 5, false, false, boat.env) == 0.5f);
        OBL_CHECK(boat.env.calls == (std::vector<std::string>{"wharf", "place 33 11.000000 13.000000 0.000000", "visible 33 1"}));
    }
}

OBL_TEST_CASE("Boarding animation: coach rider follows the coach direction and the bag animation")
{
    for (bool boarding : {false, true}) {
        Boat boat(life::Coach, 2);
        boat.env.bag = true;
        const float result = helpers::PlayBoarding(boat.object, 6, boarding, false, boat.env);
        std::vector<std::string> expected = {"teleport 22", "visible 22 1", "sprite 22", "direction", "set-direction guest 6",
                "type " + Env::Label(&boat.env.guestSprite) + " 2", "bag 22", "direction", "set-direction bag 6",
                "anim " + Env::Label(&boat.env.bagSprite) + " bags_offbus " + T(6) + " world"};
        if (boarding) expected.push_back("type " + Env::Label(&boat.env.bagSprite) + " 1");
        expected.push_back(
                "anim " + Env::Label(&boat.env.guestSprite) + (boarding ? " d_walk_onbus " : " d_walk_offbus ") + T(6) + " world");
        OBL_CHECK(boat.env.calls == expected);
        OBL_CHECK(result == boat.env.length);
    }
    for (bool boarding : {false, true}) {
        Boat boat(life::Coach, 1);
        helpers::PlayBoarding(boat.object, 6, boarding, true, boat.env);
        OBL_CHECK(boat.env.calls.back() ==
                "anim " + Env::Label(&boat.env.guestSprite) + (boarding ? " d_walk_onbus_nobag " : " d_walk_offbus_nobag ") + T(6) +
                        " world");
        OBL_CHECK(boat.env.calls[boat.env.calls.size() - 2] == "bag 11");
    }
}
