#pragma once

#include "game/objects/building_queue_state.h"

#include <cstdint>

// queue point search and reconstruction describe the geometry of a service queue (docs/architecture/objects.md).
// No game memory here: the glue in src/game/objects/building_queue.cpp supplies the Env and the unit tests a fake.
//   uint32_t CellFlags(float x, float y)            reads the flags of the grid cell containing (x, y)
//   int Rand()                                      the game's rand()
//   float TableValue(const BuildingQueue&, uint32_t k)             the k-th float of the points table (point i = floats 2i, 2i+1)
//   void SetTableValue(BuildingQueue&, uint32_t k, float value)    writes it (the table is a separate allocation, m_points)
//
// A queue walks over the grid in diagonal steps of one cell: the four directions are (+-16, +-16). The cell read is a pure query, so it is
// done once per position. The conditions are NOT symmetric and are kept as they are (anything else would move guests to different spots):
//   - the flag words come in several variants of "this cell will do" (Strict, Lenient..., the straight-ahead test), see the predicates below;
//   - RebuildPoints, for a direction off the A/B axis, tests the 0x8000 bit of the *second* candidate while checking the first (a bug kept on purpose;
//     the second candidate is Strict there, which already excludes that bit, so the first one's bit is simply never asked).
namespace obl {

namespace queue_points {

inline constexpr float kStep = 16.0f;
struct Dir {
    float x, y;
    bool operator==(const Dir& o) const { return x == o.x && y == o.y; }
};
// The constants of queue initialization: W/E and A/B are the two diagonals, each pair opposite.
inline constexpr Dir kW{-kStep, -kStep};
inline constexpr Dir kE{kStep, kStep};
inline constexpr Dir kA{kStep, -kStep};
inline constexpr Dir kB{-kStep, kStep};

// Cell flag bits.
inline constexpr uint32_t kWalkable = 0x10000080; // either bit: the cell can be stood on
inline constexpr uint32_t kWalkableEdge = 0x10000000;
inline constexpr uint32_t kPathBit = 0x2;
inline constexpr uint32_t kBlocksPath = 0x10;
inline constexpr uint32_t kBlockedA = 0x800;
inline constexpr uint32_t kBlockedB = 0x8000;

inline bool Has(uint32_t flags, uint32_t mask)
{
    return (flags & mask) != 0;
}

// The cell will do as a stand point.
inline bool Strict(uint32_t f)
{
    return (Has(f, kWalkable) && !Has(f, kBlockedA) && !Has(f, kBlockedB)) ||
            (Has(f, kPathBit) && !Has(f, kBlocksPath) && !Has(f, kBlockedB));
}
// The same with the weaker first bit mask (RebuildPoints' test of the point straight ahead).
inline bool StrictEdge(uint32_t f)
{
    return (Has(f, kWalkableEdge) && !Has(f, kBlockedA) && !Has(f, kBlockedB)) ||
            (Has(f, kPathBit) && !Has(f, kBlocksPath) && !Has(f, kBlockedB));
}
// RebuildPoints' second-choice test: a blocked-A cell with the walkable bit still passes when the blocked-B bit is set too.
inline bool LenientRebuild(uint32_t f)
{
    return (Has(f, kWalkable) && (!Has(f, kBlockedA) || Has(f, kBlockedB))) ||
            (Has(f, kPathBit) && (!Has(f, kBlocksPath) || Has(f, kBlockedB)));
}
// FindNextPoint: the point straight ahead is kept.
inline bool StraightAhead(uint32_t f)
{
    return (Has(f, kWalkable) && !Has(f, kBlockedB)) || (Has(f, kPathBit) && (!Has(f, kBlocksPath) || Has(f, kBlockedB)));
}
// FindNextPoint: the point straight back is taken (the queue turns round).
inline bool Back(uint32_t f)
{
    return (Has(f, kWalkable) && !Has(f, kBlockedB)) || (Has(f, kPathBit) && !Has(f, kBlocksPath) && !Has(f, kBlockedB));
}
// FindNextPoint, direction on the A/B axis: the first candidate's second look.
inline bool CandidateRetry(uint32_t f)
{
    return (Has(f, kWalkable) && !Has(f, kBlockedA) && !Has(f, kBlockedB)) ||
            (Has(f, kPathBit) && (!Has(f, kBlocksPath) || Has(f, kBlockedB)));
}

inline bool Even(int draw)
{
    return draw % 2 == 0;
}

// Which way FindNextPoint went (the unit tests check it).
enum class FindBranch { Ahead, Coin, First, Second, Back, Kept };
// How many of the entries RebuildPoints computed took each way.
struct RebuildTrace {
    int ahead = 0; // the step straight on will do
    int longer = 0; // both sideways cells considered, the longer free run won
    int lenient = 0; // a sideways cell taken by the weaker test
    int kept = 0; // nothing better than the blocked step
};

} // namespace queue_points

// QueueFindNextPoint (fills `out`): the stand point after the tail of a long queue (limit > 10), by the direction the
// queue grows in. Straight ahead when the cell will do; otherwise the two sideways cells (perpendicular diagonals; a coin toss when both do), then
// straight back (which turns the direction round), then straight ahead after all. Writes the direction of the queue, nothing else.
template<class Env> queue_points::FindBranch QueueFindNextPoint(Env& env, object::BuildingQueue& queue, float out[2])
{
    using namespace queue_points;
    const float tx = queue.m_tail[0], ty = queue.m_tail[1];
    const Dir dir{queue.m_QueueDirection[0], queue.m_QueueDirection[1]};
    const float ahead[2] = {dir.x + tx, dir.y + ty};
    if (StraightAhead(env.CellFlags(ahead[0], ahead[1]))) {
        out[0] = ahead[0];
        out[1] = ahead[1];
        return FindBranch::Ahead;
    }
    auto turn = [&](const Dir& to, const float point[2]) {
        queue.m_QueueDirection[0] = to.x;
        queue.m_QueueDirection[1] = to.y;
        out[0] = point[0];
        out[1] = point[1];
    };
    const bool aOrB = dir == kA || dir == kB;
    // First / second sideways candidates: perpendicular to the direction's axis.
    const Dir& first = aOrB ? kW : kA;
    const Dir& second = aOrB ? kE : kB;
    const float c1[2] = {first.x + tx, first.y + ty};
    const float c2[2] = {second.x + tx, second.y + ty};
    const uint32_t f1 = env.CellFlags(c1[0], c1[1]);
    const uint32_t f2 = env.CellFlags(c2[0], c2[1]);
    const bool ok1 = Strict(f1), ok2 = Strict(f2);
    if (ok1 && ok2) {
        if (Even(env.Rand()))
            turn(second, c2);
        else
            turn(first, c1);
        return FindBranch::Coin;
    }
    if (aOrB) {
        // Here the first candidate is tried again with the weaker flag test before the second one is looked at.
        if (CandidateRetry(f1)) {
            turn(first, c1);
            return FindBranch::First;
        }
        if (ok2) {
            turn(second, c2);
            return FindBranch::Second;
        }
    } else if (ok2) {
        turn(second, c2);
        return FindBranch::Second;
    } else if (ok1) {
        turn(first, c1);
        return FindBranch::First;
    }
    const float back[2] = {tx - dir.x, ty - dir.y};
    if (Back(env.CellFlags(back[0], back[1]))) {
        queue.m_QueueDirection[0] = -queue.m_QueueDirection[0];
        queue.m_QueueDirection[1] = -queue.m_QueueDirection[1];
        out[0] = back[0];
        out[1] = back[1];
        return FindBranch::Back;
    }
    out[0] = ahead[0];
    out[1] = ahead[1];
    return FindBranch::Kept;
}

// QueueRebuildPoints (fills `out`): recomputes the points table of a queue with a limit of 1..10 from entry `index`
// to the last (limit + 1 entries; entry 0 is the serve point's neighbour). Each entry is one step from the one before (the serve point for entry 0)
// in the running direction (the queue's for the first two entries, else the step between the two entries before); a blocked step looks sideways and
// picks the side with the longer free run. The queue's own direction is not written. `out` = entry `index` afterwards; for a limit above 10 there
// is no table and `out` = the tail.
template<class Env> queue_points::RebuildTrace QueueRebuildPoints(Env& env, object::BuildingQueue& queue, uint32_t index, float out[2])
{
    using namespace queue_points;
    RebuildTrace trace;
    const uint32_t limit = queue.m_maxLength;
    if (limit > 10) {
        out[0] = queue.m_tail[0];
        out[1] = queue.m_tail[1];
        return trace;
    }
    Dir dir;
    if (index < 2) {
        dir = {queue.m_QueueDirection[0], queue.m_QueueDirection[1]};
    } else {
        dir = {env.TableValue(queue, index * 2 - 2) - env.TableValue(queue, index * 2 - 4),
                env.TableValue(queue, index * 2 - 1) - env.TableValue(queue, index * 2 - 3)};
    }
    // How many cells in a row from `p` along `step` will do.
    auto run = [&](float px, float py, const Dir& step) {
        int count = 0;
        while (Strict(env.CellFlags(px, py))) {
            ++count;
            px += step.x;
            py += step.y;
        }
        return count;
    };
    for (uint32_t i = index; i < limit + 1; ++i) {
        float prev[2];
        if (i == 0) {
            prev[0] = queue.m_ServePoint[0];
            prev[1] = queue.m_ServePoint[1];
        } else {
            prev[0] = env.TableValue(queue, i * 2 - 2);
            prev[1] = env.TableValue(queue, i * 2 - 1);
        }
        const float p[2] = {dir.x + prev[0], dir.y + prev[1]};
        auto put = [&](const float point[2]) {
            env.SetTableValue(queue, i * 2, point[0]);
            env.SetTableValue(queue, i * 2 + 1, point[1]);
        };
        if (StrictEdge(env.CellFlags(p[0], p[1]))) {
            put(p);
            ++trace.ahead;
            continue;
        }
        const bool aOrB = dir == kA || dir == kB;
        // Sideways candidates, perpendicular to the axis: W/E after A or B, else A/B.
        const Dir& first = aOrB ? kW : kA;
        const Dir& second = aOrB ? kE : kB;
        const float c1[2] = {first.x + prev[0], first.y + prev[1]};
        const float c2[2] = {second.x + prev[0], second.y + prev[1]};
        const uint32_t f1 = env.CellFlags(c1[0], c1[1]);
        const uint32_t f2 = env.CellFlags(c2[0], c2[1]);
        auto take = [&](const Dir& to, const float point[2]) {
            dir = to;
            put(point);
        };
        auto longerRun = [&]() {
            const int n1 = run(c1[0], c1[1], first);
            const int n2 = run(c2[0], c2[1], second);
            if (n1 < n2)
                take(second, c2);
            else
                take(first, c1);
            ++trace.longer;
        };
        if (aOrB) {
            if (Strict(f2) && Strict(f1)) {
                longerRun();
            } else if (LenientRebuild(f1)) {
                take(first, c1);
                ++trace.lenient;
            } else if (LenientRebuild(f2)) {
                take(second, c2);
                ++trace.lenient;
            } else {
                put(p);
                ++trace.kept;
            }
        } else {
            // This asks the blocked-B bit of the second candidate, not of the first (see the header comment).
            bool gate = Strict(f2) && Has(f1, kWalkable) && !Has(f1, kBlockedA);
            gate = gate || (Has(f1, kPathBit) && (!Has(f1, kBlocksPath) || Has(f1, kBlockedB)));
            if (gate) {
                longerRun();
            } else if (Strict(f2)) {
                take(second, c2);
                ++trace.lenient;
            } else if (Strict(f1)) {
                take(first, c1);
                ++trace.lenient;
            } else {
                put(p);
                ++trace.kept;
            }
        }
    }
    out[0] = env.TableValue(queue, index * 2);
    out[1] = env.TableValue(queue, index * 2 + 1);
    return trace;
}

} // namespace obl
