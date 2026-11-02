#include "core/text16.h"
#include "unit_test.h"
#include "game/objects/vehicle_presentation.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"

#include <array>
#include <string>
#include <vector>

namespace {
namespace present = obl::vehicle::presentation;
using namespace obl::layout;
using obl::object::Vehicle;
using obl::ui::Sprite;
using obl::ui::SpriteAnim;
using obl::ui::SpriteInstance;
struct Call {
    int sprite, direction;
    std::array<float, 3> world;
    float time, tone, illness;
};
struct Env {
    Vehicle object{};
    obl::object::Base owner{}, guests[3]{};
    SpriteInstance sprites[9]{};
    Sprite asset{};
    SpriteAnim anims[2]{};
    SpriteAnim* animations[2] = {&anims[0], &anims[1]};
    std::vector<Call> calls;
    std::vector<std::string> queries;
    int zoom = 1;
    bool open = true;
    std::string fatal;
    Env()
    {
        anims[0].m_directions = 4;
        anims[1].m_directions = 8;
        asset.m_anims = animations;
        for (auto& s : sprites) {
            s.m_sprite = &asset;
            s.m_animIndex = 1;
            s.m_direction = 7;
        }
        object.m_sprite = &sprites[0];
        object.m_vehicleSprite = &sprites[1];
        object.m_doorSprite = &sprites[5];
        object.m_WorldXYZ[0] = 10;
        object.m_WorldXYZ[1] = 20;
        object.m_WorldXYZ[2] = 30;
        object.m_ownerId = 50;
        for (int i = 0; i < 3; ++i) {
            object.m_Guests[i] = i;
            guests[i].m_sprite = &sprites[6 + i];
            guests[i].m_WorldXYZ[0] = 100.0f + static_cast<float>(i);
            guests[i].m_WorldXYZ[1] = 200.0f + static_cast<float>(i);
            guests[i].m_WorldXYZ[2] = 300.0f + static_cast<float>(i);
        }
    }
    void Parts()
    {
        object.m_auxSprite1 = &sprites[2];
        object.m_auxSprite2 = &sprites[3];
        object.m_auxSprite3 = &sprites[4];
    }
    float Time() { return 123.5f; }
    float Delta() { return 0.02f; }
    int Zoom() { return zoom; }
    void BaseDraw(Vehicle& vehicle) { Draw(present::Sprite(vehicle.m_sprite), vehicle.m_WorldXYZ, Time()); }
    void Draw(SpriteInstance* sprite, float* world, float time) { Toned(sprite, world, time, 0, 0); }
    void Toned(SpriteInstance* sprite, float* world, float time, float tone, float illness)
    {
        calls.push_back({static_cast<int>(sprite - sprites), sprite->m_direction, {world[0], world[1], world[2]}, time, tone, illness});
    }
    obl::object::Base* Object(int id)
    {
        queries.push_back("object " + std::to_string(id));
        return id == 50 ? &owner : &guests[id];
    }
    bool Open(obl::object::Base*)
    {
        queries.push_back("open");
        return open;
    }
    float Illness(obl::object::Base*)
    {
        queries.push_back("illness");
        return 0.25f;
    }
    double Tone(obl::object::Base*)
    {
        queries.push_back("tone");
        return 0.123456789;
    }
    void Wide(const char* text, obl::Char16* out)
    {
        while ((*out++ = static_cast<unsigned char>(*text++))) {}
    }
    void Fatal(const char* text) { fatal = text; }
    void Run() { present::Draw(object, *this); }
};
} // namespace

OBL_TEST_CASE("vehicle_presentation.ferry_position_direction_and_delta")
{
    for (int state : {0x47, 0x48}) {
        Env e;
        e.object.m_state = state;
        e.object.m_vehicleType = 5;
        float* point = state == 0x47 ? e.object.m_FerryEntryPoint : e.object.m_FerryExitPoint;
        float* offset = state == 0x47 ? e.object.m_FerryEntryOffset : e.object.m_FerryExitOffset;
        point[0] = 40;
        point[1] = -30;
        point[2] = 10;
        offset[0] = 7;
        offset[1] = -9;
        e.object.m_ferryEntryDir = e.object.m_ferryExitDir = 11;
        e.Run();
        OBL_REQUIRE(e.calls.size() == 2);
        OBL_CHECK_EQ(e.calls[0].sprite, 0);
        OBL_CHECK_EQ(e.calls[0].direction, 3);
        OBL_CHECK((e.calls[0].world == std::array<float, 3>{33, -21, 10}));
        OBL_CHECK_EQ(e.calls[0].time, 0.02f);
        OBL_CHECK_EQ(e.calls[1].sprite, 5);
        OBL_CHECK_EQ(e.calls[1].time, 123.5f);
        OBL_CHECK(e.calls[1].world == e.calls[0].world);
    }
}
OBL_TEST_CASE("vehicle_presentation.wake_and_coach_layers")
{
    for (int state : {0x41, 0x42, 0x44, 0x45}) {
        Env e;
        e.object.m_state = state;
        e.object.m_vehicleType = 0;
        e.Run();
        const bool wake = state == 0x42 || state == 0x44;
        OBL_REQUIRE(e.calls.size() == (wake ? 2u : 1u));
        OBL_CHECK_EQ(e.calls[0].sprite, wake ? 1 : 0);
    }
    Env e;
    e.object.m_vehicleType = 5;
    e.object.m_currCap = 3;
    e.Run();
    OBL_REQUIRE(e.calls.size() == 2);
    OBL_CHECK_EQ(e.calls[1].sprite, 5);
    OBL_CHECK(e.queries.empty());
}
OBL_TEST_CASE("vehicle_presentation.closed_fishing_driver_and_direction_clamp")
{
    Env e;
    e.Parts();
    e.object.m_vehicleType = 6;
    e.object.m_state = 0x41;
    e.open = false;
    e.anims[1].m_directions = 4;
    e.Run();
    OBL_REQUIRE(e.calls.size() == 3);
    OBL_CHECK_EQ(e.calls[1].sprite, 2);
    OBL_CHECK_EQ(e.calls[1].direction, 3);
    OBL_CHECK_EQ(e.calls[2].sprite, 3);
    OBL_CHECK_EQ(e.sprites[4].m_direction, 7);
    OBL_CHECK((e.queries == std::vector<std::string>{"object 50", "open"}));
    e.object.m_state = 0x45;
    e.calls.clear();
    e.Run();
    OBL_REQUIRE(e.calls.size() == 4);
    OBL_CHECK_EQ(e.calls.back().sprite, 4);
}
OBL_TEST_CASE("vehicle_presentation.clockwise_turn_restores_hull_rods_guests")
{
    Env e;
    e.Parts();
    e.object.m_vehicleType = 6;
    e.object.m_turnAnim = e.object.m_turnCW = 1;
    e.object.m_currCap = 3;
    e.Run();
    OBL_REQUIRE(e.calls.size() == 7);
    for (const auto& call : e.calls) OBL_CHECK_EQ(call.direction, 0);
    OBL_CHECK_EQ(e.sprites[0].m_direction, 7);
    OBL_CHECK_EQ(e.sprites[2].m_direction, 7);
    OBL_CHECK_EQ(e.sprites[3].m_direction, 7);
    OBL_CHECK_EQ(e.sprites[4].m_direction, 0);
    for (int i = 0; i < 3; ++i) {
        OBL_CHECK_EQ(e.sprites[6 + i].m_direction, 7);
        OBL_CHECK(e.calls[4 + i].world[0] == 100 + i);
    }
    e.calls.clear();
    e.object.m_waitForGuestToBoard = 1;
    e.Run();
    OBL_CHECK_EQ(e.calls.size(), 4u);
}
OBL_TEST_CASE("vehicle_presentation.first_guest_boarding_bias_and_tone")
{
    for (int type : {0, 1, 3, 4, 6}) {
        Env e;
        e.object.m_vehicleType = type;
        e.object.m_currCap = 3;
        e.object.m_waitForGuestToBoard = 1;
        e.zoom = 2;
        e.Run();
        OBL_REQUIRE(e.calls.size() == 2);
        OBL_CHECK_EQ(e.calls[1].sprite, 6);
        OBL_CHECK_EQ(e.calls[1].world[0], 10.0f);
        OBL_CHECK_EQ(e.calls[1].world[1], 22.0f);
        OBL_CHECK_EQ(e.calls[1].tone, static_cast<float>(0.123456789));
        OBL_CHECK_EQ(e.calls[1].illness, 0.25f);
        OBL_CHECK_EQ(e.sprites[7].m_direction, 7);
        OBL_CHECK((e.queries == std::vector<std::string>{"object 0", "illness", "tone", "object 1", "object 2"}));
    }
    Env e;
    e.object.m_vehicleType = 2;
    e.object.m_currCap = 3;
    e.Run();
    OBL_CHECK_EQ(e.calls.size(), 1u);
}
OBL_TEST_CASE("vehicle_presentation.variable_keys_states_and_invalid_fatal")
{
    Env e;
    obl::tables::VehicleDef def{};
    e.object.m_definition = &def;
    def.m_Name = "Boat 100%";
    obl::Char16 out[256]{};
    present::GetVariableText(e.object, "VEHICLE_NAME", out, e);
    OBL_CHECK(obl::text16::Compare(out, u"Boat 100%") == 0);
    const int states[] = {1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x47, 0x48, -1};
    const obl::Char16* texts[] = {u"Startup", u"Waiting Guests", u"Heading Stop", u"Unloading Guests", u"Ride In Progress",
            u"Unrecognized State", u"Unrecognized State", u"Unrecognized State", u"Unrecognized State"};
    for (int i = 0; i < 9; ++i) {
        e.object.m_state = states[i];
        present::GetVariableText(e.object, "VEHICLE_STATE", out, e);
        OBL_CHECK(obl::text16::Compare(out, texts[i]) == 0);
    }
    obl::text16::Copy(out, u"unchanged");
    present::GetVariableText(e.object, "vehicle_state", out, e);
    OBL_CHECK_EQ(e.fatal, "A script read an unrecognized vehicle variable");
    OBL_CHECK(obl::text16::Compare(out, u"unchanged") == 0);
}
