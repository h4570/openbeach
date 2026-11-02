#include "unit_test.h"
#include "game/objects/person_methods.h"

#include <string>
#include <vector>

using namespace obl;
namespace {
using object::Person;
using script::ScriptValue;

struct FakeEnv {
    std::vector<std::string> calls;
    ScriptValue value{7, 9};
    bool ok = true;
    int32_t lastKey = 0, lastType = 0, lastBits = 0;
    intptr_t lastData = 0;
    const void* lastStream = nullptr;

    ScriptValue BaseGet(Person&, int32_t key, intptr_t data)
    {
        calls.push_back("get");
        lastKey = key;
        lastData = data;
        return value;
    }
    void BaseSet(Person&, int32_t key, intptr_t data, int32_t type, int32_t bits)
    {
        calls.push_back("set");
        lastKey = key;
        lastData = data;
        lastType = type;
        lastBits = bits;
    }
    bool BaseReceive(Person&, const void* message)
    {
        calls.push_back("receive");
        lastStream = message;
        return ok;
    }
};
} // namespace

OBL_TEST_CASE("person_methods.get_value_answers_no_value_itself")
{
    Person object{};
    FakeEnv env;
    const ScriptValue got = human::GetValue(object, 0x110, 1234, env);
    OBL_CHECK_EQ(got.type, 0x110);
    OBL_CHECK_EQ(got.bits, 1234);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("person_methods.get_value_passes_other_numbers_to_the_base_class")
{
    Person object{};
    FakeEnv env;
    for (int32_t key : {0, 0x10f, 0x111, -1}) {
        env.calls.clear();
        const ScriptValue got = human::GetValue(object, key, 5, env);
        OBL_CHECK_EQ(got.type, 7);
        OBL_CHECK_EQ(got.bits, 9);
        OBL_CHECK_EQ(env.calls.size(), size_t(1));
        OBL_CHECK_EQ(env.lastKey, key);
        OBL_CHECK_EQ(env.lastData, 5);
    }
}

OBL_TEST_CASE("person_methods.set_value_and_receive_forward_unchanged")
{
    Person object{};
    FakeEnv env;
    human::SetValue(object, 0x110, 1, 2, 3, env); // even the "no value" number goes to the base class when written
    OBL_CHECK_EQ(env.calls.size(), size_t(1));
    OBL_CHECK_EQ(env.lastKey, 0x110);
    OBL_CHECK_EQ(env.lastData, 1);
    OBL_CHECK_EQ(env.lastType, 2);
    OBL_CHECK_EQ(env.lastBits, 3);
    int stream = 0;
    OBL_CHECK(human::ReceiveMovementMessage(object, &stream, env));
    env.ok = false;
    OBL_CHECK(!human::ReceiveMovementMessage(object, &stream, env));
    OBL_CHECK_EQ(env.calls.size(), size_t(3));
}

namespace {

struct LifeEnv {
    std::vector<std::string> calls;
    bool updateOk = true;
    const char* fatalName = nullptr;
    // TrySelect
    int32_t drawX = 3, hotspotY = 5, pixelX = 7, pixelY = 11;
    bool hit = true;
    int32_t hitPoint[2] = {};
    float worldSeen[3] = {};

    bool BaseUpdate(Person&)
    {
        calls.push_back("update");
        return updateOk;
    }
    void Fatal(const char* name)
    {
        calls.push_back("fatal");
        fatalName = name;
    }
    void WorldToScreen(const float* world, int32_t* out)
    {
        calls.push_back("screen");
        for (int i = 0; i < 3; ++i) worldSeen[i] = world[i];
        out[0] = 100;
        out[1] = 200;
    }
    int32_t DrawOffsetX() { return drawX; }
    int32_t HotspotY() { return hotspotY; }
    void FirstOpaquePixel(int32_t& x, int32_t& y)
    {
        x = pixelX;
        y = pixelY;
    }
    bool HitTest(const int32_t* point, int32_t*)
    {
        hitPoint[0] = point[0];
        hitPoint[1] = point[1];
        return hit;
    }
    void Select() { calls.push_back("select"); }
};
} // namespace

OBL_TEST_CASE("person_methods.update_answers_one_and_reports_a_failing_base_update")
{
    Person object{};
    LifeEnv env;
    OBL_CHECK(human::Update(object, env));
    OBL_CHECK(env.calls.size() == size_t(1) && !env.fatalName);
    env.updateOk = false;
    OBL_CHECK(human::Update(object, env));
    OBL_CHECK(env.fatalName == object.m_Name);
}

OBL_TEST_CASE("person_methods.try_select_needs_a_visible_object_and_a_hit")
{
    Person object{};
    object.m_WorldXYZ[0] = 1.5f;
    object.m_WorldXYZ[1] = 2.5f;
    object.m_WorldXYZ[2] = 3.5f;
    int32_t screen[2] = {1, 2};
    LifeEnv env;
    OBL_CHECK(!human::TrySelectAtScreenPoint(object, screen, env));
    OBL_CHECK(env.calls.empty());
    object.m_visible = 1;
    OBL_CHECK(human::TrySelectAtScreenPoint(object, screen, env));
    OBL_CHECK(env.worldSeen[0] == 1.5f && env.worldSeen[2] == 3.5f);
    OBL_CHECK_EQ(env.hitPoint[0], 110);
    OBL_CHECK_EQ(env.hitPoint[1], 216);
    OBL_CHECK(env.calls.back() == "select");
    env.calls.clear();
    env.hit = false;
    OBL_CHECK(!human::TrySelectAtScreenPoint(object, screen, env));
    OBL_CHECK(env.calls.size() == size_t(1) && env.calls[0] == "screen");
}
