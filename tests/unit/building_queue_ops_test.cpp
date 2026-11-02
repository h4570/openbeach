#include "unit_test.h"

#include "game/objects/building_queue_ops.h"

#include <functional>
#include <string>
#include <vector>

using namespace obl;
using object::BuildingQueue;

namespace {

struct Sent {
    int32_t sender, receiver, type, flags, data;
    float delay; // < 0 = not delayed
    bool operator==(const Sent& o) const
    {
        return sender == o.sender && receiver == o.receiver && type == o.type && flags == o.flags && data == o.data && delay == o.delay;
    }
};

// A queue with its deques, a points table and an Env that records what the code asks of the game.
struct Fixture {
    std::vector<float> points; // (limit + 1) x (x, y)
    BuildingQueue queue{};
    std::vector<Sent> sent;
    std::vector<std::string> calls;
    std::function<uint32_t(float, float)> cell = [](float, float) { return 0x10000082u; };
    float findNext[2] = {70.0f, 71.0f};
    float rebuilt[2] = {80.0f, 81.0f};

    Fixture(size_t count, uint32_t limit, bool table = true)
    {
        for (size_t i = 0; i < count; ++i) {
            queue.m_qlQueue.push_back(static_cast<int32_t>(100 + i));
            queue.m_qlCommmod.push_back(static_cast<int32_t>(i % 5));
        }
        queue.m_ownerId = 7;
        queue.m_maxLength = limit;
        queue.m_ServePoint[0] = 10.0f;
        queue.m_ServePoint[1] = -4.0f;
        queue.m_QueueDirection[0] = 2.5f;
        queue.m_QueueDirection[1] = 0.5f;
        queue.m_tail[0] = 99.0f;
        queue.m_tail[1] = 98.0f;
        if (table && limit >= 1 && limit <= 10) {
            for (uint32_t i = 0; i <= limit; ++i) {
                points.push_back(1000.0f + i * 10.0f);
                points.push_back(2000.0f + i * 10.0f);
            }
            queue.m_points = points.data();
        }
    }

    // The Env of building_queue_ops.h.
    uint32_t CellFlags(float x, float y) { return cell(x, y); }
    float TableValue(const BuildingQueue& q, uint32_t k) { return q.m_points[k]; }
    void Post(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
    {
        sent.push_back({sender, receiver, type, flags, data, -1.0f});
    }
    void PostDelayed(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data, float delay)
    {
        sent.push_back({sender, receiver, type, flags, data, delay});
    }
    void FindNextPoint(BuildingQueue&, float out[2])
    {
        calls.push_back("find");
        out[0] = findNext[0];
        out[1] = findNext[1];
    }
    void RebuildPoints(BuildingQueue&, uint32_t index, float out[2])
    {
        calls.push_back("rebuild " + std::to_string(index));
        out[0] = rebuilt[0];
        out[1] = rebuilt[1];
    }
};

// Records the tail RebuildPoints finds in the queue (what Dequeue computed before it).
struct SpyFixture : Fixture {
    using Fixture::Fixture;
    std::vector<std::pair<float, float>> seen;
    void RebuildPoints(BuildingQueue& q, uint32_t index, float out[2])
    {
        seen.push_back({q.m_tail[0], q.m_tail[1]});
        Fixture::RebuildPoints(q, index, out);
    }
};

} // namespace

OBL_TEST_CASE("building_queue_ops.enqueue_refuses_a_full_queue_with_queue_full")
{
    Fixture f(3, 3);
    OBL_CHECK(!EnqueueGuest(f, f.queue, 555, 2, true));
    OBL_REQUIRE(f.sent.size() == 1);
    OBL_CHECK(f.sent[0] == (Sent{7, 555, 32, 0, 0, -1.0f}));
    OBL_CHECK_EQ(f.queue.m_qlQueue.size(), size_t(3));
    OBL_CHECK_EQ(f.queue.m_qlCommmod.size(), size_t(3));
}

OBL_TEST_CASE("building_queue_ops.enqueue_into_a_building_without_a_queue_says_building_full")
{
    Fixture f(0, 0);
    OBL_CHECK(!EnqueueGuest(f, f.queue, 555, 2, true));
    OBL_REQUIRE(f.sent.size() == 1);
    OBL_CHECK_EQ(f.sent[0].type, 34);
}

OBL_TEST_CASE("building_queue_ops.enqueue_refuses_when_only_the_commodity_deque_is_full")
{
    Fixture f(2, 4);
    f.queue.m_qlCommmod.insert(f.queue.m_qlCommmod.end(), {0, 0}); // two more commodity entries than guests
    OBL_CHECK(!EnqueueGuest(f, f.queue, 555, 2, true));
    OBL_REQUIRE(f.sent.size() == 1);
    OBL_CHECK_EQ(f.sent[0].type, 32);
    OBL_CHECK_EQ(f.queue.m_qlQueue.size(), size_t(2));
}

OBL_TEST_CASE("building_queue_ops.enqueue_without_moving_only_stores_the_guest")
{
    Fixture f(1, 5);
    OBL_CHECK(EnqueueGuest(f, f.queue, 555, 3, false));
    OBL_CHECK(f.sent.empty());
    OBL_CHECK_EQ(f.queue.m_qlQueue[1], 555);
    OBL_CHECK_EQ(f.queue.m_qlCommmod[1], 3);
    OBL_CHECK_EQ(f.queue.m_tail[0], 99.0f);
}

OBL_TEST_CASE("building_queue_ops.enqueue_takes_the_point_table_entry_for_the_new_length")
{
    Fixture f(2, 5);
    OBL_CHECK(EnqueueGuest(f, f.queue, 555, 3, true));
    // Length 3 -> points[3] = (1030, 2030); the cell is walkable and free: no RebuildPoints.
    OBL_CHECK_EQ(f.queue.m_tail[0], 1030.0f);
    OBL_CHECK_EQ(f.queue.m_tail[1], 2030.0f);
    OBL_REQUIRE(f.sent.size() == 2);
    OBL_CHECK(f.sent[0] == (Sent{7, 555, 33, 0, 0, -1.0f}));
    OBL_CHECK(f.sent[1] == (Sent{7, 555, 31, 1030, 2030, -1.0f}));
    OBL_CHECK(f.calls.empty());
}

OBL_TEST_CASE("building_queue_ops.enqueue_rebuilds_when_the_tail_cell_is_not_a_stand_point")
{
    struct Case {
        uint32_t flags;
        bool rebuild;
    };
    const Case cases[] = {
            {0x00000000, true}, // not walkable
            {0x00000100, true}, // not in the walkable mask
            {0x10000082, false}, // fine
            {0x10000092, true}, // 0x10 set
            {0x10008082, true}, // 0x8000 set
            {0x00000002, false}, // any bit of the mask is enough
    };
    for (const Case& c : cases) {
        Fixture f(2, 5);
        f.cell = [&](float, float) { return c.flags; };
        f.rebuilt[0] = 80.9f;
        f.rebuilt[1] = -3.9f;
        OBL_CHECK(EnqueueGuest(f, f.queue, 555, 3, true));
        OBL_CHECK_EQ(!f.calls.empty(), c.rebuild);
        if (c.rebuild) {
            OBL_CHECK_EQ(f.calls[0], std::string("rebuild 3")); // the new length
            OBL_CHECK_EQ(f.queue.m_tail[0], 80.9f);
            OBL_REQUIRE(f.sent.size() == 2);
            OBL_CHECK(f.sent[1] == (Sent{7, 555, 31, 80, -3, -1.0f})); // truncated toward zero
        }
    }
}

OBL_TEST_CASE("building_queue_ops.enqueue_asks_find_next_point_for_long_queues")
{
    Fixture f(2, 20);
    OBL_CHECK(EnqueueGuest(f, f.queue, 555, 3, true));
    OBL_REQUIRE(f.calls.size() == 1);
    OBL_CHECK_EQ(f.calls[0], std::string("find"));
    OBL_CHECK_EQ(f.queue.m_tail[0], 70.0f);
    OBL_REQUIRE(f.sent.size() == 2);
    OBL_CHECK(f.sent[1] == (Sent{7, 555, 31, 70, 71, -1.0f}));
}

OBL_TEST_CASE("building_queue_ops.enqueue_reads_the_tail_again_after_the_join_message")
{
    // A script the message ran moved the tail; the cell test and the move message use the new tail.
    struct MovingFixture : Fixture {
        using Fixture::Fixture;
        void Post(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
        {
            Fixture::Post(sender, receiver, type, flags, data);
            if (type == 33) {
                queue.m_tail[0] = 5.0f;
                queue.m_tail[1] = 6.0f;
            }
        }
    } env(1, 5);
    OBL_CHECK(EnqueueGuest(env, env.queue, 555, 3, true));
    OBL_REQUIRE(env.sent.size() == 2);
    OBL_CHECK(env.sent[1] == (Sent{7, 555, 31, 5, 6, -1.0f}));
}

OBL_TEST_CASE("building_queue_ops.dequeue_of_an_empty_queue_is_false")
{
    Fixture f(0, 5);
    uint32_t guest = 7;
    int32_t goods = 7;
    OBL_CHECK(!DequeueGuest(f, f.queue, &guest, &goods));
    OBL_CHECK_EQ(guest, 7u);
    OBL_CHECK(f.sent.empty());
    Fixture g(2, 5);
    g.queue.m_qlCommmod.clear(); // commodity deque empty, guests not
    OBL_CHECK(!DequeueGuest(g, g.queue, &guest, &goods));
    OBL_CHECK_EQ(guest, 7u);
}

OBL_TEST_CASE("building_queue_ops.dequeue_returns_the_head_and_moves_the_rest_up")
{
    Fixture f(3, 5); // guests 100, 101, 102; commodities 0, 1, 2
    uint32_t guest = 0;
    int32_t goods = -1;
    f.rebuilt[0] = 55.5f;
    f.rebuilt[1] = 66.5f;
    OBL_CHECK(DequeueGuest(f, f.queue, &guest, &goods));
    OBL_CHECK_EQ(guest, 100u);
    OBL_CHECK_EQ(goods, 0);
    OBL_CHECK_EQ(f.queue.m_qlQueue.size(), size_t(2));
    OBL_CHECK_EQ(f.queue.m_qlQueue[0], 101);
    // Two guests remain: each hears 3 s later where to stand; RebuildPoints gets their position, not the table index.
    OBL_REQUIRE(f.sent.size() == 2);
    OBL_CHECK(f.sent[0] == (Sent{7, 101, 31, 55, 66, 3.0f}));
    OBL_CHECK(f.sent[1] == (Sent{7, 102, 31, 55, 66, 3.0f}));
    OBL_REQUIRE(f.calls.size() == 2);
    OBL_CHECK_EQ(f.calls[0], std::string("rebuild 0"));
    OBL_CHECK_EQ(f.calls[1], std::string("rebuild 1"));
    OBL_CHECK_EQ(f.queue.m_tail[0], 55.5f);
}

OBL_TEST_CASE("building_queue_ops.dequeue_with_a_single_guest_sends_nothing")
{
    Fixture f(1, 5);
    uint32_t guest = 0;
    int32_t goods = 0;
    OBL_CHECK(DequeueGuest(f, f.queue, &guest, &goods));
    OBL_CHECK(f.sent.empty());
    OBL_CHECK(f.calls.empty());
    OBL_CHECK_EQ(f.queue.m_tail[0], 99.0f); // untouched
}

OBL_TEST_CASE("building_queue_ops.dequeue_without_a_table_computes_the_spot_from_serve_point_and_direction")
{
    SpyFixture env(4, 40, false);
    uint32_t guest = 0;
    int32_t goods = 0;
    OBL_CHECK(DequeueGuest(env, env.queue, &guest, &goods));
    OBL_REQUIRE(env.seen.size() == 3);
    OBL_CHECK_EQ(env.seen[0].first, 10.0f);
    OBL_CHECK_EQ(env.seen[0].second, -4.0f);
    OBL_CHECK_EQ(env.seen[2].first, 15.0f);
    OBL_CHECK_EQ(env.seen[2].second, -3.0f);
}

OBL_TEST_CASE("building_queue_ops.dequeue_with_a_table_reads_entry_i_plus_one")
{
    SpyFixture env(3, 5);
    uint32_t guest = 0;
    int32_t goods = 0;
    OBL_CHECK(DequeueGuest(env, env.queue, &guest, &goods));
    OBL_REQUIRE(env.seen.size() == 2);
    OBL_CHECK_EQ(env.seen[0].first, 1010.0f);
    OBL_CHECK_EQ(env.seen[1].second, 2020.0f);
}

OBL_TEST_CASE("building_queue_ops.tail_point_of_a_queue_without_a_table_is_the_stored_tail")
{
    for (uint32_t limit : {0u, 11u, 40u}) {
        Fixture f(0, limit);
        float out[2];
        QueueTailPoint(f, f.queue, out);
        OBL_CHECK_EQ(out[0], 99.0f);
        OBL_CHECK_EQ(out[1], 98.0f);
        OBL_CHECK(f.calls.empty());
    }
}

OBL_TEST_CASE("building_queue_ops.tail_point_is_the_last_table_entry_when_its_cell_is_fine")
{
    Fixture f(0, 4);
    float out[2];
    QueueTailPoint(f, f.queue, out);
    OBL_CHECK_EQ(out[0], 1040.0f);
    OBL_CHECK_EQ(out[1], 2040.0f);
    OBL_CHECK_EQ(f.queue.m_tail[0], 1040.0f);
    OBL_CHECK(f.calls.empty());
}

OBL_TEST_CASE("building_queue_ops.tail_point_rebuilds_for_unfit_cells")
{
    struct Case {
        uint32_t flags;
        bool rebuild;
    };
    const Case cases[] = {
            {0x00000000, true}, // not walkable
            {0x10000082, false},
            {0x10000090, false}, // 0x10 alone does not matter here
            {0x10000092, true}, // 0x02 and 0x10 together
            {0x10000012, true}, // 0x12 both set
            {0x10008002, true}, // 0x8000
            {0x00000080, false}, // 0x80 is in the walkable mask; neither 0x12 nor 0x8000
            {0x00000100, true}, // no bit of the mask
    };
    for (const Case& c : cases) {
        Fixture f(0, 4);
        f.cell = [&](float, float) { return c.flags; };
        float out[2];
        QueueTailPoint(f, f.queue, out);
        OBL_CHECK_EQ(!f.calls.empty(), c.rebuild);
        if (c.rebuild) OBL_CHECK_EQ(f.calls[0], std::string("rebuild 0"));
    }
}

OBL_TEST_CASE("building_queue_ops.tail_point_reads_the_table_again_after_the_rebuild")
{
    struct RewritingFixture : Fixture {
        using Fixture::Fixture;
        void RebuildPoints(BuildingQueue& q, uint32_t index, float out[2])
        {
            Fixture::RebuildPoints(q, index, out);
            q.m_points[8] = 7.0f; // the table entry for limit 4
            q.m_points[9] = 8.0f;
        }
    } env(0, 4);
    env.cell = [](float, float) { return 0u; };
    float out[2];
    QueueTailPoint(env, env.queue, out);
    OBL_CHECK_EQ(out[0], 7.0f);
    OBL_CHECK_EQ(out[1], 8.0f);
}
