#include "unit_test.h"

#include "game/world/world_power.h"
#include "importer/src/readers/building_behavior_keys.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace obl::world_power;

namespace {

struct FakeBuilding {
    int power = 0;
    bool closedForPower = false;
    bool buildJob = false;
    bool demolishJob = false;
    int output = 0;
    std::string key = "SHOP";
};

struct FakeEnv {
    std::vector<FakeBuilding> buildings;
    std::vector<int> rands;
    size_t nextRand = 0;
    std::vector<std::string> calls;
    std::vector<uint32_t> shed;

    int Power(uint32_t i) { return buildings[i].power; }
    bool ClosedForPower(uint32_t i) { return buildings[i].closedForPower; }
    bool HasBuildJob(uint32_t i)
    {
        calls.push_back("build" + std::to_string(i));
        return buildings[i].buildJob;
    }
    bool HasDemolishJob(uint32_t i)
    {
        calls.push_back("demolish" + std::to_string(i));
        return buildings[i].demolishJob;
    }
    int OutputPower(uint32_t i)
    {
        calls.push_back("output" + std::to_string(i));
        return buildings[i].output;
    }
    int Rand() { return rands[nextRand++]; }
    obl::BuildingBehavior Behavior(uint32_t i) { return obl::BehaviorFromKey(buildings[i].key.c_str()); }
    void SwitchOffConsumer(uint32_t i) { shed.push_back(i); }
};

} // namespace

OBL_TEST_CASE("world_power.sums_split_consumers_and_generators")
{
    FakeEnv env;
    env.buildings = {{1000}, {800, true}, {-7500, false, false, false, 7500}, {0}};
    const Sums sums = SumPower(env, 4);
    OBL_CHECK_EQ(sums.total, 1800.0f);
    OBL_CHECK_EQ(sums.active, 1000.0f);
    OBL_CHECK_EQ(sums.installed, 7500.0f);
    OBL_CHECK_EQ(sums.output, 7500.0f);
}

OBL_TEST_CASE("world_power.generator_with_a_job_adds_no_capacity_but_still_delivers")
{
    FakeEnv env;
    env.buildings = {{-7500, false, true, false, 0}, {-5000, false, false, true, 5000}, {-1000, false, false, false, 1000}};
    const Sums sums = SumPower(env, 3);
    OBL_CHECK_EQ(sums.installed, 1000.0f);
    OBL_CHECK_EQ(sums.output, 6000.0f);
    // HasDemolishJob is skipped when HasBuildJob is already true; consumers ask neither.
    OBL_CHECK_EQ(env.calls[0], std::string("build0"));
    OBL_CHECK_EQ(env.calls[1], std::string("output0"));
    OBL_CHECK_EQ(env.calls[2], std::string("build1"));
    OBL_CHECK_EQ(env.calls[3], std::string("demolish1"));
}

OBL_TEST_CASE("world_power.sums_round_to_float_on_every_store")
{
    // 2^24 + 1 is not a float: the sum of two consumers drops the odd one at the store, as the float store does.
    FakeEnv env;
    env.buildings = {{16777216}, {1}, {1}};
    const Sums sums = SumPower(env, 3);
    OBL_CHECK_EQ(sums.total, 16777216.0f);
    // An int above 2^24 passes through a float first.
    FakeEnv big;
    big.buildings = {{16777217}};
    OBL_CHECK_EQ(SumPower(big, 1).total, 16777216.0f);
}

OBL_TEST_CASE("world_power.no_buildings_means_zero_sums")
{
    FakeEnv env;
    const Sums sums = SumPower(env, 0);
    OBL_CHECK_EQ(sums.total, 0.0f);
    OBL_CHECK_EQ(sums.installed, 0.0f);
    OBL_CHECK_EQ(sums.output, 0.0f);
    OBL_CHECK_EQ(sums.active, 0.0f);
}

OBL_TEST_CASE("world_power.shed_rule_truth_table")
{
    Sums sums;
    sums.installed = 1000.0f;
    sums.output = 1000.0f;
    sums.active = 1500.0f;
    OBL_CHECK(!ShouldShed(sums)); // generators fully on: overload does not shed
    sums.output = 500.0f;
    OBL_CHECK(ShouldShed(sums)); // a generator is off and the demand is above what is delivered
    sums.active = 400.0f;
    OBL_CHECK(!ShouldShed(sums)); // delivered covers the demand
    sums.active = 500.0f;
    OBL_CHECK(!ShouldShed(sums)); // equal is not above
    sums.installed = 0.0f;
    OBL_CHECK(ShouldShed(sums)); // no generators at all
    sums.installed = 1e-8f;
    OBL_CHECK(ShouldShed(sums)); // |installed| below FLT_EPSILON
    sums.installed = -1e-8f;
    OBL_CHECK(ShouldShed(sums));
    sums.installed = 1.2e-7f;
    sums.output = 1.2e-7f;
    sums.active = 0.0f;
    OBL_CHECK(!ShouldShed(sums));
}

OBL_TEST_CASE("world_power.shed_rule_with_nan_follows_the_fpu_flags")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Sums sums;
    sums.installed = 1000.0f;
    sums.output = 500.0f;
    sums.active = nan; // unordered: the "demand above output" test fails
    OBL_CHECK(!ShouldShed(sums));
    sums.active = 1500.0f;
    sums.output = nan; // demand test unordered -> fails, so no shedding through the first branch
    OBL_CHECK(!ShouldShed(sums));
    sums.installed = nan; // |installed| compare unordered counts as "below epsilon"
    OBL_CHECK(ShouldShed(sums));
    sums.installed = 1000.0f;
    sums.output = 500.0f;
    sums.active = 1500.0f;
    OBL_CHECK(ShouldShed(sums));
    sums.installed = nan; // output < installed unordered counts as true
    OBL_CHECK(ShouldShed(sums));
}

OBL_TEST_CASE("world_power.shed_draws_once_and_sheds_an_eligible_building")
{
    FakeEnv env;
    env.buildings = {{0}, {500}, {800}};
    env.rands = {32767 /* % 3 = 1 */};
    Sums sums; // no generators -> the rule fires
    OBL_CHECK_EQ(SwitchOffConsumers(sums, 3, env), 1);
    OBL_CHECK_EQ(env.nextRand, static_cast<size_t>(1));
    OBL_REQUIRE(env.shed.size() == 1);
    OBL_CHECK_EQ(env.shed[0], 1u);
}

OBL_TEST_CASE("world_power.shed_skips_buildings_that_do_not_qualify")
{
    Sums sums;
    for (int variant = 0; variant < 8; ++variant) {
        FakeEnv env;
        FakeBuilding building;
        building.power = 500;
        switch (variant) {
        case 0: building.power = 0; break;
        case 1: building.power = -500; break;
        case 2: building.closedForPower = true; break;
        case 3: building.buildJob = true; break;
        case 4: building.demolishJob = true; break;
        case 5: building.key = "HOTEL"; break;
        case 6: building.key = "ACCOMBLK"; break;
        default: building.key = "BUILDERYARD"; break;
        }
        env.buildings = {building};
        env.rands = {7};
        OBL_CHECK_EQ(SwitchOffConsumers(sums, 1, env), -1);
        OBL_CHECK_EQ(env.nextRand, static_cast<size_t>(1)); // the draw happens before the checks
        OBL_CHECK(env.shed.empty());
    }
}

OBL_TEST_CASE("world_power.shed_exemptions_are_the_five_power_exempt_behaviors")
{
    OBL_CHECK(IsShedExempt(obl::BehaviorFromKey("HANDYMAN")));
    OBL_CHECK(IsShedExempt(obl::BehaviorFromKey("BUILDERYARD")));
    OBL_CHECK(IsShedExempt(obl::BehaviorFromKey("HOTEL")));
    OBL_CHECK(IsShedExempt(obl::BehaviorFromKey("COMPLAINTHUT")));
    OBL_CHECK(IsShedExempt(obl::BehaviorFromKey("ACCOMBLK")));
    OBL_CHECK(!IsShedExempt(obl::BehaviorFromKey("HOTELS")));
    OBL_CHECK(!IsShedExempt(obl::BehaviorFromKey("HOTE")));
    OBL_CHECK(!IsShedExempt(obl::BehaviorFromKey("MAINPOOL")));
}

OBL_TEST_CASE("world_power.shed_does_not_draw_when_the_rule_is_off_or_the_list_is_empty")
{
    FakeEnv env;
    env.buildings = {{500}};
    env.rands = {0};
    Sums sums;
    sums.installed = 1000.0f;
    sums.output = 1000.0f;
    sums.active = 2000.0f;
    OBL_CHECK_EQ(SwitchOffConsumers(sums, 1, env), -1);
    OBL_CHECK_EQ(env.nextRand, static_cast<size_t>(0));
    Sums none;
    OBL_CHECK_EQ(SwitchOffConsumers(none, 0, env), -1);
    OBL_CHECK_EQ(env.nextRand, static_cast<size_t>(0));
}

OBL_TEST_CASE("world_power.shed_index_is_an_unsigned_remainder")
{
    FakeEnv env;
    env.buildings = {{500}, {500}, {500}, {500}, {500}};
    env.rands = {12};
    Sums sums;
    OBL_CHECK_EQ(SwitchOffConsumers(sums, 5, env), 2);
}
