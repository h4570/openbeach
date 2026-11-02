#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/objects/staff_call_function.h"

#include <cstring>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::world_data;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;
namespace call = obl::staff::call;

namespace {

struct FakeEnv {
    std::string log;
    void FindWanderPos() { log += "wander "; }
    void PushThought(uint32_t bits)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "thought(%x) ", bits);
        log += text;
    }
    void DirectToObject(int32_t id) { log += "direct(" + std::to_string(id) + ") "; }
    void Fatal(const char* text) { log += std::string("fatal(") + text + ") "; }
};

struct Member {
    std::unique_ptr<object::Staff> object = std::make_unique<object::Staff>();
    SpriteInstance instance;
    Sprite sprite;
    SpriteAnim anim;
    SpriteAnim* table[1];

    explicit Member(uint8_t directions = 8)
    {
        obl::unit::FillObject<object::Staff>(*object);
        std::memset(static_cast<void*>(&instance), 0, sizeof(instance));
        std::memset(static_cast<void*>(&sprite), 0, sizeof(sprite));
        std::memset(static_cast<void*>(&anim), 0, sizeof(anim));
        anim.m_directions = directions;
        table[0] = &anim;
        sprite.m_animCount = 1;
        sprite.m_anims = table;
        instance.m_sprite = &sprite;
        instance.m_direction = 1;
        object->m_sprite = &instance;
        object->m_GotoObject = 55;
        object->m_DirectStaff = 1;
    }
};

std::string Run(Member& member, int32_t id)
{
    FakeEnv env;
    call::CallFunction(*member.object, id, env);
    return env.log;
}

OBL_TEST_CASE("staff_call_function.the_shared_functions_call_the_helpers")
{
    Member member;
    OBL_CHECK_EQ(Run(member, 0x76), std::string("wander "));
    OBL_CHECK_EQ(Run(member, 0x79), std::string("thought(20000000) "));
    OBL_CHECK_EQ(Run(member, 0x7a), std::string("thought(40000000) "));
    OBL_CHECK_EQ(Run(member, 0xac), std::string("thought(1000000) "));
    OBL_CHECK_EQ(member.object->m_endedShift, 0);
    OBL_CHECK_EQ(Run(member, 0xad), std::string("thought(2000000) "));
    OBL_CHECK_EQ(member.object->m_endedShift, 1);
    OBL_CHECK_EQ(Run(member, 0xab), std::string("direct(55) "));
}

OBL_TEST_CASE("staff_call_function.idle_time_is_spent_down_to_zero")
{
    Member member;
    member.object->m_IdleTimePerHour = 2;
    OBL_CHECK_EQ(Run(member, 0xa8), std::string());
    OBL_CHECK_EQ(member.object->m_IdleTimePerHour, 1);
    Run(member, 0xa8);
    Run(member, 0xa8);
    OBL_CHECK_EQ(member.object->m_IdleTimePerHour, 0);
    member.object->m_IdleTimePerHour = -3; // the test is unsigned: only zero stays
    Run(member, 0xa8);
    OBL_CHECK_EQ(member.object->m_IdleTimePerHour, -4);
}

OBL_TEST_CASE("staff_call_function.the_directed_target_is_cleared")
{
    Member member;
    OBL_CHECK_EQ(Run(member, 0xa9), std::string());
    OBL_CHECK_EQ(member.object->m_DirectStaff, 0);
    OBL_CHECK_EQ(member.object->m_GotoObject, -1);
}

OBL_TEST_CASE("staff_call_function.the_sprite_faces_front_or_its_last_direction")
{
    Member wide(8);
    Run(wide, 0xaa);
    OBL_CHECK_EQ(wide.instance.m_direction, 4);
    Member five(5);
    Run(five, 0xaa);
    OBL_CHECK_EQ(five.instance.m_direction, 4);
    Member four(4);
    Run(four, 0xaa);
    OBL_CHECK_EQ(four.instance.m_direction, 3);
    Member one(1);
    Run(one, 0xaa);
    OBL_CHECK_EQ(one.instance.m_direction, 0);
}

OBL_TEST_CASE("staff_call_function.an_unrecognized_function_is_fatal")
{
    Member member;
    const std::string fatal = "fatal(A script called a function staff members do not have) ";
    for (int32_t id : {0x75, 0x77, 0x78, 0x7b, 0xa7, 0xae, 0, -1}) OBL_CHECK_EQ(Run(member, id), fatal);
    OBL_CHECK_EQ(member.object->m_GotoObject, 55);
}

} // namespace
