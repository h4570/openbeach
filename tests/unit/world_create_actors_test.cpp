#include "unit_test.h"

#include "game/objects/world_create_actors.h"
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
    object::Base* CreateObjectByKey(const char* key, ObjectType type)
    {
        calls.push_back(std::string("create ") + key + " " + std::to_string(static_cast<int>(type)));
        return &object;
    }
    object::Base* CreatePerformerObject()
    {
        calls.push_back("create-performer");
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
    void SetHomePoint(object::Base*, float x, float y, float z)
    {
        calls.push_back("homepoint " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
    }
    void SetBuilderNumber(object::Base*, int number) { calls.push_back("number " + std::to_string(number)); }
    void SetSprite(object::Base*, const char* name) { calls.push_back(std::string("sprite ") + name); }
    void SetBounds(object::Base*, const int (&w)[4])
    {
        calls.push_back(
                "bounds " + std::to_string(w[0]) + " " + std::to_string(w[1]) + " " + std::to_string(w[2]) + " " + std::to_string(w[3]));
    }
    void SetInitParam(object::Base*, int value) { calls.push_back("init " + std::to_string(value)); }
    void SetVariant(object::Base*, int variant) { calls.push_back("variant " + std::to_string(variant)); }
    void AttachToBuilding(object::Base*, int building) { calls.push_back("attach " + std::to_string(building)); }
    void Post(obl::messages::Message& message)
    {
        posted = message;
        calls.push_back("post");
    }
};

obl::object::BuilderCreateParams BuilderRecord()
{
    obl::object::BuilderCreateParams record{};
    std::strcpy(record.m_key, "BUILDER_X");
    record.m_pos[0] = 1.5f;
    record.m_pos[1] = -2.0f;
    record.m_pos[2] = 0.25f;
    record.m_homeId = 42;
    record.m_builderNumber = 3;
    return record;
}

obl::object::PerformerCreateParams PerformerRecord()
{
    obl::object::PerformerCreateParams record{};
    record.m_variant = 2;
    record.m_pos[0] = 1.5f;
    record.m_pos[1] = -2.0f;
    record.m_pos[2] = 0.25f;
    record.m_buildingId = 9;
    return record;
}

obl::object::SharkCreateParams SharkRecord()
{
    obl::object::SharkCreateParams record{};
    std::strcpy(record.m_key, "SHARK_X");
    record.m_initParam = 5;
    for (int i = 0; i < 4; ++i) record.m_bounds[i] = 10 + i;
    record.m_pos[0] = 1.5f;
    record.m_pos[1] = -2.0f;
    record.m_pos[2] = 0.25f;
    return record;
}

} // namespace

OBL_TEST_CASE("world_create_actors.the_builder_is_the_staff_template_plus_home_point_and_number")
{
    FakeEnv env;
    object::Base* result = CreateBuilder(env, BuilderRecord());
    OBL_CHECK(result == &env.object);
    const std::vector<std::string> expected = {"lookup BUILDER_X", "create BUILDER_X 10", "type 10", "state 0",
            "teleport 1.500000 -2.000000 0.250000", "homepoint 1.500000 -2.000000 0.250000", "home 42", "number 3", "post"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK_EQ(env.posted.sender, -2);
    OBL_CHECK_EQ(env.posted.receiver, 77);
    OBL_CHECK_EQ(env.posted.type, 3);
    OBL_CHECK_EQ(env.posted.flags, 0);
    OBL_CHECK_EQ(env.posted.data, 1);
}

OBL_TEST_CASE("world_create_actors.the_shark_makes_its_sprite_and_bounds_before_the_class_and_state")
{
    FakeEnv env;
    object::Base* result = CreateShark(env, SharkRecord());
    OBL_CHECK(result == &env.object);
    const std::vector<std::string> expected = {"create SHARK_X 13", "sprite Shark", "bounds 10 11 12 13", "type 13", "state 0",
            "teleport 1.500000 -2.000000 0.250000", "init 5", "post"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK_EQ(env.posted.receiver, 77);
    OBL_CHECK_EQ(env.posted.data, 1);
}

OBL_TEST_CASE("world_create_actors.every_performer_has_its_class_number_and_setter_order")
{
    struct Row {
        PerformerKind kind;
        int type;
        bool typeFirst;
    };
    const Row rows[] = {{kWeightLifter, 16, false}, {kSinger, 15, false}, {kAerobic, 20, false}, {kCabaret, 14, true}, {kCatwalk, 17, true},
            {kWetTShirt, 18, true}};
    for (const Row& row : rows) {
        FakeEnv env;
        object::Base* result = CreatePerformer(env, PerformerRecord(), row.kind);
        OBL_CHECK(result == &env.object);
        const std::string type = "type " + std::to_string(row.type);
        const std::vector<std::string> expected = {"create-performer", "variant 2", row.typeFirst ? type : "state 0",
                row.typeFirst ? "state 0" : type, "teleport 1.500000 -2.000000 0.250000", "attach 9", "post"};
        OBL_CHECK(env.calls == expected);
    }
}

OBL_TEST_CASE("world_create_actors.the_receiver_id_is_read_after_the_last_setup_call")
{
    // The id of the receiver is the object's at the time of the Post: an Env whose last setup call changes it shows the order.
    struct RenumberingEnv : FakeEnv {
        void AttachToBuilding(object::Base* o, int building)
        {
            o->m_id = 99;
            FakeEnv::AttachToBuilding(o, building);
        }
        void SetInitParam(object::Base* o, int value)
        {
            o->m_id = 98;
            FakeEnv::SetInitParam(o, value);
        }
        void SetBuilderNumber(object::Base* o, int number)
        {
            o->m_id = 97;
            FakeEnv::SetBuilderNumber(o, number);
        }
    };
    {
        RenumberingEnv env;
        CreatePerformer(env, PerformerRecord(), kSinger);
        OBL_CHECK_EQ(env.posted.receiver, 99);
    }
    {
        RenumberingEnv env;
        CreateShark(env, SharkRecord());
        OBL_CHECK_EQ(env.posted.receiver, 98);
    }
    {
        RenumberingEnv env;
        CreateBuilder(env, BuilderRecord());
        OBL_CHECK_EQ(env.posted.receiver, 97);
    }
}
