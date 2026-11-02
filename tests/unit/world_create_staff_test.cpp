#include "unit_test.h"

#include "game/objects/world_create_staff.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_create_params.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_create;
using object::ObjectType;

namespace {

struct FakeEnv {
    std::vector<std::string> calls;
    object::Base object{};
    obl::messages::Message posted{};

    FakeEnv() { object.m_id = 77; }

    void LookUpStaff(const char* key) { calls.push_back(std::string("lookup ") + key); }
    object::Base* CreateObject(const obl::object::StaffCreateParams& record, ObjectType type)
    {
        calls.push_back("create " + std::string(record.m_key) + " " + std::to_string(static_cast<int>(type)));
        return &object;
    }
    void SetType(object::Base* o, ObjectType type)
    {
        OBL_CHECK(o == &object);
        calls.push_back("type " + std::to_string(static_cast<int>(type)));
    }
    void SetState(object::Base* o, int state)
    {
        OBL_CHECK(o == &object);
        calls.push_back("state " + std::to_string(state));
    }
    void Teleport(object::Base*, float x, float y, float z)
    {
        calls.push_back("teleport " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
    }
    void SetHome(object::Base*, int home) { calls.push_back("home " + std::to_string(home)); }
    void Post(obl::messages::Message& message)
    {
        posted = message;
        calls.push_back("post");
    }
};

obl::object::StaffCreateParams Record()
{
    obl::object::StaffCreateParams record{};
    std::strcpy(record.m_key, "STAFF_X");
    record.m_pos[0] = 1.5f;
    record.m_pos[1] = -2.0f;
    record.m_pos[2] = 0.25f;
    record.m_homeId = 42;
    return record;
}

} // namespace

OBL_TEST_CASE("world_create_staff.the_sequence_is_lookup_create_type_state_teleport_home_post")
{
    FakeEnv env;
    object::Base* result = CreateStaff(env, Record(), kCleaner);
    OBL_CHECK(result == &env.object);
    const std::vector<std::string> expected = {
            "lookup STAFF_X", "create STAFF_X 11", "type 11", "state 0", "teleport 1.500000 -2.000000 0.250000", "home 42", "post"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_create_staff.every_factory_has_its_class_number_and_first_state")
{
    struct Row {
        StaffKind kind;
        int type;
        int state;
    };
    const Row rows[] = {
            {kServiceStaff, 6, 0}, {kLifeguard, 7, 1}, {kMechanic, 8, 0}, {kCleaner, 11, 0}, {kSecurityGuard, 12, 0}, {kRep, 9, 0}};
    for (const Row& row : rows) {
        FakeEnv env;
        CreateStaff(env, Record(), row.kind);
        OBL_CHECK_EQ(env.calls[1], "create STAFF_X " + std::to_string(row.type));
        OBL_CHECK_EQ(env.calls[2], "type " + std::to_string(row.type));
        OBL_CHECK_EQ(env.calls[3], "state " + std::to_string(row.state));
    }
}

OBL_TEST_CASE("world_create_staff.the_message_goes_from_the_world_to_the_new_object_with_state_one")
{
    FakeEnv env;
    CreateStaff(env, Record(), kRep);
    OBL_CHECK_EQ(env.posted.sender, -2);
    OBL_CHECK_EQ(env.posted.receiver, 77);
    OBL_CHECK_EQ(env.posted.type, 3);
    OBL_CHECK_EQ(env.posted.flags, 0);
    OBL_CHECK_EQ(env.posted.data, 1);
}

OBL_TEST_CASE("world_create_staff.the_id_is_read_after_the_object_was_set_up")
{
    // The id of the receiver is the object's at the time of the Post, not when it was allocated: an Env whose SetHome changes it shows the order.
    struct RenumberingEnv : FakeEnv {
        void SetHome(object::Base* o, int home)
        {
            o->m_id = 99;
            FakeEnv::SetHome(o, home);
        }
    } env;
    CreateStaff(env, Record(), kMechanic);
    OBL_CHECK_EQ(env.posted.receiver, 99);
}
