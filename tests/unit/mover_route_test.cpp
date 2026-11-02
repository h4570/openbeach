#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/mover_route.h"
#include "game/map/path_data.h"

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace obl;
namespace {
using object::Mover;
using path::RouteCursor;

constexpr uint32_t kPavement = moving::kPavementMask;
constexpr uint32_t kLand = moving::kLandMask;

// Records the calls in order; tiles come from a map, helpers answer from plain fields.
struct FakeEnv {
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> flags;
    bool prepareResult = true;
    uint32_t prepareStart[2] = {0, 0}, prepareStop[2] = {0, 0}; // where the helper moves the ends to (when moveEnds)
    bool moveEnds = false;
    bool routeFails = false;
    RouteCursor cursor{};
    std::vector<std::string> calls;
    uint32_t lastRouteArgs[4] = {};
    uint32_t lastOption = 99;
    uint32_t clearArgs[4] = {};

    uint32_t TileFlags(uint32_t x, uint32_t y)
    {
        calls.push_back("flags");
        const auto it = flags.find({x, y});
        return it == flags.end() ? 0 : it->second;
    }
    bool Prepare(uint32_t* start, uint32_t* stop, uint32_t)
    {
        calls.push_back("prepare");
        if (prepareResult && moveEnds) {
            start[0] = prepareStart[0];
            start[1] = prepareStart[1];
            stop[0] = prepareStop[0];
            stop[1] = prepareStop[1];
        }
        return prepareResult;
    }
    void Clear(uint32_t* start, uint32_t* stop, uint32_t)
    {
        calls.push_back("clear");
        clearArgs[0] = start[0];
        clearArgs[1] = start[1];
        clearArgs[2] = stop[0];
        clearArgs[3] = stop[1];
    }
    RouteCursor* Route(const char* name, uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
    {
        calls.push_back(name);
        lastRouteArgs[0] = sx;
        lastRouteArgs[1] = sy;
        lastRouteArgs[2] = tx;
        lastRouteArgs[3] = ty;
        if (routeFails) {
            *found = false;
            return nullptr;
        }
        return &cursor;
    }
    RouteCursor* Pavement(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
    {
        return Route("pavement", sx, sy, tx, ty, found);
    }
    RouteCursor* Land(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option)
    {
        lastOption = option;
        return Route("land", sx, sy, tx, ty, found);
    }
    std::string Calls() const
    {
        std::string text;
        for (const auto& call : calls) text += (text.empty() ? "" : ",") + call;
        return text;
    }
};

struct Holder {
    alignas(Mover) unsigned char bytes[sizeof(Mover)];
    Holder()
    {
        std::memset(bytes, 0, sizeof(bytes));
        obl::unit::StampVtable<Mover>(bytes);
        Object().m_pathCursor = reinterpret_cast<path::RouteCursor*>(0x1234);
    }
    Mover& Object() { return *reinterpret_cast<Mover*>(bytes); }
};
path::RouteCursor* const kOldCursor = reinterpret_cast<path::RouteCursor*>(0x1234);
} // namespace

OBL_TEST_CASE("moving_route.matching_ends_get_a_pavement_route_without_cleanup")
{
    Holder h;
    FakeEnv env;
    env.flags[{1, 2}] = 0x10000000u;
    env.flags[{5, 6}] = 0x80;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, kPavement, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,pavement"));
    OBL_CHECK(h.Object().m_pathCursor == &env.cursor);
    OBL_CHECK_EQ(env.lastRouteArgs[0], 1u);
    OBL_CHECK_EQ(env.lastRouteArgs[3], 6u);
    OBL_CHECK(found);
}

OBL_TEST_CASE("moving_route.matching_ends_get_a_land_route_with_option_zero")
{
    Holder h;
    FakeEnv env;
    env.flags[{1, 2}] = 0x80000000u;
    env.flags[{5, 6}] = 0x80000000u | 0x8000; // scenery does not matter for land
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, kLand, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,land"));
    OBL_CHECK_EQ(env.lastOption, 0u);
    OBL_CHECK(h.Object().m_pathCursor == &env.cursor);
}

OBL_TEST_CASE("moving_route.a_failed_route_clears_found_and_stores_null")
{
    Holder h;
    FakeEnv env;
    env.routeFails = true;
    env.flags[{1, 2}] = 0x80000000u;
    env.flags[{5, 6}] = 0x80000000u;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, kLand, env);
    OBL_CHECK(!found);
    OBL_CHECK(h.Object().m_pathCursor == nullptr);
}

OBL_TEST_CASE("moving_route.direct_match_with_another_mask_builds_nothing_and_leaves_the_cursor")
{
    Holder h;
    FakeEnv env;
    env.flags[{1, 2}] = 0x60000000u;
    env.flags[{5, 6}] = 0x60000000u;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, 0x60000000u, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags"));
    OBL_CHECK(h.Object().m_pathCursor == kOldCursor);
}

OBL_TEST_CASE("moving_route.pavement_scenery_on_either_end_goes_through_the_helper")
{
    for (int side = 0; side < 2; ++side) {
        Holder h;
        FakeEnv env;
        env.flags[{1, 2}] = 0x10000000u | (side == 0 ? 0x8000u : 0u);
        env.flags[{5, 6}] = 0x10000000u | (side == 1 ? 0x8000u : 0u);
        uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
        bool found = true;
        moving::BuildRoute(h.Object(), start, stop, &found, kPavement, env);
        OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,prepare,pavement,clear"));
    }
}

OBL_TEST_CASE("moving_route.a_land_end_with_scenery_still_routes_directly")
{
    Holder h;
    FakeEnv env;
    env.flags[{1, 2}] = 0x80008000u;
    env.flags[{5, 6}] = 0x80008000u;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, kLand, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,land"));
}

OBL_TEST_CASE("moving_route.unmatched_ends_use_the_corrected_coordinates_and_clean_up")
{
    Holder h;
    FakeEnv env;
    env.flags[{1, 2}] = 0x100; // free beach: the helper tags it
    env.flags[{5, 6}] = 0x80000000u;
    env.moveEnds = true;
    env.prepareStart[0] = 2;
    env.prepareStart[1] = 2;
    env.prepareStop[0] = 5;
    env.prepareStop[1] = 7;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, kLand, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,prepare,land,clear"));
    OBL_CHECK_EQ(env.lastRouteArgs[0], 2u);
    OBL_CHECK_EQ(env.lastRouteArgs[3], 7u);
    OBL_CHECK_EQ(env.clearArgs[0], 2u);
    OBL_CHECK_EQ(env.clearArgs[3], 7u);
    OBL_CHECK(h.Object().m_pathCursor == &env.cursor);
}

OBL_TEST_CASE("moving_route.a_failed_helper_still_cleans_up_and_builds_no_route")
{
    Holder h;
    FakeEnv env;
    env.prepareResult = false;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, kPavement, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,prepare,clear"));
    OBL_CHECK(found);
    OBL_CHECK(h.Object().m_pathCursor == kOldCursor);
}

OBL_TEST_CASE("moving_route.an_unrecognized_mask_cleans_up_after_a_successful_helper")
{
    Holder h;
    FakeEnv env;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, 0x60000000u, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,prepare,clear"));
}

OBL_TEST_CASE("moving_route.the_same_tile_does_nothing_after_reading_the_flags")
{
    for (const uint32_t mask : {kPavement, kLand, 0u}) {
        Holder h;
        FakeEnv env;
        env.flags[{3, 3}] = 0xffffffffu;
        uint32_t start[2] = {3, 3}, stop[2] = {3, 3};
        bool found = true;
        moving::BuildRoute(h.Object(), start, stop, &found, mask, env);
        OBL_CHECK_EQ(env.Calls(), std::string("flags,flags"));
        OBL_CHECK(h.Object().m_pathCursor == kOldCursor);
    }
}

OBL_TEST_CASE("moving_route.the_helper_runs_when_only_one_end_matches")
{
    Holder h;
    FakeEnv env;
    env.flags[{1, 2}] = 0x80000000u;
    env.flags[{5, 6}] = 0;
    uint32_t start[2] = {1, 2}, stop[2] = {5, 6};
    bool found = true;
    moving::BuildRoute(h.Object(), start, stop, &found, kLand, env);
    OBL_CHECK_EQ(env.Calls(), std::string("flags,flags,prepare,land,clear"));
}
