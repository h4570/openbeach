#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/world/world_state.h"
#include "object_storage.h"
#include "unit_test.h"

#include "game/objects/staff_service_home.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace obl;
using namespace obl::layout;
using obl::messages::Message;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;
namespace svc = obl::staff::service;

namespace {

struct Posed {
    SpriteInstance instance;
    Sprite sprite;
    SpriteAnim anim;
    SpriteAnim* table[1];

    explicit Posed(uint8_t directions, int32_t direction = 0)
    {
        std::memset(static_cast<void*>(this), 0, sizeof(*this));
        anim.m_directions = directions;
        table[0] = &anim;
        sprite.m_animCount = 1;
        sprite.m_anims = table;
        instance.m_sprite = &sprite;
        instance.m_direction = direction;
    }
};

struct World {
    obl::object::ServiceStaff member;
    Posed memberSprite;
    object::Base home;
    Posed homeSprite;
    BuildingBehavior behavior = BuildingBehavior::None; // the home's definition behavior: the decks follow it, not the name
    std::string log;

    World(const char* memberName, const char* homeName, int32_t rotation, uint8_t directions = 8)
        : memberSprite(directions), homeSprite(4, rotation)
    {
        obl::unit::FillObject(member);
        obl::unit::FillObject(home);
        member.m_id = 77;
        member.m_HomeID = -1;
        std::strcpy(member.m_Name, memberName);
        member.m_sprite = &memberSprite.instance;
        member.m_WorldXYZ[0] = 1.0f;
        member.m_WorldXYZ[1] = 2.0f;
        member.m_WorldXYZ[2] = 3.0f;
        home.m_id = 12;
        std::strcpy(home.m_Name, homeName);
        home.m_sprite = &homeSprite.instance;
        home.m_WorldXYZ[0] = 1000.0f;
        home.m_WorldXYZ[1] = 2000.0f;
        home.m_WorldXYZ[2] = 5.0f;
    }

    object::Base* Object(int32_t id)
    {
        log += "object " + std::to_string(id) + "; ";
        return &home;
    }
    BuildingBehavior Behavior(const object::Base&) { return behavior; }
    void ServePosition(const object::Base&, float* out)
    {
        out[0] = 10.0f;
        out[1] = 20.0f;
        out[2] = 30.0f;
    }
    void FaceTowards(const float* target, const float* from)
    {
        char text[96];
        std::snprintf(text, sizeof(text), "face %g,%g,%g from %g,%g,%g; ", target[0], target[1], target[2], from[0], from[1], from[2]);
        log += text;
    }
    void FootprintPoint(const object::Base&, float x, float y, uint32_t rotation, float* out)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "footprint %g,%g r%u; ", x, y, rotation);
        log += text;
        out[0] = 640.0f;
        out[1] = 480.0f;
    }
    void Teleport(float x, float y, float z)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "teleport %g,%g,%g; ", x, y, z);
        log += text;
    }
    void Send(const Message& message)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "send %d>%d t%d f%d d%d; ", message.sender, message.receiver, message.type, message.flags,
                message.data);
        log += text;
    }
};

OBL_TEST_CASE("staff_service_home.anyone_else_turns_to_the_serve_position")
{
    World world("BARMAN", "BAR1", 2);
    svc::SetHome(world.member, 12, world);
    OBL_CHECK_EQ(world.member.m_HomeID, 12);
    OBL_CHECK_EQ(world.log, std::string("object 12; face 10,20,30 from 1,2,3; "));
    OBL_CHECK_EQ(world.memberSprite.instance.m_direction, 0);
}

OBL_TEST_CASE("staff_service_home.the_cook_stands_at_the_grill_of_each_rotation")
{
    const char* const expected[] = {"object 12; teleport 1011,1848,5; ", "object 12; teleport 988,1852,5; ",
            "object 12; teleport 988,1874,5; ", "object 12; teleport 1012,1874,5; "};
    const int32_t looks[] = {3, 1, 7, 5};
    for (int32_t rotation = 0; rotation < 4; ++rotation) {
        World world("a CHEF", "BURGER", rotation);
        svc::SetHome(world.member, 12, world);
        OBL_CHECK_EQ(world.log, std::string(expected[rotation]));
        OBL_CHECK_EQ(world.memberSprite.instance.m_direction, looks[rotation]);
        OBL_CHECK_EQ(world.member.m_HomeID, 12);
    }
    World narrow("CHEF", "BURGER", 2, 4); // the animation has four directions: the last one
    svc::SetHome(narrow.member, 12, narrow);
    OBL_CHECK_EQ(narrow.memberSprite.instance.m_direction, 3);
}

OBL_TEST_CASE("staff_service_home.the_dj_of_the_first_club_is_placed_by_the_footprint")
{
    World world("DJ", "CLUB1", 1);
    world.behavior = BuildingBehavior::DjClubFootprint;
    svc::SetHome(world.member, 12, world);
    OBL_CHECK_EQ(world.log, std::string("object 12; footprint -206,-382 r1; teleport 640,480,0; send 77>77 t57 f0 d0; "));
    OBL_CHECK_EQ(world.memberSprite.instance.m_direction, 1); // (6 - 1) % 4
}

OBL_TEST_CASE("staff_service_home.the_dj_of_the_other_clubs_is_placed_by_offsets")
{
    const char* const offsetPlaces[] = {"teleport 892,1576,0", "teleport 956,1732,0", "teleport 1108,1668,0", "teleport 1042,1514,0"};
    const char* const other[] = {"teleport 1064,1676,0", "teleport 950,1662,0", "teleport 934,1776,0", "teleport 1051,1792,0"};
    const int32_t offsetLooks[] = {2, 0, 6, 4}, otherLooks[] = {4, 2, 0, 6};
    for (int32_t rotation = 0; rotation < 4; ++rotation) {
        World second("DJ", "CLUB2", rotation);
        second.behavior = BuildingBehavior::DjClubOffset;
        svc::SetHome(second.member, 12, second);
        OBL_CHECK_EQ(second.log, std::string("object 12; ") + offsetPlaces[rotation] + "; send 77>77 t57 f0 d0; ");
        OBL_CHECK_EQ(second.memberSprite.instance.m_direction, offsetLooks[rotation]);
        World third("DJ", "CLUB3", rotation); // a name is only a label: the behavior stays None, so the other offsets
        svc::SetHome(third.member, 12, third);
        OBL_CHECK_EQ(third.log, std::string("object 12; ") + other[rotation] + "; send 77>77 t57 f0 d0; ");
        OBL_CHECK_EQ(third.memberSprite.instance.m_direction, otherLooks[rotation]);
    }
}

OBL_TEST_CASE("staff_service_home.a_club_name_without_the_behavior_is_no_deck")
{
    World world("DJ", "CLUB1", 1); // the behavior is None: the name does not place the DJ at the decks
    svc::SetHome(world.member, 12, world);
    OBL_CHECK_EQ(world.log, std::string("object 12; teleport 950,1662,0; send 77>77 t57 f0 d0; "));
}

OBL_TEST_CASE("staff_service_home.the_member_name_decides_and_dj_wins_over_chef")
{
    World both("CHEF DJ", "CLUB2", 0);
    both.behavior = BuildingBehavior::DjClubOffset;
    svc::SetHome(both.member, 12, both);
    OBL_CHECK(both.log.find("send") != std::string::npos);
    World lower("dj chef", "CLUB2", 0); // the case does not matter
    lower.behavior = BuildingBehavior::DjClubOffset;
    svc::SetHome(lower.member, 12, lower);
    OBL_CHECK(lower.log.find("send") != std::string::npos);
}

} // namespace
