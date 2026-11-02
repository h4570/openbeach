#include "unit_test.h"
#include "game/map/path_data.h"
#include <memory>
#include <string>
#include <vector>

using namespace obl;

namespace {
std::shared_ptr<path::Route> MakeRoute(std::vector<map::TilePoint> steps)
{
    auto route = std::make_shared<path::Route>();
    route->m_steps = std::move(steps);
    return route;
}
} // namespace

OBL_TEST_CASE("path_data.route_defaults_are_the_constructor_values")
{
    const path::Route d;
    OBL_CHECK(d.m_timeStamp == 0.0f);
    OBL_CHECK_EQ(d.m_referenceCount, 0);
    OBL_CHECK(d.m_steps.empty());
    OBL_CHECK_EQ(d.m_start[0], -1);
    OBL_CHECK_EQ(d.m_stop[1], -1);
    OBL_CHECK_EQ(d.m_valid, 1);
}

OBL_TEST_CASE("path_data.advance_endpoint_signals_and_equality_boundary")
{
    auto d = MakeRoute(std::vector<map::TilePoint>(5));
    path::RouteCursor c = path::MakeCursor(d, false);
    OBL_CHECK_EQ(c.m_currentStep, 0);
    OBL_CHECK_EQ(path::Advance(c), 0); // 1
    OBL_CHECK_EQ(path::Advance(c), 0); // 2
    OBL_CHECK_EQ(path::Advance(c), 1); // 3, forward signal precedes the last point
    OBL_CHECK_EQ(path::Advance(c), 0); // 4
    OBL_CHECK_EQ(path::Advance(c), 2); // 5
    OBL_CHECK_EQ(path::Advance(c), 0);
    c = path::MakeCursor(d, true);
    OBL_CHECK_EQ(c.m_currentStep, 4);
    OBL_CHECK_EQ(path::Advance(c), 0);
    OBL_CHECK_EQ(path::Advance(c), 0);
    OBL_CHECK_EQ(path::Advance(c), 0);
    OBL_CHECK_EQ(path::Advance(c), 1); // 0, reverse signal is the first array element
    OBL_CHECK_EQ(path::Advance(c), 2);
    c = path::MakeCursor(MakeRoute({}), true);
    OBL_CHECK_EQ(c.m_currentStep, -1);
    OBL_CHECK_EQ(path::Advance(c), 2);
    c.m_currentStep = INT32_MIN;
    OBL_CHECK_EQ(path::Advance(c), 0); // integer wrap to INT32_MAX
}

OBL_TEST_CASE("path_data.straight_uses_whole_route_and_quantized_world_directions")
{
    auto d = MakeRoute({{20, 20}});
    path::RouteCursor c = path::MakeCursor(d, true);
    OBL_CHECK(!path::IsStraight(c, 64));
    d->m_steps = {{20, 20}, {20, 21}};
    OBL_CHECK(path::IsStraight(c, 64));
    d->m_steps = {{20, 20}, {20, 21}, {20, 23}, {20, 26}};
    c.m_currentStep = 999;
    OBL_CHECK(path::IsStraight(c, 64));
    d->m_steps[2].x = 23;
    OBL_CHECK(!path::IsStraight(c, 64));
    d->m_steps[2] = {20, 20};
    OBL_CHECK(!path::IsStraight(c, 64));
    OBL_CHECK_EQ(path::ApproxSqrt(0), 0);
    OBL_CHECK_EQ(path::ApproxSqrt(4096), 64);
}

OBL_TEST_CASE("path_data.step_tile_reads_the_current_pair")
{
    path::RouteCursor c = path::MakeCursor(MakeRoute({{1, 2}, {3, 4}, {5, 6}}), false);
    c.m_currentStep = 2;
    OBL_CHECK_EQ(path::StepTile(c)[0], 5u);
    OBL_CHECK_EQ(path::StepTile(c)[1], 6u);
}

OBL_TEST_CASE("path_data.route_goes_with_its_last_cursor")
{
    std::weak_ptr<const path::Route> watch;
    {
        auto a = std::make_unique<path::RouteCursor>(path::MakeCursor(MakeRoute({{1, 1}}), false));
        watch = a->m_route;
        auto b = std::make_unique<path::RouteCursor>(*a);
        a.reset();
        OBL_CHECK(!watch.expired());
        b.reset();
    }
    OBL_CHECK(watch.expired());
}
