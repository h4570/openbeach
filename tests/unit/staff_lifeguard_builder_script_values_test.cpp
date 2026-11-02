#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/objects/object_building.h"
#include "game/objects/staff_lifeguard_builder_script_values.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::messages::Message;
using obl::object::Builder;
using obl::object::Building;
using obl::object::Cleaner;
using obl::object::Guest;
using obl::object::Lifeguard;
using obl::script::ScriptValue;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;
namespace ex = obl::script_program::exec;

namespace {

float Bits(int32_t bits)
{
    return script::BitsToFloat(bits);
}
int32_t FloatBits(float value)
{
    return script::FloatToBits(value);
}
int32_t TextBits(const char* text)
{
    return guest_script::TextBits(text);
}

// A fake of the Envs of staff_lifeguard_builder_script_values.h (and of the attached-sprite and thought parts it shares with the rep).
struct FakeEnv {
    std::map<int32_t, object::Base*> objects;
    std::vector<int32_t> baseKeys;
    std::vector<std::string> log;
    bool open = true;
    float dayTime = 12.0f, gameTime = 3.5f;
    char lifeguardText[40] = {};
    void* created = reinterpret_cast<void*>(0x1234);
    // The construction manager.
    int jobs = 0;
    int32_t assignResult = -1;
    uint32_t assignType = 1;
    int32_t workDirection = 0;
    int32_t location = 3;
    float entry[3] = {1.9f, -2.9f, 0};
    std::vector<int32_t> demolishing, building; // ids of objects with a demolition / building job
    int32_t whistling = 5;
    int assignCalls = 0;

    object::Base* Object(int32_t id)
    {
        log.push_back("object " + std::to_string(id));
        const auto it = objects.find(id);
        return it == objects.end() ? nullptr : it->second;
    }
    bool IsOpen(object::Base*) { return open; }
    float GameDayTime() { return dayTime; }
    float GameTime() { return gameTime; }
    void PushThought(object::Staff&, uint32_t flags) { log.push_back("thought " + std::to_string(flags)); }
    char* LifeguardText() { return lifeguardText; }
    ScriptValue Base(object::Staff&, int32_t key, intptr_t data)
    {
        baseKeys.push_back(key);
        return {key, static_cast<int32_t>(data)};
    }
    void Base(object::Staff&, int32_t key, intptr_t, int32_t, int32_t) { baseKeys.push_back(key); }
    int32_t FloatToIntWrap(int32_t floatBits) { return static_cast<int32_t>(Bits(floatBits)); }
    void* CreateSprite(const char* name)
    {
        log.push_back(std::string("create ") + name);
        return created;
    }
    void ReleaseSprite(void* sprite) { log.push_back(sprite == created ? "release created" : "release other"); }
    void SetAnim(void*, const char* name, float startTime, float*)
    {
        log.push_back(std::string("anim ") + name + " " + std::to_string(startTime));
    }

    int32_t CountJobs(int32_t mode)
    {
        log.push_back("count " + std::to_string(mode));
        return jobs;
    }
    int32_t AssignNearestJob(int32_t builder, const float pos[3], int32_t mode, uint32_t* type)
    {
        ++assignCalls;
        log.push_back("assign " + std::to_string(builder) + " " + std::to_string(pos[0]) + " " + std::to_string(mode));
        if (assignResult != -1) *type = assignType;
        return assignResult;
    }
    int32_t WorkDirection(int32_t buildingId, int32_t token)
    {
        log.push_back("direction " + std::to_string(buildingId) + " " + std::to_string(token));
        return workDirection;
    }
    int32_t BuilderLocation(int32_t) { return location; }
    void SetBuilderLocation(int32_t builder, int32_t where)
    {
        log.push_back("place " + std::to_string(builder) + " " + std::to_string(where));
    }
    void BuildEntryPoint(int32_t buildingId, int32_t token, const float pos[3], float out[3])
    {
        log.push_back("entry " + std::to_string(buildingId) + " " + std::to_string(token) + " " + std::to_string(pos[2]));
        for (int i = 0; i < 3; ++i) out[i] = entry[i];
    }
    bool HasBuildJob(object::Base* o) { return Has(building, o); }
    bool HasDemolishJob(object::Base* o) { return Has(demolishing, o); }
    int32_t& Whistling() { return whistling; }

private:
    static bool Has(const std::vector<int32_t>& ids, object::Base* o)
    {
        for (int32_t id : ids)
            if (o->m_id == static_cast<uint32_t>(id)) return true;
        return false;
    }
};

template<class T> struct Fixture {
    std::unique_ptr<T> member = std::make_unique<T>();
    Message message{};
    SpriteAnim anim{};
    SpriteAnim* anims[1] = {&anim};
    Sprite spriteData{};
    SpriteInstance sprite{};
    std::unique_ptr<Building> building = std::make_unique<Building>();
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();

    Fixture()
    {
        obl::unit::FillObject<T>(*member);
        obl::unit::FillObject(*building);
        obl::unit::FillObject<Guest>(*guest);
        member->m_currentMessage = &message;
        member->m_HomeID = 7;
        member->m_sprite = &sprite;
        member->m_id = 21;
        building->m_id = 7;
        building->m_type = object::ObjectType::Building;
        guest->m_id = 9;
        guest->m_type = object::ObjectType::Guest;
        anim.m_directions = 4;
        spriteData.m_animCount = 1;
        spriteData.m_anims = anims;
        sprite.m_sprite = &spriteData;
    }
    FakeEnv Env()
    {
        FakeEnv env;
        env.objects = {{7, building.get()}, {9, guest.get()}, {3, guest.get()}}; // 3: where the routing sweep's Set of SavingID points
        return env;
    }
};

template<class T, class Get, class Set, class HandlesGet, class HandlesSet>
void CheckRouting(Fixture<T>& f, Get get, Set set, HandlesGet handlesGet, HandlesSet handlesSet)
{
    std::vector<int32_t> keys = {INT32_MIN, -1, -0x40, 0x7fffffff, 0x1000};
    for (int32_t key = 0; key <= 0x120; ++key) keys.push_back(key);
    for (int32_t key : keys) {
        FakeEnv getEnv = f.Env();
        const ScriptValue value = get(*f.member, key, 0x1234, getEnv);
        OBL_CHECK_EQ(getEnv.baseKeys.empty(), handlesGet(key));
        if (!handlesGet(key)) {
            OBL_CHECK_EQ(value.type, key);
            OBL_CHECK_EQ(value.bits, 0x1234);
        }
        if (key >= 0x39 && key <= 0x3b) continue; // the attached sprite reads the bits as a name: covered with texts below
        FakeEnv setEnv = f.Env();
        set(*f.member, key, 0, ex::kTypeInt, 3, setEnv);
        OBL_CHECK_EQ(setEnv.baseKeys.empty(), handlesSet(key));
        if (!handlesSet(key)) OBL_CHECK_EQ(setEnv.baseKeys.size(), 1u);
    }
}

} // namespace

OBL_TEST_CASE("staff_lifeguard_builder_values.handled_numbers_are_exactly_the_ones_not_passed_to_the_base_class")
{
    Fixture<Lifeguard> lifeguard;
    lifeguard.member->m_saving_id = 9;
    CheckRouting(
            lifeguard, [](auto& o, int32_t k, int32_t d, auto& e) { return staff::LifeguardGetValue(o, k, d, e); },
            [](auto& o, int32_t k, int32_t d, int32_t t, int32_t b, auto& e) { staff::LifeguardSetValue(o, k, d, t, b, e); },
            staff::LifeguardHandlesGet, staff::LifeguardHandlesSet);
    Fixture<Builder> builder;
    builder.member->m_DestObjectID = 7;
    CheckRouting(
            builder,
            [](auto& o, int32_t k, int32_t d, auto& e) {
                e.jobs = 1;
                return staff::BuilderGetValue(o, k, d, e);
            },
            [](auto& o, int32_t k, int32_t d, int32_t t, int32_t b, auto& e) { staff::BuilderSetValue(o, k, d, t, b, e); },
            staff::BuilderHandlesGet, staff::BuilderHandlesSet);
    OBL_CHECK(!staff::LifeguardHandlesGet(0x38) && !staff::LifeguardHandlesGet(0xa5) && staff::LifeguardHandlesSet(0xa5));
    OBL_CHECK(staff::LifeguardHandlesGet(0xa6) && !staff::LifeguardHandlesGet(0xa9) && staff::LifeguardHandlesGet(0xe0) &&
            !staff::LifeguardHandlesGet(0xe1));
    OBL_CHECK(!staff::LifeguardHandlesSet(0xa6) && !staff::LifeguardHandlesSet(0xa7) && !staff::LifeguardHandlesSet(0xfc));
    OBL_CHECK(staff::LifeguardSetsExternal(0x39) && !staff::LifeguardSetsExternal(0x3a));
    OBL_CHECK(staff::BuilderHandlesGet(0x104) && staff::BuilderHandlesGet(0xfd) && !staff::BuilderHandlesGet(0xfc) &&
            !staff::BuilderHandlesGet(0x105));
    OBL_CHECK(staff::BuilderHandlesSet(0x101) && !staff::BuilderHandlesSet(0x102) && !staff::BuilderHandlesSet(0x105));
    OBL_CHECK(staff::BuilderGetsExternal(0xb0) && !staff::BuilderGetsExternal(0xb1));
}

OBL_TEST_CASE("staff_lifeguard_builder_values.lifeguard_fields_and_types")
{
    Fixture<Lifeguard> f;
    FakeEnv env = f.Env();
    auto& l = *f.member;
    l.m_StopAction = 7;
    l.m_ActionState = 9;
    l.m_saving = 1;
    l.m_saving_id = 9;
    l.m_inSea = 2;
    l.m_enteringSea = 3;
    l.m_timesPathBlocked = -4;
    l.m_inMyArea = 5;
    l.m_AtHotel = 6;
    l.m_RescueThenDeath = 8;
    const int32_t exitX = 0x7fc00001; // a NaN with a payload: copied as a word
    std::memcpy(&l.m_ExitPoint[0], &exitX, sizeof(exitX));
    l.m_ExitPoint[1] = -2.5f;
    std::strcpy(l.m_CurrAnimation, "STAND_IDLE");
    const auto Get = [&](int32_t key) { return staff::LifeguardGetValue(l, key, 0, env); };
    OBL_CHECK_EQ(Get(0x32).bits & 0xff, 7);
    OBL_CHECK_EQ(Get(0x40).bits, 9);
    OBL_CHECK_EQ(Get(0x99).bits & 0xff, 1);
    OBL_CHECK_EQ(Get(0x9b).bits & 0xff, 2);
    OBL_CHECK_EQ(Get(0x9c).bits & 0xff, 3);
    OBL_CHECK_EQ(Get(0xa3).bits & 0xff, 5);
    OBL_CHECK_EQ(Get(0xa4).bits & 0xff, 6);
    OBL_CHECK_EQ(Get(0xa7).bits & 0xff, 8);
    for (int32_t key : {0x32, 0x99, 0x9b, 0x9c, 0xa3, 0xa4, 0xa7, 0xa8, 0xfc}) OBL_CHECK_EQ(Get(key).type, ex::kTypeByte);
    OBL_CHECK_EQ(Get(0x9a).bits, 9);
    OBL_CHECK_EQ(Get(0x9d).bits, -4);
    OBL_CHECK_EQ(Get(0xa6).bits, 600);
    OBL_CHECK_EQ(Get(0xa6).type, ex::kTypeInt);
    OBL_CHECK_EQ(Get(0x97).type, ex::kTypeFloat);
    OBL_CHECK_EQ(Get(0x97).bits, exitX);
    OBL_CHECK_EQ(Bits(Get(0x98).bits), -2.5f);
    const ScriptValue text = Get(0x3d);
    OBL_CHECK_EQ(text.type, ex::kTypeText);
    OBL_CHECK_EQ(text.bits, TextBits(l.m_CurrAnimation));
    // The three animation names share one static buffer and come back as an *int* holding its address.
    const ScriptValue rescue = Get(0xa1);
    OBL_CHECK_EQ(rescue.type, ex::kTypeInt);
    OBL_CHECK_EQ(rescue.bits, TextBits(env.lifeguardText));
    OBL_CHECK_EQ(std::string(env.lifeguardText), "rescue");
    OBL_CHECK_EQ(Get(0xa2).bits, TextBits(env.lifeguardText));
    OBL_CHECK_EQ(std::string(env.lifeguardText), "rescue_swim");
    OBL_CHECK_EQ(Get(0xe0).bits, TextBits(env.lifeguardText));
    OBL_CHECK_EQ(std::string(env.lifeguardText), "D_STAND_SAD");
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_lifeguard_builder_values.lifeguard_reads_the_guest_being_saved")
{
    Fixture<Lifeguard> f;
    FakeEnv env = f.Env();
    auto& l = *f.member;
    l.m_saving_id = 9;
    l.m_WorldXYZ[0] = 10.0f;
    l.m_WorldXYZ[1] = 20.0f;
    l.m_WorldXYZ[2] = 3.0f;
    f.guest->m_WorldXYZ[0] = 7.0f;
    f.guest->m_WorldXYZ[1] = 16.0f;
    f.guest->m_WorldXYZ[2] = 3.0f;
    const auto Get = [&](int32_t key) { return staff::LifeguardGetValue(l, key, 0, env); };
    OBL_CHECK_EQ(Bits(Get(0x9e).bits), 7.0f);
    OBL_CHECK_EQ(Bits(Get(0x9f).bits), 16.0f);
    const ScriptValue distance = Get(0xa0);
    OBL_CHECK_EQ(distance.type, ex::kTypeFloat);
    OBL_CHECK_EQ(Bits(distance.bits), 5.0f); // 3-4-5 in the plane
    // The position words are copied as they are, a signalling NaN included.
    const int32_t snan = 0x7fa00000;
    std::memcpy(&f.guest->m_WorldXYZ[0], &snan, sizeof(snan));
    OBL_CHECK_EQ(Get(0x9e).bits, snan);
    // The state asked: exactly 1 counts.
    f.guest->m_ActionState = 1;
    OBL_CHECK_EQ(Get(0xa8).bits & 0xff, 1);
    f.guest->m_ActionState = 2;
    OBL_CHECK_EQ(Get(0xa8).bits & 0xff, 0);
    f.guest->m_ActionState = -1;
    OBL_CHECK_EQ(Get(0xa8).bits & 0xff, 0);
}

OBL_TEST_CASE("staff_lifeguard_builder_values.distance_sums_in_the_fixed_order_with_one_rounding")
{
    // Added z, y, x in double and rounded once: three terms where the order matters in the last float bits.
    const float point[3] = {1.0e7f, 3.3f, 1.0f};
    const float other[3] = {0.0f, 0.1f, -1.0e-3f};
    const double dx = double(point[0]) - double(other[0]), dy = double(point[1]) - double(other[1]),
                 dz = double(point[2]) - double(other[2]);
    OBL_CHECK_EQ(staff::detail::DistanceBetween(point, other), static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx)));
    // A difference of 3e38 per axis is finite as a double sum and as a float; two of them overflow float only at the final rounding.
    const float zero[3] = {0, 0, 0}, big[3] = {3.0e38f, 0.0f, 0.0f}, bigger[3] = {3.0e38f, 3.0e38f, 0.0f},
                nan[3] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    OBL_CHECK_EQ(staff::detail::DistanceBetween(big, zero), 3.0e38f);
    OBL_CHECK(std::isinf(staff::detail::DistanceBetween(bigger, zero)));
    OBL_CHECK_EQ(staff::detail::DistanceBetween(zero, zero), 0.0f);
    OBL_CHECK(std::isnan(staff::detail::DistanceBetween(zero, nan)));
}

OBL_TEST_CASE("staff_lifeguard_builder_values.lifeguard_home_closed_asks_the_building")
{
    Fixture<Lifeguard> f;
    FakeEnv env = f.Env();
    env.open = true;
    OBL_CHECK_EQ(staff::LifeguardGetValue(*f.member, 0xfc, 0, env).bits & 0xff, 0);
    env.open = false;
    OBL_CHECK_EQ(staff::LifeguardGetValue(*f.member, 0xfc, 0, env).bits & 0xff, 1);
}

OBL_TEST_CASE("staff_lifeguard_builder_values.lifeguard_set")
{
    Fixture<Lifeguard> f;
    FakeEnv env = f.Env();
    auto& l = *f.member;
    const auto Set = [&](int32_t key, int32_t type, int32_t bits) { staff::LifeguardSetValue(l, key, 0, type, bits, env); };
    Set(0x32, ex::kTypeInt, 0x1ff);
    OBL_CHECK_EQ(l.m_StopAction, 0xff); // the low byte whatever the type
    Set(0x40, ex::kTypeFloat, FloatBits(-3.9f));
    OBL_CHECK_EQ(l.m_ActionState, -3);
    Set(0x97, ex::kTypeInt, 3);
    OBL_CHECK_EQ(l.m_ExitPoint[0], 3.0f);
    Set(0x98, ex::kTypeByte, 0x1ff);
    OBL_CHECK_EQ(l.m_ExitPoint[1], 255.0f);
    Set(0x97, ex::kTypeInt, -1); // an int is converted as an unsigned number
    OBL_CHECK_EQ(l.m_ExitPoint[0], 4294967296.0f);
    Set(0x9a, ex::kTypeFloat, FloatBits(12.5f));
    OBL_CHECK_EQ(l.m_saving_id, 12);
    Set(0x9b, ex::kTypeInt, 0x100); // zero in the low byte
    OBL_CHECK_EQ(l.m_inSea, 0);
    Set(0x9c, ex::kTypeInt, 1);
    OBL_CHECK_EQ(l.m_enteringSea, 1);
    Set(0x9d, ex::kTypeInt, 77);
    OBL_CHECK_EQ(l.m_timesPathBlocked, 77);
    Set(0xa4, ex::kTypeInt, 1);
    OBL_CHECK_EQ(l.m_AtHotel, 1);
    Set(0xa5, ex::kTypeInt, 1);
    OBL_CHECK_EQ(l.m_DrawGuest, 1);
    OBL_CHECK(env.log.empty());
    OBL_CHECK(env.baseKeys.empty());
    // Saving: a "yes" makes the lifeguard think about it, a "no" is silent.
    Set(0x99, ex::kTypeByte, 1);
    OBL_CHECK_EQ(l.m_saving, 1);
    OBL_CHECK_EQ(env.log.size(), 1u);
    OBL_CHECK_EQ(env.log.back(), "thought 64");
    Set(0x99, ex::kTypeInt, 0x100); // the low byte is 0: no thought
    OBL_CHECK_EQ(l.m_saving, 0);
    OBL_CHECK_EQ(env.log.size(), 1u);
    OBL_CHECK(staff::LifeguardSetMayDraw(0x99, 1) && staff::LifeguardSetMayDraw(0x99, 0x1ff) && !staff::LifeguardSetMayDraw(0x99, 0x100) &&
            !staff::LifeguardSetMayDraw(0x9b, 1));
}

OBL_TEST_CASE("staff_lifeguard_builder_values.lifeguard_facing_and_attached_sprite_as_the_reps")
{
    Fixture<Lifeguard> f;
    FakeEnv env = f.Env();
    auto& l = *f.member;
    const auto Set = [&](int32_t key, const char* text, int32_t type = ex::kTypeText) {
        staff::LifeguardSetValue(l, key, 0, type, TextBits(text), env);
    };
    staff::LifeguardSetValue(l, 0x38, 0, ex::kTypeInt, 9, env); // MyDir: no wrap, clamped only from above
    OBL_CHECK_EQ(f.sprite.m_direction, 3);
    staff::LifeguardSetValue(l, 0x38, 0, ex::kTypeInt, -5, env);
    OBL_CHECK_EQ(f.sprite.m_direction, -5);
    // Nothing attached: NONE, an animation and the facing do nothing, and the base class never hears of them.
    Set(0x39, "none");
    Set(0x3a, "STAND");
    Set(0x3b, "MyDir");
    OBL_CHECK(env.log.empty());
    OBL_CHECK(l.m_attachedSprite == nullptr);
    Set(0x39, "lifeguard_ring", ex::kTypeInt);
    OBL_CHECK(l.m_attachedSprite == env.created);
    OBL_CHECK_EQ(env.log.back(), "create lifeguard_ring");
    Set(0x39, "lifeguard_flag"); // a second name creates another over the first without releasing it
    OBL_CHECK_EQ(env.log.size(), 2u);
    Set(0x3a, "Reverse_RESCUE");
    OBL_CHECK_EQ(env.log.back(), "anim Reverse_RESCUE 3.500000");
    SpriteAnim attachedAnim{};
    attachedAnim.m_directions = 2;
    SpriteAnim* attachedAnims[1] = {&attachedAnim};
    Sprite attachedData{};
    attachedData.m_animCount = 1;
    attachedData.m_anims = attachedAnims;
    SpriteInstance attached{};
    attached.m_sprite = &attachedData;
    l.m_attachedSprite = &attached;
    f.sprite.m_direction = 3;
    Set(0x3b, "MYDIR");
    OBL_CHECK_EQ(attached.m_direction, 1);
    f.sprite.m_direction = 0;
    Set(0x3b, "MyDirection");
    OBL_CHECK_EQ(attached.m_direction, 1);
    staff::LifeguardSetValue(l, 0x3e, 0, ex::kTypeFloat, FloatBits(-2.5f), env);
    staff::LifeguardSetValue(l, 0x3f, 0, ex::kTypeInt, 17, env);
    OBL_CHECK_EQ(l.m_AttachedOffsetX, -2);
    OBL_CHECK_EQ(l.m_AttachedOffsetY, 17);
    l.m_attachedSprite = env.created;
    Set(0x39, "NoNe");
    OBL_CHECK_EQ(env.log.back(), "release created");
    OBL_CHECK(l.m_attachedSprite == nullptr);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_lifeguard_builder_values.builder_fields_and_types")
{
    Fixture<Builder> f;
    FakeEnv env = f.Env();
    auto& b = *f.member;
    b.m_ActionState = 9;
    b.m_HomeID = 7;
    b.m_JobType = 1;
    b.m_BuilderNumber = 4;
    b.m_NearestValidTileX = -11;
    b.m_NearestValidTileY = 12;
    b.m_NumTimesPlayedAnimInRow = 13;
    b.m_WaitTillMorning = 14;
    b.m_Night = 15;
    b.m_InHut = 16;
    b.m_lastTaskTime = 10.25f;
    b.m_HomePoint[0] = -3.75f;
    b.m_HomePoint[1] = 2147483648.0f; // past int: the low 32 bits of the 64-bit truncation
    env.whistling = 99;
    env.location = 4;
    const auto Get = [&](int32_t key) { return staff::BuilderGetValue(b, key, 0, env); };
    OBL_CHECK_EQ(Get(0x40).bits, 9);
    OBL_CHECK_EQ(Get(0x70).bits, 7);
    OBL_CHECK_EQ(Get(0xb1).bits, 1);
    OBL_CHECK_EQ(Get(0xb6).bits, 4);
    OBL_CHECK_EQ(Get(0xfd).bits, -11);
    OBL_CHECK_EQ(Get(0xfe).bits, 12);
    OBL_CHECK_EQ(Get(0xff).bits, 13);
    OBL_CHECK_EQ(Get(0x100).bits, 14);
    OBL_CHECK_EQ(Get(0x101).bits, 99); // the global shared by all builders
    OBL_CHECK_EQ(Get(0x104).bits, 4);
    OBL_CHECK_EQ(Get(0x106).bits, 15);
    OBL_CHECK_EQ(Get(0x107).bits, 16);
    for (int32_t key : {0x40, 0x70, 0xb1, 0xb6, 0xfd, 0xfe, 0xff, 0x100, 0x101, 0x104, 0x106, 0x107, 0xf9, 0xfa})
        OBL_CHECK_EQ(Get(key).type, ex::kTypeInt);
    OBL_CHECK_EQ(Get(0xf9).bits, -3); // the saved home point, truncated
    OBL_CHECK_EQ(Get(0xfa).bits, INT32_MIN);
    OBL_CHECK_EQ(Get(0x6f).type, ex::kTypeFloat);
    OBL_CHECK_EQ(Bits(Get(0x6f).bits), 1.75f);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_lifeguard_builder_values.builder_default_facings")
{
    const int32_t expected[20] = {2, 6, 7, 3, 0, 1, 2, 4, 3, 1, 4, 6, 0, 2, 7, 3, 4, 1, 7, 3};
    for (int32_t i = 0; i < 20; ++i) OBL_CHECK_EQ(staff::BuilderDefaultFacing(i), expected[i]);
    OBL_CHECK_EQ(staff::BuilderDefaultFacing(-1), 0);
    OBL_CHECK_EQ(staff::BuilderDefaultFacing(20), 0);
    Fixture<Builder> f;
    FakeEnv env = f.Env();
    auto& b = *f.member;
    b.m_DestObjectID = 7;
    for (int32_t number = 0; number < 10; ++number) {
        b.m_BuilderNumber = number;
        // The manager's direction wins; none (0) falls back to the table by the builder's number.
        env.workDirection = 5;
        OBL_CHECK_EQ(staff::BuilderGetValue(b, 0xb4, 0, env).bits, 5);
        env.workDirection = 0;
        OBL_CHECK_EQ(staff::BuilderGetValue(b, 0xb4, 0, env).bits, expected[number]);
        OBL_CHECK_EQ(staff::BuilderGetValue(b, 0xb5, 0, env).bits, expected[number + 10]);
    }
    // The manager is asked with the target and the builder's number (its token).
    b.m_BuilderNumber = 3;
    env.log.clear();
    env.workDirection = 6;
    staff::BuilderGetValue(b, 0xb4, 0, env);
    OBL_CHECK_EQ(env.log.size(), 1u);
    OBL_CHECK_EQ(env.log.back(), "direction 7 3");
}

OBL_TEST_CASE("staff_lifeguard_builder_values.builder_entry_point_and_job_queries")
{
    Fixture<Builder> f;
    FakeEnv env = f.Env();
    auto& b = *f.member;
    b.m_DestObjectID = 7;
    b.m_BuilderNumber = 2;
    b.m_WorldXYZ[2] = 4.5f;
    env.entry[0] = 1.9f;
    env.entry[1] = -2.9f;
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0xb2, 0, env).bits, 1); // truncated toward zero
    OBL_CHECK_EQ(env.log.back(), "entry 7 2 4.500000");
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0xb3, 0, env).bits, -2);
    env.entry[0] = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0xb2, 0, env).bits, 0); // the float-to-int conversion of NaN is 0
    // AmIDemolishing / IsJobStillValid: a missing target is false without asking about jobs.
    env.demolishing = {};
    env.building = {};
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x102, 0, env).bits & 0xff, 0);
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x103, 0, env).bits & 0xff, 0);
    env.building = {7};
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x102, 0, env).bits & 0xff, 0);
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x103, 0, env).bits & 0xff, 1);
    env.demolishing = {7};
    env.building = {};
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x102, 0, env).bits & 0xff, 1);
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x103, 0, env).bits & 0xff, 1);
    b.m_DestObjectID = -1;
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x102, 0, env).bits & 0xff, 0);
    OBL_CHECK_EQ(staff::BuilderGetValue(b, 0x103, 0, env).bits & 0xff, 0);
    for (int32_t key : {0x102, 0x103}) OBL_CHECK_EQ(staff::BuilderGetValue(b, key, 0, env).type, ex::kTypeByte);
}

OBL_TEST_CASE("staff_lifeguard_builder_values.builder_look_for_nearest_job")
{
    Fixture<Builder> f;
    FakeEnv env = f.Env();
    auto& b = *f.member;
    b.m_DestObjectID = 33;
    b.m_JobType = 7;
    b.m_WorldXYZ[0] = 100.0f;
    // No job at all: only the count is asked, nothing changes.
    env.jobs = 0;
    ScriptValue v = staff::BuilderGetValue(b, 0xb0, 0, env);
    OBL_CHECK_EQ(v.type, ex::kTypeByte);
    OBL_CHECK_EQ(v.bits & 0xff, 0);
    OBL_CHECK_EQ(env.assignCalls, 0);
    OBL_CHECK_EQ(env.log.size(), 1u);
    OBL_CHECK_EQ(env.log.back(), "count 2");
    OBL_CHECK_EQ(b.m_DestObjectID, 33);
    OBL_CHECK_EQ(b.m_JobType, 7);
    // Jobs but none for this builder: the target becomes -1 (the type stays), the answer is false.
    env.jobs = 3;
    env.assignResult = -1;
    v = staff::BuilderGetValue(b, 0xb0, 0, env);
    OBL_CHECK_EQ(v.bits & 0xff, 0);
    OBL_CHECK_EQ(env.assignCalls, 1);
    OBL_CHECK_EQ(env.log.back(), "assign 21 100.000000 2");
    OBL_CHECK_EQ(b.m_DestObjectID, -1);
    OBL_CHECK_EQ(b.m_JobType, 7);
    // A job: the target and its type (0 build, 1 demolish) are kept, the answer is true.
    env.assignResult = 12;
    env.assignType = 1;
    v = staff::BuilderGetValue(b, 0xb0, 0, env);
    OBL_CHECK_EQ(v.bits & 0xff, 1);
    OBL_CHECK_EQ(b.m_DestObjectID, 12);
    OBL_CHECK_EQ(b.m_JobType, 1);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_lifeguard_builder_values.builder_set")
{
    Fixture<Builder> f;
    FakeEnv env = f.Env();
    auto& b = *f.member;
    const auto Set = [&](int32_t key, int32_t type, int32_t bits) { staff::BuilderSetValue(b, key, 0, type, bits, env); };
    Set(0x40, ex::kTypeFloat, FloatBits(7.9f));
    OBL_CHECK_EQ(b.m_ActionState, 7);
    Set(0xff, ex::kTypeInt, -3);
    OBL_CHECK_EQ(b.m_NumTimesPlayedAnimInRow, -3);
    Set(0x100, ex::kTypeByte, 0x1ff);
    OBL_CHECK_EQ(b.m_WaitTillMorning, 0xff);
    Set(0x101, ex::kTypeInt, 12);
    OBL_CHECK_EQ(env.whistling, 12); // the shared global, not a member
    Set(0x106, ex::kTypeInt, 1);
    OBL_CHECK_EQ(b.m_Night, 1);
    Set(0x107, ex::kTypeInt, 2);
    OBL_CHECK_EQ(b.m_InHut, 2);
    Set(0x38, ex::kTypeInt, 2);
    OBL_CHECK_EQ(f.sprite.m_direction, 2);
    OBL_CHECK(env.log.empty());
    OBL_CHECK(env.baseKeys.empty());
    // BuilderLocation: the member keeps the number whatever it is; the manager hears of 0..5 only; 4 adds a thought chosen by the job type.
    for (int32_t where : {0, 1, 2, 3, 5}) {
        env.log.clear();
        Set(0x104, ex::kTypeInt, where);
        OBL_CHECK_EQ(b.m_BuilderLocation, where);
        OBL_CHECK_EQ(env.log.size(), 1u);
        OBL_CHECK_EQ(env.log.back(), "place 21 " + std::to_string(where));
    }
    b.m_JobType = 0;
    env.log.clear();
    Set(0x104, ex::kTypeInt, 4);
    OBL_CHECK_EQ(env.log.size(), 2u);
    OBL_CHECK_EQ(env.log[0], "place 21 4");
    OBL_CHECK_EQ(env.log[1], "thought 8"); // building
    b.m_JobType = 1;
    env.log.clear();
    Set(0x104, ex::kTypeInt, 4);
    OBL_CHECK_EQ(env.log[1], "thought 16"); // demolition
    b.m_JobType = 2; // anything but 0 counts as demolition
    env.log.clear();
    Set(0x104, ex::kTypeInt, 4);
    OBL_CHECK_EQ(env.log[1], "thought 16");
    for (int32_t where : {6, -1, 100}) {
        env.log.clear();
        Set(0x104, ex::kTypeInt, where);
        OBL_CHECK_EQ(b.m_BuilderLocation, where);
        OBL_CHECK(env.log.empty());
    }
    env.log.clear();
    Set(0x104, ex::kTypeFloat, FloatBits(4.9f)); // a float is truncated first
    OBL_CHECK_EQ(b.m_BuilderLocation, 4);
    OBL_CHECK_EQ(env.log.size(), 2u);
    OBL_CHECK(staff::BuilderSetMayDraw(0x104, 4) && !staff::BuilderSetMayDraw(0x104, 5) && !staff::BuilderSetMayDraw(0x101, 4));
}
