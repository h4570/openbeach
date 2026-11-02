#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"

#include "game/objects/staff_set_variable.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::layout;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::Mechanic;
using obl::object::Rep;
using obl::object::SecurityGuard;

namespace {

// The game as a log: every call into it is written in order.
struct Env {
    std::string log;
    void PushThought(uint32_t flags) { add("Thought(" + std::to_string(flags) + ")"); }
    void RecalculateHappiness() { add("Recalc"); }
    void Fatal(const char* key) { add(std::string("Fatal(") + key + ")"); }
    void ApplyBeachMode(int32_t home, uint32_t mode) { add("Beach(" + std::to_string(home) + "," + std::to_string(mode) + ")"); }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

template<class T> std::unique_ptr<T> Member()
{
    auto member = std::make_unique<T>();
    obl::unit::FillObject<T>(*member);
    member->m_HomeID = 42;
    return member;
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

} // namespace

OBL_TEST_CASE("staff_set_variable.wage_rise_and_cut_are_compared_unsigned")
{
    struct Case {
        int32_t before;
        uint32_t value;
        uint32_t thought;
        int32_t after;
    };
    const Case cases[] = {{100, 150, 0x4000000, 150}, {100, 100, 0x8000000, 100}, {100, 50, 0x8000000, 50}, {100, 4, 0x8000000, 5},
            {100, 0, 0x8000000, 5}, {3, 4, 0x4000000, 5}, {-1, 10, 0x8000000, 10}, {10, 0xffffffffu, 0x4000000, -1}};
    for (const Case& c : cases) {
        auto member = Member<object::Staff>();
        member->m_Wage = c.before;
        Env env;
        staff::SetVariable(*member, "STAFF_WAGES", &c.value, env);
        OBL_CHECK_EQ(env.log, "Thought(" + std::to_string(c.thought) + ") Recalc");
        OBL_CHECK_EQ(member->m_Wage, c.after);
    }
}

OBL_TEST_CASE("staff_set_variable.wage_is_matched_by_its_eleven_character_prefix_and_other_keys_are_fatal")
{
    const uint32_t value = 20;
    {
        auto member = Member<object::Staff>();
        Env env;
        staff::SetVariable(*member, "STAFF_WAGES_EXTRA", &value, env);
        OBL_CHECK_EQ(env.log, std::string("Thought(67108864) Recalc"));
    }
    for (const char* key : {"STAFF_WAGE", "staff_wages", "", "CLEANER_BINS"}) {
        auto member = Member<object::Staff>();
        Env env;
        staff::SetVariable(*member, key, &value, env);
        OBL_CHECK_EQ(env.log, std::string("Fatal(") + key + ")");
        OBL_CHECK_EQ(member->m_Wage, 0);
    }
}

OBL_TEST_CASE("staff_set_variable.cleaner_sets_one_byte_per_keyword_and_hands_the_rest_to_the_base")
{
    const uint32_t value = 0x01020305; // only the low byte is read
    auto member = Member<Cleaner>();
    Env env;
    staff::SetVariableCleaner(*member, "CLEANER_TOILETS", &value, env);
    OBL_CHECK_EQ(member->m_cleanToilets, 5);
    OBL_CHECK_EQ(member->m_cleanBins, 0);
    OBL_CHECK_EQ(member->m_cleanLitter, 0);
    staff::SetVariableCleaner(*member, "CLEANER_BINS", &value, env);
    staff::SetVariableCleaner(*member, "CLEANER_LITTER", &value, env);
    OBL_CHECK_EQ(member->m_cleanBins, 5);
    OBL_CHECK_EQ(member->m_cleanLitter, 5);
    OBL_CHECK_EQ(env.log, std::string());
    const uint32_t wage = 77;
    staff::SetVariableCleaner(*member, "STAFF_WAGES", &wage, env);
    OBL_CHECK_EQ(env.log, "Thought(67108864) Recalc");
    OBL_CHECK_EQ(member->m_Wage, 77);
    Env other;
    staff::SetVariableCleaner(*member, "CLEANER_BIN", &wage, other);
    OBL_CHECK_EQ(other.log, std::string("Fatal(CLEANER_BIN)"));
}

OBL_TEST_CASE("staff_set_variable.lifeguard_maps_the_beach_flag_onto_the_tower_mode")
{
    auto member = Member<Lifeguard>();
    Env env;
    const uint32_t open = 1, closed = 0, odd = 0x100;
    staff::SetVariableLifeguard(*member, "STAFF_BEACH_OPEN", &open, env);
    staff::SetVariableLifeguard(*member, "STAFF_BEACH_OPEN", &closed, env);
    staff::SetVariableLifeguard(*member, "STAFF_BEACH_OPEN", &odd, env); // the whole word counts, not its low byte
    OBL_CHECK_EQ(env.log, std::string("Beach(42,0) Beach(42,1) Beach(42,0)"));
    Env other;
    staff::SetVariableLifeguard(*member, "STAFF_WAGES", &open, other);
    OBL_CHECK_EQ(other.log, std::string("Thought(67108864) Recalc"));
}

OBL_TEST_CASE("staff_set_variable.mechanic_keeps_the_other_job_bits")
{
    struct Key {
        const char* name;
        uint32_t bit;
    };
    const Key keys[] = {{"MECHANIC_REPAIR", 1}, {"MECHANIC_VANDALS", 2}, {"MECHANIC_WATER", 4}};
    for (const Key& key : keys) {
        for (uint32_t before : {0u, 7u, 5u, 0xfffffff8u, 0xffffffffu}) {
            for (uint32_t value : {0u, 1u, 2u, 3u, 0x1ffu, 0x100u}) {
                auto member = Member<Mechanic>();
                member->m_JobMask = before;
                Env env;
                staff::SetVariableMechanic(*member, key.name, &value, env);
                OBL_CHECK_EQ(member->m_JobMask, (before & ~key.bit) | ((value & 1) ? key.bit : 0));
                OBL_CHECK_EQ(env.log, std::string());
            }
        }
    }
    auto member = Member<Mechanic>();
    Env env;
    const uint32_t wage = 9;
    staff::SetVariableMechanic(*member, "STAFF_WAGES", &wage, env);
    OBL_CHECK_EQ(env.log, "Thought(67108864) Recalc");
}

OBL_TEST_CASE("staff_set_variable.rep_stores_the_guest_time_word_as_is")
{
    auto member = Member<Rep>();
    Env env;
    const uint32_t bits = Bits(12.5f);
    staff::SetVariableRep(*member, "REP_GUEST_TIME", &bits, env);
    OBL_CHECK_EQ(Bits(member->m_timeWithGuests), bits);
    const uint32_t nan = 0x7fc12345;
    staff::SetVariableRep(*member, "REP_GUEST_TIME", &nan, env);
    OBL_CHECK_EQ(Bits(member->m_timeWithGuests), nan);
    OBL_CHECK_EQ(env.log, std::string());
    staff::SetVariableRep(*member, "REP_GUEST", &bits, env);
    OBL_CHECK_EQ(env.log, std::string("Fatal(REP_GUEST)"));
}

OBL_TEST_CASE("staff_set_variable.security_harshness_is_five_steps_truncated_and_read_unsigned")
{
    struct Case {
        float slider;
        int32_t tolerance;
    };
    // slider * 5 truncated: 0.4f is just above 0.4 (x5 = 2.00000003), 0.6f just above 0.6 (3.0000001), -0.2f just below -0.2 (-1.00000003); a product beyond 64 bits
    // or NaN is the "integer indefinite" whose low 32 bits are 0.
    const Case cases[] = {{0.0f, 0}, {0.1f, 0}, {0.39f, 0}, {0.4f, 1}, {0.5f, 1}, {0.6f, 2}, {0.79f, 2}, {0.8f, 2}, {1.0f, 2}, {2.0f, 2},
            {-0.1f, 0}, {-0.19f, 0}, {-0.2f, 2}, {-5.0f, 2}, {1.0e30f, 0}, {std::numeric_limits<float>::quiet_NaN(), 0},
            {std::numeric_limits<float>::infinity(), 0}, {-std::numeric_limits<float>::infinity(), 0}};
    for (const Case& c : cases) {
        auto member = Member<SecurityGuard>();
        member->m_toleranceClass = 9;
        Env env;
        const uint32_t bits = Bits(c.slider);
        staff::SetVariableSecurity(*member, "SECURITY_HARSHNESS", &bits, env);
        OBL_CHECK_EQ(Bits(member->m_tolerance), bits);
        OBL_CHECK_EQ(member->m_toleranceClass, c.tolerance);
        OBL_CHECK_EQ(env.log, std::string());
    }
}

OBL_TEST_CASE("staff_set_variable.security_harshness_quiets_a_signaling_nan_like_a_float_load")
{
    auto member = Member<SecurityGuard>();
    Env env;
    const uint32_t signaling = 0x7f800001, negative = 0xff812345, quiet = 0x7fc00000, infinity = 0x7f800000;
    staff::SetVariableSecurity(*member, "SECURITY_HARSHNESS", &signaling, env);
    OBL_CHECK_EQ(Bits(member->m_tolerance), 0x7fc00001u);
    OBL_CHECK_EQ(member->m_toleranceClass, 0);
    staff::SetVariableSecurity(*member, "SECURITY_HARSHNESS", &negative, env);
    OBL_CHECK_EQ(Bits(member->m_tolerance), 0xffc12345u);
    staff::SetVariableSecurity(*member, "SECURITY_HARSHNESS", &quiet, env);
    OBL_CHECK_EQ(Bits(member->m_tolerance), quiet);
    staff::SetVariableSecurity(*member, "SECURITY_HARSHNESS", &infinity, env);
    OBL_CHECK_EQ(Bits(member->m_tolerance), infinity);
}
