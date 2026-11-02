#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/objects/staff_construction.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::Mechanic;
using obl::object::Rep;
using obl::object::SecurityGuard;
using ui::Sprite;
using namespace obl::tables;
namespace cons = obl::staff::construct;
using cons::Kind;

namespace {

// The game as a script of answers; every effect is written to `log` in order.
struct FakeEnv {
    std::string log;
    StaffDef definition{};
    int32_t counters[8] = {};
    std::vector<int> draws{7, 5}; // answers of rand, in order
    size_t drawn = 0;
    uint32_t nameCount[2] = {3, 4};
    char names[2][8][8] = {{"M0", "M1", "M2"}, {"F0", "F1", "F2", "F3"}};
    std::deque<std::u16string> heap; // a deque: the pointers handed out must stay valid
    int sprite = 0;
    uint32_t dayBits = 0x7fa00000; // a signalling-looking pattern: copying the clock through a float register makes it quiet
    void* freed = nullptr;

    void add(const std::string& text) { log += (log.empty() ? "" : " ") + text; }

    void RecalculateHappiness(object::Staff&) { add("Recalculate"); }
    const StaffDef* StaffDefinition(const char*)
    {
        add("Definition");
        return &definition;
    }
    uint32_t GameDay() { return 41; }
    uint32_t DayTimeBits() { return dayBits; }
    int Rand()
    {
        add("Rand");
        return draws[drawn++ % draws.size()];
    }
    void* ScriptLookup(const char* name)
    {
        add(std::string("Script(") + name + ")");
        return reinterpret_cast<void*>(0x5c0de);
    }
    void* CreateSprite(const char* name)
    {
        add(std::string("Sprite(") + name + ")");
        return reinterpret_cast<void*>(static_cast<uintptr_t>(0x5000 + ++sprite));
    }
    void CopyKeyline(void*) { add("Keyline"); }
    void PlayStand(obl::object::ServiceStaff&) { add("Stand"); }
    void ReleaseSprite(void*) { add("ReleaseSprite"); }
    int32_t& Counter(Kind kind) { return counters[static_cast<int>(kind)]; }
    void UpdateStaffButtons(uint8_t type, bool present) { add("Buttons(" + std::to_string(type) + (present ? ",on)" : ",off)")); }
    void RemoveFeedbackByText(const char* key) { add(std::string("Complaint(") + key + ")"); }
    void RemoveFeedbackByObject(uint32_t id) { add("RemoveByObject(" + std::to_string(id) + ")"); }
    void RemoveBuilder(int32_t id) { add("RemoveBuilder(" + std::to_string(id) + ")"); }
    uint32_t FineBits(const char* key)
    {
        add(std::string("Fine(") + key + ")");
        return key[1] == 'I' ? 0x41200000 : 0x42c80000;
    }
    uint32_t NameCount(bool female) { return nameCount[female]; }
    const char* NameKey(bool female, uint32_t index)
    {
        add(std::string("Key(") + names[female][index] + ")");
        return names[female][index];
    }
    void* New(size_t size)
    {
        add("New(" + std::to_string(size) + ")");
        return ::operator new(size);
    }
    void Delete(void* memory)
    {
        add("Delete");
        freed = memory;
        ::operator delete(memory);
    }
    void Widen(const char* source, Char16* destination, size_t count)
    {
        for (size_t i = 0; i < count; ++i) destination[i] = static_cast<Char16>(source[i]);
    }
    const Char16* Lookup(const Char16* key)
    {
        std::u16string text(reinterpret_cast<const char16_t*>(key));
        add("Lookup");
        heap.push_back(text == u"F1" ? u"Fiona" : text == u"M2" ? u"Max" : u"[" + text + u"]");
        return reinterpret_cast<const Char16*>(heap.back().c_str());
    }
};

std::u16string NameOf(const object::Staff& staffer)
{
    return std::u16string(reinterpret_cast<const char16_t*>(cons::NameBuffer(staffer)));
}

void Free(object::Staff& staffer)
{
    ::operator delete(cons::NameBuffer(staffer));
}

// The memory is freed without running the derived destructor; these objects own nothing beyond the name buffer, which the
// destruct cases free themselves.
struct FreeWithoutDestructor {
    template<class T> void operator()(T* ptr) const { ::operator delete(static_cast<void*>(ptr)); }
};

template<class T> using Destructible = std::unique_ptr<T, FreeWithoutDestructor>;

OBL_TEST_CASE("staff_construct.name_key_is_formatted_like_swprintf")
{
    Char16 key[64];
    cons::FormatKey(key, "ID_REP_", 7);
    OBL_CHECK(std::u16string(reinterpret_cast<const char16_t*>(key)) == u"ID_REP_7");
    cons::FormatKey(key, "ID_X_", 31);
    OBL_CHECK(std::u16string(reinterpret_cast<const char16_t*>(key)) == u"ID_X_31");
    cons::FormatKey(key, "ID_X_", 0);
    OBL_CHECK(std::u16string(reinterpret_cast<const char16_t*>(key)) == u"ID_X_0");
    cons::FormatKey(key, "ID_X_", -12);
    OBL_CHECK(std::u16string(reinterpret_cast<const char16_t*>(key)) == u"ID_X_-12");
}

OBL_TEST_CASE("staff_construct.base_copies_the_definition")
{
    FakeEnv env;
    env.definition.m_Wage = 45;
    env.definition.m_RecommendedWage = static_cast<int>(0x80000000u);
    env.definition.m_StartHour = 8;
    env.definition.m_EndHour = 18;
    env.definition.m_initialHappiness = 0.75f;
    env.definition.m_expectationGrowth = 1.5f;
    env.definition.m_scale = 2.0f;
    auto staff = std::make_unique<object::Staff>();
    staff->m_HiredOn = 99;
    cons::Construct(*staff, "CLEANER", env);
    OBL_CHECK_EQ(env.log, std::string("Definition Recalculate"));
    OBL_CHECK_EQ(staff->m_Wage, 45);
    OBL_CHECK_EQ(staff->m_SalaryExpectation, 2147483648.0f); // read unsigned
    OBL_CHECK_EQ(staff->m_StartTime, 8);
    OBL_CHECK_EQ(staff->m_EndTime, 18);
    OBL_CHECK_EQ(staff->m_happiness, 0.75f);
    OBL_CHECK_EQ(staff->m_salaryExpectationGrowth, 1.5f);
    OBL_CHECK_EQ(staff->m_HiredOn, 42);
    OBL_CHECK_EQ(staff->m_GotoObject, -1);
    OBL_CHECK_EQ(staff->m_staffDefinition, &env.definition);
}

OBL_TEST_CASE("staff_construct.waiter_has_no_definition")
{
    FakeEnv env;
    auto staff = std::make_unique<object::Staff>();
    obl::unit::FillObject<object::Staff>(*staff, 0xa5);
    cons::Construct(*staff, "wAiTeR", env);
    OBL_CHECK_EQ(env.log, std::string("Recalculate"));
    OBL_CHECK_EQ(staff->m_Wage, 0);
    OBL_CHECK_EQ(staff->m_SalaryExpectation, 0.0f);
    OBL_CHECK_EQ(staff->m_IdleTimePerHour, 0);
    // The fields only a definition fills are left alone (in the game they keep their member initializers' zero).
    OBL_CHECK_EQ(staff->m_hoursWorkedInRow, 0xa5a5a5a5u);
    OBL_CHECK_EQ(staff->m_happinessChanging, 0xa5);
    OBL_CHECK_EQ(static_cast<int>(staff->m_Thoughts[2][0]), 0);
}

OBL_TEST_CASE("staff_construct.cleaner_follows_the_fixed_order")
{
    FakeEnv env;
    env.draws = {5, 5};
    env.definition.m_female = 1;
    env.definition.m_Sprite = "cleaner_spr";
    auto cleaner = std::make_unique<Cleaner>();
    cons::Construct(*cleaner, "CLEANER", env);
    cons::ConstructCleaner(*cleaner, "CLEANER", env);
    OBL_CHECK(env.log.find("Script(CleanerLogic) Buttons(11,on) Complaint(FEEDBACK_NO_CLEANERS) Sprite(cleaner_spr) Keyline Rand Key(F1) "
                           "New(6) Rand Lookup Lookup Delete New(") != std::string::npos);
    OBL_CHECK_EQ(env.counters[static_cast<int>(Kind::Cleaner)], 1);
    OBL_CHECK(NameOf(*cleaner) == u"Fiona [ID_CLEANER_6]");
    OBL_CHECK_EQ(cleaner->m_cleanBins, 1);
    OBL_CHECK_EQ(cleaner->m_FeedbackStatus, 0x2000007e);
    OBL_CHECK_EQ(cleaner->m_maxVel, 50.0f);
    OBL_CHECK_EQ(cleaner->m_maxForce, 30.0f);
    OBL_CHECK(cleaner->m_sprite == reinterpret_cast<void*>(0x5001));
    uint32_t quiet;
    std::memcpy(&quiet, &cleaner->m_lastTaskTime, 4);
    OBL_CHECK_EQ(quiet, 0x7fe00000u); // copying the clock through a float register sets the quiet bit
    Free(*cleaner);
}

OBL_TEST_CASE("staff_construct.second_cleaner_does_not_touch_the_button")
{
    FakeEnv env;
    env.counters[static_cast<int>(Kind::Cleaner)] = 2;
    auto cleaner = std::make_unique<Cleaner>();
    cons::Construct(*cleaner, "CLEANER", env);
    cons::ConstructCleaner(*cleaner, "CLEANER", env);
    OBL_CHECK(env.log.find("Buttons") == std::string::npos);
    OBL_CHECK_EQ(env.counters[static_cast<int>(Kind::Cleaner)], 3);
    Free(*cleaner);
}

OBL_TEST_CASE("staff_construct.names_come_from_the_list_of_the_gender")
{
    FakeEnv env;
    env.draws = {1, 0};
    auto mechanic = std::make_unique<Mechanic>();
    mechanic->m_JobMask = 0xf0;
    cons::Construct(*mechanic, "MECHANIC", env);
    cons::ConstructMechanic(*mechanic, "MECHANIC", env);
    OBL_CHECK(env.log.find("Key(M1)") != std::string::npos);
    OBL_CHECK_EQ(mechanic->m_JobMask, 7u);
    OBL_CHECK(NameOf(*mechanic) == u"[M1] [ID_MECHANIC_1]");
    Free(*mechanic);

    FakeEnv lifeguard;
    lifeguard.definition.m_female = 1;
    auto guard = std::make_unique<Lifeguard>();
    cons::Construct(*guard, "LIFEG_F", lifeguard);
    cons::ConstructLifeguard(*guard, "LIFEG_F", lifeguard);
    OBL_CHECK(lifeguard.log.find("Key(F") != std::string::npos);
    OBL_CHECK(lifeguard.log.find("Definition Recalculate Script(LifeGuardLogic)") != std::string::npos);
    Free(*guard);
}

OBL_TEST_CASE("staff_construct.rep_sprite_has_no_keyline_and_security_reads_the_fines")
{
    FakeEnv rep;
    auto representative = std::make_unique<Rep>();
    cons::Construct(*representative, "REP_M", rep);
    cons::ConstructRep(*representative, "REP_M", rep);
    OBL_CHECK(rep.log.find("Keyline") == std::string::npos);
    OBL_CHECK_EQ(representative->m_MaxTalkTime, 10);
    OBL_CHECK_EQ(representative->m_MinTalkTime, 3);
    OBL_CHECK_EQ(representative->m_timeWithGuests, 0.5f);
    Free(*representative);

    FakeEnv env;
    auto guard = std::make_unique<SecurityGuard>();
    cons::Construct(*guard, "SECURITYGUARD", env);
    cons::ConstructSecurity(*guard, "SECURITYGUARD", env);
    OBL_CHECK_EQ(guard->m_minorFineAmount, 10.0f); // key "MINOR": 'I' second letter
    OBL_CHECK_EQ(guard->m_seriousFineAmount, 100.0f);
    OBL_CHECK_EQ(guard->m_offenderId, -1);
    OBL_CHECK_EQ(guard->m_toleranceClass, 1);
    OBL_CHECK_EQ(guard->m_tolerance, 0.5f);
    OBL_CHECK_EQ(guard->m_maxVel, 50.0f);
    Free(*guard);
}

OBL_TEST_CASE("staff_construct.last_one_out_turns_the_button_off")
{
    FakeEnv env;
    env.counters[static_cast<int>(Kind::Mechanic)] = 2;
    auto mechanic = Destructible<Mechanic>(new Mechanic());
    mechanic->m_id = 17;
    cons::SetNameBuffer(*mechanic, static_cast<Char16*>(::operator new(8)));
    cons::DestructMechanic(*mechanic, env);
    OBL_CHECK_EQ(env.log, std::string("RemoveByObject(17) Delete"));
    OBL_CHECK_EQ(env.counters[static_cast<int>(Kind::Mechanic)], 1);
    env.log.clear();
    cons::SetNameBuffer(*mechanic, static_cast<Char16*>(::operator new(8)));
    cons::DestructMechanic(*mechanic, env);
    OBL_CHECK_EQ(env.log, std::string("Buttons(8,off) RemoveByObject(17) Delete"));
}

OBL_TEST_CASE("staff_construct.lifeguard_gives_back_its_attached_sprite")
{
    FakeEnv env;
    env.counters[static_cast<int>(Kind::Lifeguard)] = 1;
    auto guard = Destructible<Lifeguard>(new Lifeguard());
    guard->m_attachedSprite = reinterpret_cast<void*>(0x77);
    cons::SetNameBuffer(*guard, static_cast<Char16*>(::operator new(8)));
    cons::DestructLifeguard(*guard, env);
    OBL_CHECK_EQ(env.log, std::string("Buttons(7,off) ReleaseSprite Delete"));
    // A destructed object is not read again: the second case gets a fresh one.
    auto second = Destructible<Lifeguard>(new Lifeguard());
    env.counters[static_cast<int>(Kind::Lifeguard)] = 3;
    env.log.clear();
    cons::SetNameBuffer(*second, static_cast<Char16*>(::operator new(8)));
    cons::DestructLifeguard(*second, env);
    OBL_CHECK_EQ(env.log, std::string("Delete"));
}

OBL_TEST_CASE("staff_construct.builder_follows_the_fixed_order")
{
    FakeEnv env;
    env.draws = {5, 5};
    env.definition.m_StartHour = 6;
    env.definition.m_Sprite = "builder_spr";
    auto builder = std::make_unique<Builder>();
    obl::unit::FillObject<Builder>(*builder, 0xa5);
    cons::Construct(*builder, "BUILDER", env);
    cons::ConstructBuilder(*builder, "BUILDER", env);
    OBL_CHECK(env.log.find("Recalculate Script(BuilderLogic) Sprite(builder_spr) Keyline Buttons(10,on) Rand Key(M2) New(6) Rand "
                           "Lookup Lookup Delete New(") != std::string::npos);
    OBL_CHECK(env.log.find("Complaint") == std::string::npos);
    OBL_CHECK_EQ(env.counters[static_cast<int>(Kind::Builder)], 1);
    OBL_CHECK_EQ(builder->m_maxVel, 65.0f);
    OBL_CHECK_EQ(builder->m_maxForce, 65.0f);
    OBL_CHECK_EQ(builder->m_FeedbackStatus, 6);
    OBL_CHECK_EQ(builder->m_RequiredFeedback, 1);
    OBL_CHECK_EQ(builder->m_LastStartTime, 6);
    OBL_CHECK_EQ(builder->m_BuilderNumber, 0);
    OBL_CHECK_EQ(builder->m_JobType, 0);
    OBL_CHECK_EQ(builder->m_checkForJobs, 0);
    OBL_CHECK(NameOf(*builder) == u"Max [ID_BUILDER_6]");
    uint32_t quiet;
    std::memcpy(&quiet, &builder->m_lastTaskTime, 4);
    OBL_CHECK_EQ(quiet, 0x7fe00000u);
    Free(*builder);
}

OBL_TEST_CASE("staff_construct.builder_unregisters_before_the_count_goes_down")
{
    FakeEnv env;
    env.counters[static_cast<int>(Kind::Builder)] = 2;
    auto builder = Destructible<Builder>(new Builder());
    builder->m_id = 33;
    cons::SetNameBuffer(*builder, static_cast<Char16*>(::operator new(8)));
    cons::DestructBuilder(*builder, env);
    OBL_CHECK_EQ(env.log, std::string("RemoveBuilder(33) Delete"));
    OBL_CHECK_EQ(env.counters[static_cast<int>(Kind::Builder)], 1);
    env.log.clear();
    cons::SetNameBuffer(*builder, static_cast<Char16*>(::operator new(8)));
    cons::DestructBuilder(*builder, env);
    OBL_CHECK_EQ(env.log, std::string("RemoveBuilder(33) Buttons(10,off) Delete"));
}

OBL_TEST_CASE("staff_construct.service_staff_stands_only_when_its_sprite_asks")
{
    for (const char* sprite : {"BARMAN", "BARWOMAN", "CHEF", "CHEFFETTE", "WAITER", "barman"}) {
        FakeEnv env;
        env.definition.m_Sprite = sprite;
        auto service = std::make_unique<obl::object::ServiceStaff>();
        obl::unit::FillObject<obl::object::ServiceStaff>(*service, 0xa5);
        cons::Construct(*service, "BAR_M", env);
        cons::ConstructService(*service, "BAR_M", env);
        const bool stands = std::strcmp(sprite, "WAITER") != 0;
        // The script is looked up before the sprite exists; no counter, no button, no name buffer, no further draw.
        OBL_CHECK_EQ(env.log, std::string("Definition Recalculate Script(GuestLogic) Sprite(") + sprite + (stands ? ") Stand" : ")"));
        OBL_CHECK(service->m_logic == reinterpret_cast<void*>(0x5c0de));
        OBL_CHECK_EQ(service->m_actionState, 0);
    }
}

} // namespace
