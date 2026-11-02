#include "unit_test.h"
#include "game/ferry/ferry_lifecycle.h"

#include <utility>

// The FerryManager's saved cursor (ferry_lifecycle.h SetCursor), which the obl.save reader restores from its schedule index.
namespace {
obl::ferry::Boat MakeBoat(float time)
{
    obl::ferry::Boat boat{};
    boat.Time = time;
    return boat;
}

// The schedule index of the cursor, -1 without one or past the end.
int CursorIndex(const obl::ferry::Manager& m)
{
    if (!m.cursor) return -1;
    int index = 0;
    for (auto node = m.schedule.begin(); node != m.schedule.end(); ++node, ++index)
        if (node == *m.cursor) return index;
    return -1;
}
} // namespace

OBL_TEST_CASE("ferry.lifecycle cursor index past the end wraps around the schedule ring")
{
    using namespace obl::ferry;
    // Three boats: the ring is boat 0, 1, 2, the sentinel, and boat 0 again. Index 4 is boat 0, 3 the sentinel, 5 boat 1.
    for (auto [index, expected] : {std::pair{4u, 0}, std::pair{3u, -1}, std::pair{5u, 1}, std::pair{2u, 2}, std::pair{kNoCursor, -1}}) {
        Manager m;
        for (float time : {1.0f, 2.0f, 3.0f}) m.schedule.push_back(MakeBoat(time));
        SetCursor(m, index);
        OBL_CHECK_EQ(CursorIndex(m), expected);
    }
}
