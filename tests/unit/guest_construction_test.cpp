#include "core/char16.h"
#include "game/ui/sprite.h"
#include "unit_test.h"
#include "leak_ok.h"
#include "game/objects/guest_construction.h"
#include "object_storage.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::guest::construct;
using obl::object::Guest;
using obl::tables::GuestDef;

namespace {

struct FakeEnv {
    std::vector<int> draws; // scripted values handed out by Rand()
    size_t next = 0;
    std::vector<std::string> log;
    int32_t counts[6] = {};
    int32_t population = 0;
    ui::SpriteInstance sprites[2] = {};
    int spriteCalls = 0;
    const GuestDef* def = nullptr;
    int liveBlocks = 0;
    int32_t day = 3;
    float dayTime = 11.5f, gameTime = 40.25f;

    // What Person's constructor (Mover's, in the game) leaves in the log: its one draw, before the guest's constructor.
    void HumanBase()
    {
        log.push_back("human");
        Rand();
    }
    const GuestDef* Definition(const void*)
    {
        log.push_back("definition");
        return def;
    }
    int Rand()
    {
        log.push_back("rand");
        return draws.empty() ? 0 : draws[next++ % draws.size()];
    }
    float GameTime() { return gameTime; }
    int32_t GameDay() { return day; }
    float DayTime() { return dayTime; }
    int32_t* Counts() { return counts; }
    int32_t& Population() { return population; }
    void* ScriptLookup(const char* name)
    {
        log.push_back(std::string("script ") + name);
        return reinterpret_cast<void*>(0x77);
    }
    uint32_t NameCount(int list) { return list == 2 ? 3 : 4; }
    const char* NameKey(int list, uint32_t index)
    {
        static const char* const kFirst[2][4] = {{"M0", "M1", "M2", "M3"}, {"F0", "F1", "F2", "F3"}};
        static const char* const kFamily[3] = {"S0", "S1", "S2"};
        return list == 2 ? kFamily[index] : kFirst[list][index];
    }
    void* New(size_t size)
    {
        ++liveBlocks;
        log.push_back("new");
        return LeakOk(::operator new(size));
    }
    void Delete(void* block)
    {
        --liveBlocks;
        log.push_back("delete");
        ::operator delete(block);
    }
    void Widen(const char* source, Char16* destination, size_t count)
    {
        for (size_t i = 0; i < count; ++i) destination[i] = static_cast<Char16>(source[i]);
    }
    const Char16* Lookup(const Char16* key)
    {
        static Text16 text[2];
        static int turn = 0;
        Text16& slot = text[turn++ % 2];
        slot = Text16(u"<") + key + u">";
        return slot.c_str();
    }
    void* CreateSprite(const char* name)
    {
        log.push_back(std::string("sprite ") + name);
        return &sprites[spriteCalls++ % 2];
    }
};

GuestDef MakeDef()
{
    GuestDef def{};
    for (int i = 0; i < 13; ++i) {
        def.m_Pref[i][0] = 0.1f * i;
        def.m_Pref[i][1] = 0.1f * i + 1.0f;
    }
    for (int i = 0; i < 7; ++i) {
        def.m_OpinionStart[i][0] = 0.2f;
        def.m_OpinionStart[i][1] = 0.9f;
        def.m_OpinionTol[i][0] = 0.1f * i;
        def.m_OpinionTol[i][1] = 0.1f * i + 0.05f;
    }
    def.m_NightsMin = 2;
    def.m_NightsMax = 6;
    def.m_SleepTime[0] = 22.0f;
    def.m_SleepTime[1] = 23.0f;
    def.m_WakeTime[0] = 7.0f;
    def.m_WakeTime[1] = 8.0f;
    def.m_Attractive = 0.5f;
    def.m_Money = 123.5f;
    def.m_SunburnTime = 9.0f;
    return def;
}

Guest* Build(const GuestDef& def, FakeEnv& env)
{
    static Guest guest;
    obl::unit::FillObject(guest, 0xcd);
    env.def = &def;
    env.HumanBase();
    return Construct(guest, nullptr, env);
}

} // namespace

OBL_TEST_CASE("guest_construct.lerp_is_one_rounding_per_step")
{
    OBL_CHECK_EQ(Lerp(2.0f, 4.0f, 0), 2.0f);
    OBL_CHECK_EQ(Lerp(2.0f, 4.0f, 32767), static_cast<float>(2.0 * (32767 * static_cast<double>(3.0518509447574615e-05f)) + 2.0));
    OBL_CHECK_EQ(Lerp(4.0f, 2.0f, 0), 4.0f); // an unsorted pair draws towards the first limit
}

OBL_TEST_CASE("guest_construct.slot_uses_the_constructor_limits_not_the_destructor_ones")
{
    OBL_CHECK_EQ(ConstructionSlot(0.25f, 0), 0);
    OBL_CHECK_EQ(ConstructionSlot(0.26f, 0), 0); // the float 0.26 is just below the double 0.26
    OBL_CHECK_EQ(ConstructionSlot(0.2601f, 0), 1);
    OBL_CHECK_EQ(ConstructionSlot(0.74f, 1), 5); // the float 0.74 is above the double 0.74
    OBL_CHECK_EQ(ConstructionSlot(0.739f, 1), 4);
    OBL_CHECK_EQ(ConstructionSlot(std::numeric_limits<float>::quiet_NaN(), 1), 3);
    // The destructor's limits (0.2 / 0.7) differ, so a guest at 0.25 leaves another slot than it entered.
    OBL_CHECK(guest_lifecycle::AttractionSlot(0.25f, 0) != ConstructionSlot(0.25f, 0));
}

OBL_TEST_CASE("guest_construct.draw_order_and_count")
{
    FakeEnv env;
    env.draws = {16384};
    const GuestDef def = MakeDef();
    Build(def, env);
    int rands = 0;
    for (const std::string& entry : env.log) rands += entry == "rand";
    OBL_CHECK_EQ(rands, 26); // the base constructor's one and the 25 of the guest
    OBL_CHECK_EQ(env.log.front(), std::string("human"));
    OBL_CHECK_EQ(env.log[2], std::string("definition")); // after the base constructor and its draw
    OBL_CHECK_EQ(env.liveBlocks, 1); // only the name remains
    OBL_CHECK_EQ(env.population, 1);
}

OBL_TEST_CASE("guest_construct.fields")
{
    FakeEnv env;
    env.draws = {0};
    const GuestDef def = MakeDef();
    Guest* guest = Build(def, env);
    OBL_CHECK(guest->m_definition == &def);
    OBL_CHECK_EQ(guest->m_person[3], def.m_Pref[3][0]);
    OBL_CHECK_EQ(guest->m_opinion[5], 0.2f);
    OBL_CHECK(std::memcmp(guest->m_Tolerance, def.m_OpinionTol.data(), sizeof(guest->m_Tolerance)) == 0);
    OBL_CHECK_EQ(guest->m_happiness, static_cast<float>(7 * static_cast<double>(0.2f) * static_cast<double>(0.14285715f)));
    OBL_CHECK_EQ(guest->m_NightsToStay, 2);
    OBL_CHECK_EQ(guest->m_DayArrived, 3);
    OBL_CHECK_EQ(guest->m_timeArrived, 11.5f);
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 40.25f);
    OBL_CHECK_EQ(guest->m_BedTime, 22.0f);
    OBL_CHECK_EQ(guest->m_WakeTime, 7.0f);
    OBL_CHECK_EQ(guest->m_cash, 123.5f);
    OBL_CHECK_EQ(guest->m_sunBurnLevel, 9.0f);
    OBL_CHECK_EQ(guest->m_oxygen, 100);
    OBL_CHECK_EQ(guest->m_desires[22], -1.0f);
    OBL_CHECK_EQ(guest->m_desires[0], 0.0f);
    OBL_CHECK_EQ(guest->m_Best, -2);
    OBL_CHECK_EQ(guest->m_StayingAt, -1);
    OBL_CHECK_EQ(guest->m_DestObjectID, -1);
    OBL_CHECK_EQ(guest->m_Thoughts[2], -1);
    OBL_CHECK_EQ(guest->m_missedPurchase, 1);
    OBL_CHECK_EQ(std::string(guest->m_WalkAnimation), std::string("D_WALK_NORMAL"));
    OBL_CHECK_EQ(std::string(guest->m_StandAnimation), std::string("D_STAND_NORMAL"));
    OBL_CHECK_EQ(env.sprites[0].m_animType, 3);
    OBL_CHECK_EQ(env.sprites[1].m_animType, 3);
    // Left to the allocation: the building the guest is in, the ring of the last buildings.
    OBL_CHECK_EQ(static_cast<uint32_t>(guest->m_CurID), 0xcdcdcdcdu);
    OBL_CHECK_EQ(guest->m_LastID[0], static_cast<int32_t>(0xcdcdcdcd));
    Text16 name = guest->m_name;
    OBL_CHECK(name == u"<M0> <S0>");
    env.Delete(guest->m_name);
}

OBL_TEST_CASE("guest_construct.name_picks_list_by_gender_and_family_from_the_third")
{
    FakeEnv env;
    GuestDef def = MakeDef();
    def.m_Gender = 1;
    // The two name draws come last: 1 (base) + 13 + 7 + 3 (nights, bed, wake) = 24 draws before the first key.
    env.draws.assign(26, 0);
    env.draws[24] = 2;
    env.draws[25] = 1;
    Guest* guest = Build(def, env);
    OBL_CHECK(Text16(guest->m_name) == u"<F2> <S1>");
    env.Delete(guest->m_name);
}

OBL_TEST_CASE("guest_construct.statistics_count_in_the_constructor_band")
{
    FakeEnv env;
    GuestDef def = MakeDef();
    def.m_Gender = 1;
    def.m_Attractive = 0.8f;
    Build(def, env);
    OBL_CHECK_EQ(env.counts[5], 1);
    OBL_CHECK_EQ(env.population, 1);
}

OBL_TEST_CASE("guest_construct.sleep_window_wrapping_midnight")
{
    // A bed window 23.5 .. 1.0 draws from 23.5 + (1 + 24 - 23.5) * s: with the top draw the result is past 24 and folds back.
    FakeEnv env;
    GuestDef def = MakeDef();
    def.m_SleepTime[0] = 23.5f;
    def.m_SleepTime[1] = 1.0f;
    env.draws.assign(26, 32767);
    Guest* guest = Build(def, env);
    const double scaled = 32767 * static_cast<double>(3.0518509447574615e-05f);
    const double value = ((1.0 + 24.0) - 23.5) * scaled + 23.5;
    OBL_CHECK(value >= 24.0);
    OBL_CHECK_EQ(guest->m_BedTime, static_cast<float>(value - 24.0));
    env.Delete(guest->m_name);

    FakeEnv low;
    low.draws.assign(26, 0);
    Guest* early = Build(def, low);
    OBL_CHECK_EQ(early->m_BedTime, 23.5f);
    low.Delete(early->m_name);
}

OBL_TEST_CASE("guest_construct.wrapping_wake_window_folds_the_bed_time")
{
    FakeEnv env;
    GuestDef def = MakeDef();
    def.m_SleepTime[0] = 25.0f;
    def.m_SleepTime[1] = 26.0f;
    def.m_WakeTime[0] = 22.0f;
    def.m_WakeTime[1] = 5.0f;
    env.draws.assign(26, 0);
    Guest* guest = Build(def, env);
    OBL_CHECK_EQ(guest->m_BedTime, 1.0f);
    OBL_CHECK_EQ(guest->m_WakeTime, 22.0f);
    env.Delete(guest->m_name);

    // A wake window that does not wrap leaves the bed time alone.
    FakeEnv plain;
    GuestDef flat = MakeDef();
    flat.m_SleepTime[0] = 25.0f;
    flat.m_SleepTime[1] = 26.0f;
    plain.draws.assign(26, 0);
    Guest* second = Build(flat, plain);
    OBL_CHECK_EQ(second->m_BedTime, 25.0f);
    plain.Delete(second->m_name);
}

OBL_TEST_CASE("guest_construct.nights_remainder_is_unsigned")
{
    FakeEnv env;
    GuestDef def = MakeDef();
    def.m_NightsMin = 5;
    def.m_NightsMax = 2; // span -2 as a 32 bit unsigned: the remainder is the draw itself
    env.draws.assign(26, 7);
    Guest* guest = Build(def, env);
    OBL_CHECK_EQ(guest->m_NightsToStay, 7 + 5);
    env.Delete(guest->m_name);
}
