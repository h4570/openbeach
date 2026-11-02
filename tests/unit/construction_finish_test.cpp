#include "unit_test.h"
#include "game/construction/construction_job_finish.h"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::construction;

namespace {
struct Env {
    bool footprint = true, job = true;
    float time = 10;
    int refund = 17;
    uint32_t flags = 0;
    std::vector<construction::Hoarding> hoardings{{0x102, -1, 129, 0}, {3, 4, 5, 0}};
    std::vector<std::string> calls;
    FinishState published{};
    HoardingPlacement placement{};
    int posts = 0;
    std::array<uint32_t, 2> TilePosition()
    {
        calls.push_back("tile");
        return {12, 34};
    }
    int Refund() { return refund; }
    void ClearFootprint(int id) { calls.push_back("clear " + std::to_string(id)); }
    void ApplyFootprint(uint32_t x, uint32_t y, int phase, int mode, int id)
    {
        calls.push_back("footprint " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(phase) + " " +
                std::to_string(mode) + " " + std::to_string(id));
    }
    void Publish(const FinishState& state) { published = state; }
    void DeleteJob(int id) { calls.push_back("delete " + std::to_string(id)); }
    void Reallocate() { calls.push_back("reallocate"); }
    bool HasFootprint() { return footprint; }
    bool HasJob() { return job; }
    float Time() { return time; }
    construction::Hoarding HoardingAt(int index)
    {
        calls.push_back("entry " + std::to_string(index));
        return hoardings.at(index);
    }
    uint32_t TileFlags(uint32_t x, uint32_t y)
    {
        calls.push_back("flags " + std::to_string(x) + " " + std::to_string(y));
        return flags;
    }
    void PostPlace(const HoardingPlacement& value)
    {
        ++posts;
        placement = value;
        calls.push_back("place");
        OBL_CHECK_EQ(published.hoardings, 0);
        time = 11;
    }
};
} // namespace

OBL_TEST_CASE("Construction finish: grid, deletion and allocation order")
{
    Env env;
    FinishState state{10, 20, 30, 1, 2, 0};
    FinishBuild(42, state, env);
    OBL_CHECK((env.calls == std::vector<std::string>{"tile", "footprint 12 34 1 2 42", "delete 42", "reallocate"}));
    OBL_CHECK_EQ(state.cash, 10);
    OBL_CHECK_EQ(state.death, 0);
    env.calls.clear();
    FinishDemolish(42, state, env);
    OBL_CHECK((env.calls == std::vector<std::string>{"clear 42", "tile", "footprint 12 34 1 3 42", "delete 42", "reallocate"}));
    OBL_CHECK_EQ(env.published.cash, 27);
    OBL_CHECK_EQ(env.published.income, 20);
    OBL_CHECK_EQ(env.published.demolitionIncome, 47);
    OBL_CHECK_EQ(env.published.death, 1);
}

OBL_TEST_CASE("Construction demolition: refunds cannot satisfy operating-income goals")
{
    Env env;
    FinishState state{100, 50, 0};
    FinishDemolish(42, state, env);
    FinishDemolish(43, state, env);
    OBL_CHECK_EQ(state.cash, 134);
    OBL_CHECK_EQ(state.income, 50);
    OBL_CHECK_EQ(state.demolitionIncome, 34);
}

OBL_TEST_CASE("Construction demolition: signed cash and unsigned demolition statistics")
{
    Env env;
    env.refund = -1;
    FinishState state{100, 0, 0};
    FinishDemolish(42, state, env);
    OBL_CHECK_EQ(state.cash, 99);
    OBL_CHECK_EQ(state.income, 0);
    OBL_CHECK_EQ(state.demolitionIncome, 4294967296.0f);
}

OBL_TEST_CASE("Construction demolition: the sum is rounded once")
{
    Env env;
    env.refund = 16777217;
    FinishState state{-16777216.0f, -16777216.0f, -16777216.0f};
    FinishDemolish(42, state, env);
    OBL_CHECK_EQ(state.cash, 1);
    OBL_CHECK_EQ(state.income, -16777216.0f);
    OBL_CHECK_EQ(state.demolitionIncome, 1);
}

OBL_TEST_CASE("Construction hoarding: early exits leave count and time")
{
    for (int branch = 0; branch < 6; ++branch) {
        Env env;
        FinishState state{};
        state.placed = 0;
        state.hoardings = 1;
        if (branch == 0) env.footprint = false;
        if (branch == 1) env.job = false;
        if (branch == 2) state.hoardings = 0;
        if (branch == 3) env.time = 0.1f;
        if (branch == 4) env.time = -1;
        if (branch == 5) env.time = std::numeric_limits<float>::quiet_NaN();
        const int count = state.hoardings;
        SpawnHoarding(42, state, env);
        OBL_CHECK(env.calls.empty());
        OBL_CHECK_EQ(state.hoardings, count);
        OBL_CHECK_EQ(state.placed, 0);
    }
}

OBL_TEST_CASE("Construction hoarding: double subtraction stays above float threshold")
{
    Env env;
    env.time = 0.1f;
    FinishState state{};
    state.placed = -1e-10f;
    state.hoardings = 1;
    SpawnHoarding(42, state, env);
    OBL_CHECK_EQ(env.posts, 1);
}

OBL_TEST_CASE("Construction hoarding: terrain consumes the entry without timestamp")
{
    for (uint32_t flags : {1u, 2u, 3u, 0x803u}) {
        Env env;
        env.flags = flags;
        FinishState state{};
        state.hoardings = 2;
        SpawnHoarding(42, state, env);
        OBL_CHECK_EQ(state.hoardings, 1);
        OBL_CHECK_EQ(env.published.hoardings, 1);
        OBL_CHECK_EQ(state.placed, 0);
        OBL_CHECK_EQ(env.posts, 0);
        OBL_CHECK((env.calls == std::vector<std::string>{"entry 1", "flags 4 5"}));
    }
}

OBL_TEST_CASE("Construction hoarding: placement payload and post-call timestamp")
{
    Env env;
    env.flags = 0x800;
    FinishState state{};
    state.hoardings = 1;
    SpawnHoarding(42, state, env);
    OBL_CHECK_EQ(env.posts, 1);
    OBL_CHECK_EQ(std::string(env.placement.m_name), "singlecell");
    OBL_CHECK_EQ(std::string(env.placement.m_sprite), "HOARDING_22");
    OBL_CHECK_EQ(env.placement.m_x, UINT32_MAX);
    OBL_CHECK_EQ(env.placement.m_y, 129u);
    OBL_CHECK_EQ(env.placement.m_direction, 2);
    OBL_CHECK_EQ(env.placement.m_usesTilePosition, 1);
    OBL_CHECK_EQ(env.placement.m_owner, -2);
    OBL_CHECK_EQ(state.placed, 11);
    OBL_CHECK_EQ(state.hoardings, 0);
    OBL_CHECK_EQ(env.published.placed, 11);
}

OBL_TEST_CASE("Construction hoarding: radix 20 id range")
{
    construction::Hoarding h{};
    OBL_CHECK_EQ(std::string(Placement(0, h).m_sprite), "HOARDING_0");
    OBL_CHECK_EQ(std::string(Placement(19, h).m_sprite), "HOARDING_j");
    OBL_CHECK_EQ(std::string(Placement(20, h).m_sprite), "HOARDING_10");
    OBL_CHECK_EQ(std::string(Placement(-1, h).m_sprite), "HOARDING_3723ai4f");
}
