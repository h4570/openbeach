#include "unit_test.h"

#include "game/objects/building_queue_points.h"

#include <functional>
#include <map>
#include <utility>
#include <vector>

using namespace obl;
using object::BuildingQueue;
using namespace queue_points;

namespace {

constexpr uint32_t kOpen = 0x10000082; // walkable, on a path: every predicate accepts it
constexpr uint32_t kBlocked = 0;

Dir DirOf(const BuildingQueue& queue)
{
    return {queue.m_QueueDirection[0], queue.m_QueueDirection[1]};
}

// The Env of building_queue_points.h: a grid given as a map from the cell position to its flags (unlisted cells are blocked), a scripted rand()
// and the points table as a vector.
struct Fixture {
    std::map<std::pair<float, float>, uint32_t> grid;
    std::vector<int> draws; // rand() results, in order
    size_t drawn = 0;
    std::vector<float> table;
    BuildingQueue queue{};

    uint32_t CellFlags(float x, float y) const
    {
        const auto it = grid.find({x, y});
        return it == grid.end() ? kBlocked : it->second;
    }
    int Rand() { return drawn < draws.size() ? draws[drawn++] : 0; }
    float TableValue(const BuildingQueue&, uint32_t k) const { return table[k]; }
    void SetTableValue(BuildingQueue&, uint32_t k, float value) { table[k] = value; }
    void Set(float x, float y, uint32_t flags) { grid[{x, y}] = flags; }
    void Open(float x, float y) { Set(x, y, kOpen); }

    explicit Fixture(uint32_t limit = 20)
    {
        queue.m_maxLength = limit;
        queue.m_tail[0] = 100.0f;
        queue.m_tail[1] = 200.0f;
        queue.m_QueueDirection[0] = kW.x;
        queue.m_QueueDirection[1] = kW.y;
        queue.m_ServePoint[0] = 0.0f;
        queue.m_ServePoint[1] = 0.0f;
        if (limit >= 1 && limit <= 10) table.assign((limit + 1) * 2, -1.0f);
    }
};

} // namespace

OBL_TEST_CASE("building_queue_points.find_next_goes_straight_on_when_the_cell_will_do")
{
    Fixture f;
    f.Open(100.0f + kW.x, 200.0f + kW.y);
    float out[2];
    OBL_CHECK(QueueFindNextPoint(f, f.queue, out) == FindBranch::Ahead);
    OBL_CHECK_EQ(out[0], 84.0f);
    OBL_CHECK_EQ(out[1], 184.0f);
    OBL_CHECK_EQ(f.queue.m_QueueDirection[0], kW.x);
    OBL_CHECK_EQ(f.drawn, 0u);
}

OBL_TEST_CASE("building_queue_points.find_next_takes_the_only_open_side")
{
    // Heading W (not on the A/B axis): the sideways cells are A and B.
    Fixture f;
    f.Open(100.0f + kA.x, 200.0f + kA.y);
    float out[2];
    OBL_CHECK(QueueFindNextPoint(f, f.queue, out) == FindBranch::First);
    OBL_CHECK_EQ(out[0], 116.0f);
    OBL_CHECK_EQ(out[1], 184.0f);
    OBL_CHECK(DirOf(f.queue) == kA);

    Fixture g;
    g.Open(100.0f + kB.x, 200.0f + kB.y);
    OBL_CHECK(QueueFindNextPoint(g, g.queue, out) == FindBranch::Second);
    OBL_CHECK(DirOf(g.queue) == kB);
    OBL_CHECK_EQ(out[0], 84.0f);
    OBL_CHECK_EQ(out[1], 216.0f);
}

OBL_TEST_CASE("building_queue_points.find_next_tosses_a_coin_when_both_sides_will_do")
{
    // Heading A (on the axis): the sideways cells are W and E; an even draw takes the second (E).
    for (int draw : {4, 7}) {
        Fixture f;
        f.queue.m_QueueDirection[0] = kA.x;
        f.queue.m_QueueDirection[1] = kA.y;
        f.Open(100.0f + kW.x, 200.0f + kW.y);
        f.Open(100.0f + kE.x, 200.0f + kE.y);
        f.draws = {draw};
        float out[2];
        OBL_CHECK(QueueFindNextPoint(f, f.queue, out) == FindBranch::Coin);
        OBL_CHECK_EQ(f.drawn, 1u);
        const Dir expected = draw % 2 == 0 ? kE : kW;
        OBL_CHECK(DirOf(f.queue) == expected);
        OBL_CHECK_EQ(out[0], 100.0f + expected.x);
        OBL_CHECK_EQ(out[1], 200.0f + expected.y);
    }
}

OBL_TEST_CASE("building_queue_points.find_next_turns_round_and_keeps_the_blocked_cell_as_a_last_resort")
{
    Fixture f;
    f.Open(100.0f - kW.x, 200.0f - kW.y); // straight back
    float out[2];
    OBL_CHECK(QueueFindNextPoint(f, f.queue, out) == FindBranch::Back);
    OBL_CHECK_EQ(f.queue.m_QueueDirection[0], -kW.x);
    OBL_CHECK_EQ(f.queue.m_QueueDirection[1], -kW.y);
    OBL_CHECK_EQ(out[0], 116.0f);
    OBL_CHECK_EQ(out[1], 216.0f);

    Fixture g; // nothing open at all: the cell ahead, direction unchanged
    OBL_CHECK(QueueFindNextPoint(g, g.queue, out) == FindBranch::Kept);
    OBL_CHECK_EQ(out[0], 84.0f);
    OBL_CHECK_EQ(out[1], 184.0f);
    OBL_CHECK_EQ(g.queue.m_QueueDirection[0], kW.x);
}

OBL_TEST_CASE("building_queue_points.find_next_axis_retries_the_first_side_with_a_weaker_test")
{
    // Heading A: the first side (W) has the path bit and the blocked-B bit but not the walkable bits: Strict refuses it, CandidateRetry takes it
    // even though the second side (E) is a perfectly good cell.
    Fixture f;
    f.queue.m_QueueDirection[0] = kA.x;
    f.queue.m_QueueDirection[1] = kA.y;
    f.Set(100.0f + kW.x, 200.0f + kW.y, kPathBit | kBlocksPath | kBlockedB);
    f.Open(100.0f + kE.x, 200.0f + kE.y);
    float out[2];
    OBL_CHECK(QueueFindNextPoint(f, f.queue, out) == FindBranch::First);
    OBL_CHECK(DirOf(f.queue) == kW);
    OBL_CHECK_EQ(f.drawn, 0u);
}

OBL_TEST_CASE("building_queue_points.rebuild_walks_the_table_straight_on_over_open_ground")
{
    Fixture f(3);
    f.queue.m_QueueDirection[0] = kE.x;
    f.queue.m_QueueDirection[1] = kE.y;
    for (int i = 1; i <= 4; ++i) f.Open(16.0f * i, 16.0f * i);
    float out[2];
    const RebuildTrace trace = QueueRebuildPoints(f, f.queue, 0, out);
    OBL_CHECK_EQ(trace.ahead, 4);
    for (uint32_t i = 0; i < 4; ++i) {
        OBL_CHECK_EQ(f.table[i * 2], 16.0f * (i + 1));
        OBL_CHECK_EQ(f.table[i * 2 + 1], 16.0f * (i + 1));
    }
    OBL_CHECK_EQ(out[0], 16.0f);
    OBL_CHECK_EQ(out[1], 16.0f);
}

OBL_TEST_CASE("building_queue_points.rebuild_from_the_middle_continues_in_the_running_direction")
{
    Fixture f(3);
    f.table = {0, 0, 10, 20, 99, 99, 99, 99}; // entries 0 and 1 step by (10, 20); 2 and 3 are stale
    f.Open(20.0f, 40.0f);
    f.Open(30.0f, 60.0f);
    float out[2];
    const RebuildTrace trace = QueueRebuildPoints(f, f.queue, 2, out);
    OBL_CHECK_EQ(trace.ahead, 2);
    OBL_CHECK_EQ(f.table[4], 20.0f);
    OBL_CHECK_EQ(f.table[5], 40.0f);
    OBL_CHECK_EQ(f.table[6], 30.0f);
    OBL_CHECK_EQ(f.table[7], 60.0f);
    OBL_CHECK_EQ(out[0], 20.0f);
    OBL_CHECK_EQ(out[1], 40.0f);
    OBL_CHECK_EQ(f.table[2], 10.0f); // earlier entries are left alone
}

OBL_TEST_CASE("building_queue_points.rebuild_sideways_takes_the_longer_free_run_and_ties_go_to_the_first")
{
    // Heading A (on the axis) from the serve point: straight on is blocked; the sideways cells are W (-16,-16) and E (16,16).
    auto make = [](int westRun, int eastRun) {
        Fixture f(1);
        f.queue.m_QueueDirection[0] = kA.x;
        f.queue.m_QueueDirection[1] = kA.y;
        for (int i = 0; i < westRun; ++i) f.Open(kW.x * (i + 1), kW.y * (i + 1));
        for (int i = 0; i < eastRun; ++i) f.Open(kE.x * (i + 1), kE.y * (i + 1));
        return f;
    };
    float out[2];
    Fixture west = make(3, 1);
    const RebuildTrace a = QueueRebuildPoints(west, west.queue, 0, out);
    OBL_CHECK_EQ(a.longer, 1);
    OBL_CHECK_EQ(out[0], kW.x);

    Fixture east = make(1, 3);
    QueueRebuildPoints(east, east.queue, 0, out);
    OBL_CHECK_EQ(out[0], kE.x);

    Fixture tie = make(2, 2);
    QueueRebuildPoints(tie, tie.queue, 0, out);
    OBL_CHECK_EQ(out[0], kW.x);
}

OBL_TEST_CASE("building_queue_points.rebuild_off_axis_sideways_cells_are_a_and_b")
{
    // Heading W (off the axis): the sideways cells are A (first) and B (second); the second has the longer run here.
    Fixture f(1);
    f.Open(kA.x, kA.y);
    f.Open(kB.x, kB.y);
    f.Open(kB.x * 2, kB.y * 2);
    float out[2];
    const RebuildTrace trace = QueueRebuildPoints(f, f.queue, 0, out);
    OBL_CHECK_EQ(trace.longer, 1);
    OBL_CHECK_EQ(f.table[0], kB.x);
    OBL_CHECK_EQ(f.table[1], kB.y);

    // Only the first one is open: still through the gate, and the first wins.
    Fixture g(1);
    g.Open(kA.x, kA.y);
    QueueRebuildPoints(g, g.queue, 0, out);
    OBL_CHECK_EQ(g.table[0], kA.x);

    // Neither is open: the blocked step straight on is kept.
    Fixture h(1);
    const RebuildTrace kept = QueueRebuildPoints(h, h.queue, 0, out);
    OBL_CHECK_EQ(kept.kept, 2);
    OBL_CHECK_EQ(h.table[0], kW.x);
}

OBL_TEST_CASE("building_queue_points.rebuild_of_a_long_queue_is_the_tail")
{
    Fixture f(11);
    float out[2] = {0, 0};
    const RebuildTrace trace = QueueRebuildPoints(f, f.queue, 0, out);
    OBL_CHECK_EQ(trace.ahead + trace.longer + trace.lenient + trace.kept, 0);
    OBL_CHECK_EQ(out[0], 100.0f);
    OBL_CHECK_EQ(out[1], 200.0f);
}
