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
#include "game/objects/object_scenery.h"
#include "game/objects/staff_profession_script_values.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::messages::Message;
using obl::object::Builder;
using obl::object::Building;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::Mechanic;
using obl::object::Rep;
using obl::object::SecurityGuard;
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
float Nan()
{
    return std::numeric_limits<float>::quiet_NaN();
}

// The member's own sprite plays an animation with four directions; a home building (id 7) with an entrance.
template<class T> struct Fixture {
    std::unique_ptr<T> member = std::make_unique<T>();
    std::unique_ptr<Building> building = std::make_unique<Building>();
    Message message{};
    SpriteAnim anim{};
    SpriteAnim* anims[1] = {&anim};
    Sprite spriteData{};
    SpriteInstance sprite{};

    Fixture()
    {
        obl::unit::FillObject<T>(*member);
        obl::unit::FillObject(*building);
        member->m_currentMessage = &message;
        member->m_HomeID = 7;
        member->m_sprite = &sprite;
        building->m_id = 7;
        building->m_type = object::ObjectType::Building;
        anim.m_directions = 4;
        spriteData.m_animCount = 1;
        spriteData.m_anims = anims;
        sprite.m_sprite = &spriteData;
    }
};

// A fake of both Envs of staff_profession_script_values.h that records what it was asked.
struct FakeEnv {
    object::Base* home = nullptr;
    std::vector<int32_t> baseKeys;
    std::vector<std::string> log;
    bool open = true;
    float dayTime = 12.0f, gameTime = 3.5f;
    char cleanerText[40] = {}, mechanicText[40] = {};
    void* created = reinterpret_cast<void*>(0x1234);

    object::Base* Object(int32_t id)
    {
        log.push_back("object " + std::to_string(id));
        return id == 7 ? home : nullptr;
    }
    bool IsOpen(object::Base*)
    {
        log.push_back("open");
        return open;
    }
    float GameDayTime() { return dayTime; }
    float GameTime() { return gameTime; }
    void PushThought(object::Staff&, uint32_t flags) { log.push_back("thought " + std::to_string(flags)); }
    char* CleanerText() { return cleanerText; }
    char* MechanicText() { return mechanicText; }
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
};

template<class T> FakeEnv EnvFor(Fixture<T>& f)
{
    FakeEnv env;
    env.home = f.building.get();
    return env;
}

// Every number: the base class is asked exactly when the dispatcher does not handle it, with the same number, and its answer is returned unchanged.
template<class T, class Get, class Set, class HandlesGet, class HandlesSet>
void CheckRouting(Fixture<T>& f, Get get, Set set, HandlesGet handlesGet, HandlesSet handlesSet)
{
    std::vector<int32_t> keys = {INT32_MIN, -1, -0x40, 0x7fffffff, 0x1000};
    for (int32_t key = 0; key <= 0x120; ++key) keys.push_back(key);
    for (int32_t key : keys) {
        FakeEnv getEnv = EnvFor(f);
        const ScriptValue value = get(*f.member, key, 0x1234, getEnv);
        OBL_CHECK_EQ(getEnv.baseKeys.empty(), handlesGet(key));
        if (!handlesGet(key)) {
            OBL_CHECK_EQ(value.type, key);
            OBL_CHECK_EQ(value.bits, 0x1234);
        }
        if (key >= 0x39 && key <= 0x3b) continue; // the rep reads their bits as a name: covered with texts below
        FakeEnv setEnv = EnvFor(f);
        set(*f.member, key, 0, ex::kTypeInt, 3, setEnv);
        OBL_CHECK_EQ(setEnv.baseKeys.empty(), handlesSet(key));
        if (!handlesSet(key)) OBL_CHECK_EQ(setEnv.baseKeys.size(), 1u);
    }
}

} // namespace

OBL_TEST_CASE("staff_profession_values.handled_numbers_are_exactly_the_ones_not_passed_to_the_base_class")
{
    Fixture<Cleaner> cleaner;
    CheckRouting(
            cleaner, [](auto& o, int32_t k, int32_t d, auto& e) { return staff::CleanerGetValue(o, k, d, e); },
            [](auto& o, int32_t k, int32_t d, int32_t t, int32_t b, auto& e) { staff::CleanerSetValue(o, k, d, t, b, e); },
            staff::CleanerHandlesGet, staff::CleanerHandlesSet);
    Fixture<Mechanic> mechanic;
    mechanic.member->m_DestObjectID = 7;
    CheckRouting(
            mechanic, [](auto& o, int32_t k, int32_t d, auto& e) { return staff::MechanicGetValue(o, k, d, e); },
            [](auto& o, int32_t k, int32_t d, int32_t t, int32_t b, auto& e) { staff::MechanicSetValue(o, k, d, t, b, e); },
            staff::MechanicHandlesGet, staff::MechanicHandlesSet);
    Fixture<Rep> rep;
    CheckRouting(
            rep, [](auto& o, int32_t k, int32_t d, auto& e) { return staff::RepGetValue(o, k, d, e); },
            [](auto& o, int32_t k, int32_t d, int32_t t, int32_t b, auto& e) { staff::RepSetValue(o, k, d, t, b, e); },
            staff::RepHandlesGet, staff::RepHandlesSet);
    Fixture<SecurityGuard> guard;
    CheckRouting(
            guard, [](auto& o, int32_t k, int32_t d, auto& e) { return staff::SecurityGetValue(o, k, d, e); },
            [](auto& o, int32_t k, int32_t d, int32_t t, int32_t b, auto& e) { staff::SecuritySetValue(o, k, d, t, b, e); },
            staff::SecurityHandlesGet, staff::SecurityHandlesSet);
    OBL_CHECK(staff::SecurityHandlesGet(0x85) && staff::SecurityHandlesGet(0x91) && staff::SecurityHandlesGet(0x93) &&
            !staff::SecurityHandlesGet(0x92));
    OBL_CHECK(!staff::SecurityHandlesGet(0x97) && !staff::SecurityHandlesGet(0xf8) && staff::SecurityHandlesGet(0xfc) &&
            !staff::SecurityHandlesGet(0xfd));
    OBL_CHECK(staff::RepSetsExternal(0x39) && !staff::RepSetsExternal(0x3a));
}

OBL_TEST_CASE("staff_profession_values.cleaner_fields_types_and_bin_texts")
{
    Fixture<Cleaner> f;
    FakeEnv env = EnvFor(f);
    auto& c = *f.member;
    c.m_ActionState = 9;
    c.m_litterFound = 1;
    c.m_toiletFound = 2;
    c.m_binFound = 3;
    c.m_urgentClean = 4;
    c.m_LitterDistance = 400;
    c.m_UrgentType = 5;
    c.m_walkpoint = 6;
    c.m_Wage = -7;
    c.m_lastTaskTime = 10.25f;
    c.m_BinPos[0] = -3.75f;
    c.m_BinPos[1] = 2147483648.0f; // past int: the low 32 bits of the 64-bit truncation
    const auto Get = [&](int32_t key) { return staff::CleanerGetValue(c, key, 0, env); };
    OBL_CHECK_EQ(Get(0x40).bits, 9);
    OBL_CHECK_EQ(Get(0x6a).bits & 0xff, 1);
    OBL_CHECK_EQ(Get(0x6e).bits & 0xff, 2); // the toilet flag is the middle byte, the bin flag the last
    OBL_CHECK_EQ(Get(0x6b).bits & 0xff, 3);
    OBL_CHECK_EQ(Get(0x78).bits & 0xff, 4);
    for (int32_t key : {0x6a, 0x6b, 0x6e, 0x78}) OBL_CHECK_EQ(Get(key).type, ex::kTypeByte);
    OBL_CHECK_EQ(Get(0x71).bits, 400);
    OBL_CHECK_EQ(Get(0x79).bits, 5);
    OBL_CHECK_EQ(Get(0x7a).bits, 6);
    OBL_CHECK_EQ(Get(0xb7).bits, -7);
    OBL_CHECK_EQ(Get(0x6f).type, ex::kTypeFloat);
    OBL_CHECK_EQ(Bits(Get(0x6f).bits), 1.75f);
    OBL_CHECK_EQ(Get(0x76).bits, -3);
    OBL_CHECK_EQ(Get(0x77).bits, INT32_MIN);
    // The bin animations: copied into the static buffer and handed out as an *int* holding its address.
    const ScriptValue full = Get(0x72);
    OBL_CHECK_EQ(full.type, ex::kTypeInt);
    OBL_CHECK_EQ(full.bits, TextBits(env.cleanerText));
    OBL_CHECK_EQ(std::string(env.cleanerText), "bin_emptyfullbin_anim");
    OBL_CHECK_EQ(Get(0x73).bits, TextBits(env.cleanerText));
    OBL_CHECK_EQ(std::string(env.cleanerText), "bin_emptybin_static");
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_profession_values.home_closed_asks_the_building")
{
    Fixture<Cleaner> f;
    FakeEnv env = EnvFor(f);
    env.open = true;
    OBL_CHECK_EQ(staff::CleanerGetValue(*f.member, 0xfc, 0, env).bits & 0xff, 0);
    env.open = false;
    OBL_CHECK_EQ(staff::CleanerGetValue(*f.member, 0xfc, 0, env).bits & 0xff, 1);
    OBL_CHECK_EQ(staff::CleanerGetValue(*f.member, 0xfc, 0, env).type, ex::kTypeByte);
    Fixture<SecurityGuard> g;
    FakeEnv guardEnv = EnvFor(g);
    guardEnv.open = false;
    OBL_CHECK_EQ(staff::SecurityGetValue(*g.member, 0xfc, 0, guardEnv).bits & 0xff, 1);
    OBL_CHECK_EQ(guardEnv.log.front(), "object 7");
}

OBL_TEST_CASE("staff_profession_values.idle_time_is_one_float_difference")
{
    Fixture<Mechanic> f;
    FakeEnv env = EnvFor(f);
    env.dayTime = 16777216.0f;
    f.member->m_lastTaskTime = 1.0f;
    OBL_CHECK_EQ(Bits(staff::MechanicGetValue(*f.member, 0x6f, 0, env).bits), 16777215.0f); // exact in double, then rounded once
    env.dayTime = Nan();
    OBL_CHECK(std::isnan(Bits(staff::MechanicGetValue(*f.member, 0x6f, 0, env).bits)));
}

OBL_TEST_CASE("staff_profession_values.mechanic_fields_and_is_building")
{
    Fixture<Mechanic> f;
    FakeEnv env = EnvFor(f);
    auto& m = *f.member;
    m.m_ActionState = 3;
    m.m_HomeID = 7;
    m.m_walkpoint = -2;
    m.m_foundRepair = 1;
    m.m_foundBetterJob = 0x80;
    m.m_repairType = 4;
    m.m_WateringPos[0] = 12.9f;
    m.m_WateringPos[1] = Nan();
    m.m_DestObjectID = 7;
    const auto Get = [&](int32_t key) { return staff::MechanicGetValue(m, key, 0, env); };
    OBL_CHECK_EQ(Get(0x40).bits, 3);
    OBL_CHECK_EQ(Get(0x70).bits, 7);
    OBL_CHECK_EQ(Get(0x7a).bits, -2);
    OBL_CHECK_EQ(Get(0xa9).bits & 0xff, 1);
    OBL_CHECK_EQ(Get(0xaa).bits & 0xff, 0x80);
    OBL_CHECK_EQ(Get(0xad).bits, 4);
    OBL_CHECK_EQ(Get(0xae).bits, 12);
    OBL_CHECK_EQ(Get(0xaf).bits, 0); // NaN truncates to the indefinite integer's low 32 bits
    OBL_CHECK_EQ(Get(0xab).bits & 0xff, 1);
    f.building->m_type = object::ObjectType::Scenery;
    OBL_CHECK_EQ(Get(0xab).bits & 0xff, 0);
    OBL_CHECK_EQ(Get(0xab).type, ex::kTypeByte);
    OBL_CHECK_EQ(Get(0x73).bits, TextBits(env.mechanicText));
    OBL_CHECK_EQ(std::string(env.mechanicText), "bin_emptybin_static");
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_profession_values.rep_reads")
{
    Fixture<Rep> f;
    FakeEnv env = EnvFor(f);
    auto& r = *f.member;
    f.message.sender = 41;
    f.message.data = 42;
    f.message.flags = 43;
    r.m_StopAction = 1;
    r.m_ActionState = 2;
    r.m_talking_id = 3;
    r.m_sentToGuest = 0xff;
    r.m_HomeID = 11;
    r.m_GuestOffSetPosX = -0.5f;
    r.m_GuestOffSetPosY = 99.99f;
    std::strcpy(r.m_CurrAnimation, "STAND_CHAT");
    const auto Get = [&](int32_t key) { return staff::RepGetValue(r, key, 0, env); };
    OBL_CHECK_EQ(Get(0x00).bits, 41);
    OBL_CHECK_EQ(Get(0x05).bits, 42);
    OBL_CHECK_EQ(Get(0x2b).bits, 43);
    OBL_CHECK_EQ(Get(0x32).type, ex::kTypeByte);
    OBL_CHECK_EQ(Get(0x32).bits & 0xff, 1);
    OBL_CHECK_EQ(Get(0x3d).type, ex::kTypeText);
    OBL_CHECK_EQ(std::string(ex::AsText(Get(0x3d).bits)), "STAND_CHAT");
    OBL_CHECK_EQ(Get(0x40).bits, 2);
    OBL_CHECK_EQ(Get(0x108).bits, 3);
    OBL_CHECK_EQ(Get(0x10c).type, ex::kTypeInt); // zero-extended, not a byte value
    OBL_CHECK_EQ(Get(0x10c).bits, 0xff);
    OBL_CHECK_EQ(Get(0x10d).bits, 0);
    OBL_CHECK_EQ(Get(0x10e).bits, 99);
    OBL_CHECK_EQ(Get(0x10f).bits, 11);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_profession_values.rep_talk_time_and_boost")
{
    Fixture<Rep> f;
    FakeEnv env = EnvFor(f);
    auto& r = *f.member;
    const auto Get = [&](int32_t key) { return staff::RepGetValue(r, key, 0, env); };
    r.m_MinTalkTime = 2;
    r.m_MaxTalkTime = 10;
    r.m_timeWithGuests = 3.9f;
    OBL_CHECK_EQ(Get(0x109).bits, 2 + 8 * 3);
    r.m_MinTalkTime = 0x7fffffff; // the difference and the product wrap
    r.m_MaxTalkTime = -0x7fffffff;
    OBL_CHECK_EQ(Get(0x109).bits, static_cast<int32_t>(0x7fffffffu + 2u * 3u));
    OBL_CHECK_EQ(Get(0x10a).type, ex::kTypeFloat);
    OBL_CHECK_EQ(Bits(Get(0x10a).bits), 3.9f);
    OBL_CHECK_EQ(Bits(Get(0x10b).bits), static_cast<float>(3.9f * static_cast<double>(0.8f)));
    r.m_timeWithGuests = 0.125f; // 0.125 * 0.8f lies just under 0.1f
    OBL_CHECK_EQ(Bits(Get(0x10b).bits), 0.1f);
    r.m_timeWithGuests = 0.12500001f;
    OBL_CHECK_EQ(Bits(Get(0x10b).bits), static_cast<float>(0.12500001f * static_cast<double>(0.8f)));
    r.m_timeWithGuests = Nan(); // unordered: the floor wins
    OBL_CHECK_EQ(Bits(Get(0x10b).bits), 0.1f);
    OBL_CHECK_EQ(Get(0x109).bits, r.m_MinTalkTime); // NaN truncates to 0 hours
    const int32_t signalling = 0x7f800001;
    std::memcpy(&r.m_timeWithGuests, &signalling, sizeof(signalling));
    OBL_CHECK_EQ(Get(0x10a).bits, signalling); // copied as a word
}

OBL_TEST_CASE("staff_profession_values.security_reads_and_the_misdemeanour_thought")
{
    Fixture<SecurityGuard> f;
    FakeEnv env = EnvFor(f);
    auto& g = *f.member;
    g.m_ActionState = 4;
    g.m_MisdemeanourPos[0] = 1.5f;
    g.m_MisdemeanourPos[1] = -2.5f;
    g.m_shouldReprimand = 1;
    g.m_shouldDetain = 2;
    g.m_doubleFine = 3;
    g.m_toleranceClass = 5;
    g.m_action = 6;
    g.m_offenderId = 7;
    g.m_fightWithId = 8;
    g.m_numMinorFinesToday = 9;
    g.m_numSeriousFinesToday = 10;
    g.m_minorFineAmount = 12.5f;
    g.m_seriousFineAmount = 50.0f;
    g.m_FineOffsetX = -7.9f;
    g.m_FineOffsetY = 7.9f;
    g.m_FineLevel = 2;
    f.building->m_EntrancePoint[0] = 100.7f;
    f.building->m_EntrancePoint[1] = -0.3f;
    const auto Get = [&](int32_t key) { return staff::SecurityGetValue(g, key, 0, env); };
    OBL_CHECK_EQ(Get(0x40).bits, 4);
    OBL_CHECK_EQ(Get(0x86).type, ex::kTypeFloat);
    OBL_CHECK_EQ(Bits(Get(0x86).bits), 1.5f);
    OBL_CHECK_EQ(Bits(Get(0x87).bits), -2.5f);
    OBL_CHECK_EQ(Get(0x88).bits & 0xff, 1);
    OBL_CHECK_EQ(Get(0x89).bits & 0xff, 2);
    OBL_CHECK_EQ(Get(0x93).bits & 0xff, 3);
    OBL_CHECK_EQ(Get(0x8a).bits, 5);
    OBL_CHECK_EQ(Get(0x8b).bits, 6);
    OBL_CHECK_EQ(Get(0x8c).bits, 7);
    OBL_CHECK_EQ(Get(0x8d).bits, 8);
    OBL_CHECK_EQ(Get(0x8e).bits, 9);
    OBL_CHECK_EQ(Get(0x8f).bits, 10);
    OBL_CHECK_EQ(Bits(Get(0x90).bits), 12.5f);
    OBL_CHECK_EQ(Bits(Get(0x91).bits), 50.0f);
    OBL_CHECK_EQ(Get(0x94).bits, -7);
    OBL_CHECK_EQ(Get(0x95).bits, 7);
    OBL_CHECK_EQ(Get(0x96).bits, 2);
    OBL_CHECK_EQ(Get(0xf9).bits, 100);
    OBL_CHECK_EQ(Get(0xfa).bits, 0);
    OBL_CHECK_EQ(Get(0xfb).bits, 7);
    // FoundMisdemeanour: a "yes" also pushes thought 0x20, a "no" does not.
    env.log.clear();
    g.m_misdemeanourFound = 0;
    OBL_CHECK_EQ(Get(0x85).bits & 0xff, 0);
    OBL_CHECK(env.log.empty());
    g.m_misdemeanourFound = 2;
    OBL_CHECK_EQ(Get(0x85).type, ex::kTypeByte);
    OBL_CHECK_EQ(Get(0x85).bits & 0xff, 2);
    OBL_CHECK_EQ(env.log.size(), 2u);
    OBL_CHECK_EQ(env.log[0], "thought 32");
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_profession_values.writes")
{
    Fixture<Cleaner> c;
    FakeEnv env = EnvFor(c);
    staff::CleanerSetValue(*c.member, 0x71, 0, ex::kTypeInt, 0x10001, env); // squared with the 32-bit wrap
    OBL_CHECK_EQ(c.member->m_LitterDistance, static_cast<int32_t>(0x10001u * 0x10001u));
    staff::CleanerSetValue(*c.member, 0x71, 0, ex::kTypeFloat, FloatBits(-3.7f), env);
    OBL_CHECK_EQ(c.member->m_LitterDistance, 9);
    staff::CleanerSetValue(*c.member, 0x78, 0, ex::kTypeInt, 0x1234, env); // the low byte whatever the type
    OBL_CHECK_EQ(c.member->m_urgentClean, 0x34);
    staff::CleanerSetValue(*c.member, 0x40, 0, ex::kTypeFloat, FloatBits(5.9f), env);
    OBL_CHECK_EQ(c.member->m_ActionState, 5);
    staff::CleanerSetValue(*c.member, 0x79, 0, ex::kTypeInt, 8, env);
    staff::CleanerSetValue(*c.member, 0x7a, 0, ex::kTypeInt, 9, env);
    OBL_CHECK_EQ(c.member->m_UrgentType, 8);
    OBL_CHECK_EQ(c.member->m_walkpoint, 9);

    Fixture<Mechanic> m;
    staff::MechanicSetValue(*m.member, 0xa9, 0, ex::kTypeFloat, FloatBits(1.0f), env); // 0x3f800000: low byte 0
    OBL_CHECK_EQ(m.member->m_foundRepair, 0);
    staff::MechanicSetValue(*m.member, 0xaa, 0, ex::kTypeByte, 0xff01, env);
    OBL_CHECK_EQ(m.member->m_foundBetterJob, 1);
    staff::MechanicSetValue(*m.member, 0x40, 0, ex::kTypeInt, -4, env);
    OBL_CHECK_EQ(m.member->m_ActionState, -4);

    Fixture<SecurityGuard> g;
    staff::SecuritySetValue(*g.member, 0x92, 0, ex::kTypeInt, -1, env); // PatrolVelocity: the number read as unsigned
    OBL_CHECK_EQ(g.member->m_maxVel, 4294967296.0f);
    staff::SecuritySetValue(*g.member, 0x92, 0, ex::kTypeFloat, FloatBits(2.5f), env);
    OBL_CHECK_EQ(g.member->m_maxVel, 2.0f);
    staff::SecuritySetValue(*g.member, 0x40, 0, ex::kTypeInt, 3, env);
    OBL_CHECK_EQ(g.member->m_ActionState, 3);

    Fixture<Rep> r;
    staff::RepSetValue(*r.member, 0x32, 0, ex::kTypeInt, 0x101, env);
    OBL_CHECK_EQ(r.member->m_StopAction, 1);
    staff::RepSetValue(*r.member, 0x10c, 0, ex::kTypeInt, 0x200, env);
    OBL_CHECK_EQ(r.member->m_sentToGuest, 0);
    for (int32_t key : {0x3e, 0x3f, 0x40, 0x108}) staff::RepSetValue(*r.member, key, 0, ex::kTypeInt, key, env);
    OBL_CHECK_EQ(r.member->m_AttachedOffsetX, 0x3e);
    OBL_CHECK_EQ(r.member->m_AttachedOffsetY, 0x3f);
    OBL_CHECK_EQ(r.member->m_ActionState, 0x40);
    OBL_CHECK_EQ(r.member->m_talking_id, 0x108);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_profession_values.facing_is_kept_below_the_direction_count_only")
{
    Fixture<SecurityGuard> f;
    FakeEnv env = EnvFor(f);
    staff::SecuritySetValue(*f.member, 0x38, 0, ex::kTypeInt, 2, env);
    OBL_CHECK_EQ(f.sprite.m_direction, 2);
    staff::SecuritySetValue(*f.member, 0x38, 0, ex::kTypeInt, 9, env);
    OBL_CHECK_EQ(f.sprite.m_direction, 3);
    staff::SecuritySetValue(*f.member, 0x38, 0, ex::kTypeInt, -5, env); // no wrap, unlike the guest's
    OBL_CHECK_EQ(f.sprite.m_direction, -5);
    Fixture<Cleaner> c;
    staff::CleanerSetValue(*c.member, 0x38, 0, ex::kTypeFloat, FloatBits(3.99f), env);
    OBL_CHECK_EQ(c.sprite.m_direction, 3);
    Fixture<Mechanic> m;
    staff::MechanicSetValue(*m.member, 0x38, 0, ex::kTypeInt, 4, env);
    OBL_CHECK_EQ(m.sprite.m_direction, 3);
    Fixture<Rep> r;
    staff::RepSetValue(*r.member, 0x38, 0, ex::kTypeInt, 1, env);
    OBL_CHECK_EQ(r.sprite.m_direction, 1);
}

OBL_TEST_CASE("staff_profession_values.rep_attached_sprite")
{
    Fixture<Rep> f;
    FakeEnv env = EnvFor(f);
    auto& r = *f.member;
    const auto Set = [&](int32_t key, const char* text, int32_t type = ex::kTypeText) {
        staff::RepSetValue(r, key, 0, type, TextBits(text), env);
    };
    // Nothing attached: NONE, an animation and the facing do nothing.
    Set(0x39, "none");
    Set(0x3a, "STAND");
    Set(0x3b, "MyDir");
    OBL_CHECK(env.log.empty());
    OBL_CHECK(r.m_attachedSprite == nullptr);
    // A name creates one (any type: the bits are the name); a second name creates another over the first without releasing it.
    Set(0x39, "rep_clipboard", ex::kTypeInt);
    OBL_CHECK(r.m_attachedSprite == env.created);
    OBL_CHECK_EQ(env.log.back(), "create rep_clipboard");
    Set(0x39, "rep_badge");
    OBL_CHECK_EQ(env.log.size(), 2u);
    OBL_CHECK_EQ(env.log.back(), "create rep_badge");
    // The animation is the name as it is, from the game clock.
    Set(0x3a, "Reverse_TALK");
    OBL_CHECK_EQ(env.log.back(), "anim Reverse_TALK 3.500000");
    // MyDir (any case) turns a real attached sprite the way the rep faces, kept below its direction count.
    SpriteAnim attachedAnim{};
    attachedAnim.m_directions = 2;
    SpriteAnim* attachedAnims[1] = {&attachedAnim};
    Sprite attachedData{};
    attachedData.m_animCount = 1;
    attachedData.m_anims = attachedAnims;
    SpriteInstance attached{};
    attached.m_sprite = &attachedData;
    r.m_attachedSprite = &attached;
    f.sprite.m_direction = 1;
    Set(0x3b, "mydir");
    OBL_CHECK_EQ(attached.m_direction, 1);
    f.sprite.m_direction = 3;
    Set(0x3b, "MYDIR");
    OBL_CHECK_EQ(attached.m_direction, 1);
    f.sprite.m_direction = 0;
    Set(0x3b, "MyDirection");
    OBL_CHECK_EQ(attached.m_direction, 1);
    // NONE (any case) releases it.
    r.m_attachedSprite = env.created;
    Set(0x39, "NoNe");
    OBL_CHECK_EQ(env.log.back(), "release created");
    OBL_CHECK(r.m_attachedSprite == nullptr);
    OBL_CHECK(env.baseKeys.empty());
}
