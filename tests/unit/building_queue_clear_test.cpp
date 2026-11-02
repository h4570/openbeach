#include "unit_test.h"

#include "game/objects/building_queue_clear.h"

#include <vector>

using namespace obl;
using object::BuildingQueue;

namespace {

struct Post {
    int32_t sender, guest;
    int kind;
    bool operator==(const Post& o) const { return sender == o.sender && guest == o.guest && kind == o.kind; }
};

struct Boarding {
    int32_t sender, queued, boarding;
    float delay;
};

struct FakeEnv {
    std::vector<Post> posts;
    std::vector<Boarding> boardings;
    std::vector<int32_t> served; // "sender guest queued" of the MT_SERVE_GUEST message
    size_t GuestCount(const BuildingQueue& queue) { return queue.m_qlQueue.size(); }
    int32_t GuestAt(const BuildingQueue& queue, size_t i) { return queue.m_qlQueue[i]; }
    void PostSpotOpinion(int32_t sender, int32_t guest, int kind) { posts.push_back({sender, guest, kind}); }
    void PostServeGuest(int32_t sender, int32_t guest, int32_t queued) { served = {sender, guest, queued}; }
    void PostBoardVehicle(int32_t sender, int32_t queued, int32_t boarding, float delay)
    {
        boardings.push_back({sender, queued, boarding, delay});
    }
};

struct Fixture {
    BuildingQueue queue{};
    FakeEnv env;

    explicit Fixture(size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            queue.m_qlQueue.push_back(static_cast<int32_t>(100 + i));
            queue.m_qlCommmod.push_back(static_cast<int32_t>(i % 5));
        }
        queue.m_ownerId = 7;
        queue.m_maxLength = 12;
        queue.m_ServePoint[0] = 10.0f;
        queue.m_ServePoint[1] = -4.0f;
        queue.m_tail[0] = 99.0f;
        queue.m_tail[1] = 98.0f;
    }
};

} // namespace

OBL_TEST_CASE("building_queue_clear.every_queued_guest_hears_the_kind_in_order")
{
    Fixture f(4);
    ClearBuildingQueue(f.env, f.queue, QueueClearKind::Closing);
    OBL_REQUIRE(f.env.posts.size() == 4);
    for (size_t i = 0; i < 4; ++i) OBL_CHECK(f.env.posts[i] == (Post{7, int32_t(100 + i), 6}));
    Fixture g(2);
    ClearBuildingQueue(g.env, g.queue, QueueClearKind::Breakdown);
    OBL_REQUIRE(g.env.posts.size() == 2);
    OBL_CHECK_EQ(g.env.posts[1].kind, 7);
}

OBL_TEST_CASE("building_queue_clear.both_deques_are_emptied")
{
    Fixture f(70);
    ClearBuildingQueue(f.env, f.queue, QueueClearKind::Breakdown);
    OBL_CHECK_EQ(f.env.posts.size(), size_t(70));
    OBL_CHECK(f.queue.m_qlQueue.empty());
    OBL_CHECK(f.queue.m_qlCommmod.empty());
}

OBL_TEST_CASE("building_queue_clear.empty_queue_sends_nothing_and_resets_the_tail")
{
    Fixture f(0);
    ClearBuildingQueue(f.env, f.queue, QueueClearKind::Closing);
    OBL_CHECK(f.env.posts.empty());
    OBL_CHECK_EQ(f.queue.m_tail[0], 10.0f);
    OBL_CHECK_EQ(f.queue.m_tail[1], -4.0f);
}

OBL_TEST_CASE("building_queue_clear.a_listener_that_shrinks_the_queue_stops_the_loop_early")
{
    Fixture f(6);
    struct ShrinkingEnv : FakeEnv {
        BuildingQueue* queue = nullptr;
        void PostSpotOpinion(int32_t sender, int32_t guest, int kind)
        {
            FakeEnv::PostSpotOpinion(sender, guest, kind);
            if (posts.size() == 2) queue->m_qlQueue.resize(3); // the guests behind left
        }
    } env;
    env.queue = &f.queue;
    ClearBuildingQueue(env, f.queue, QueueClearKind::Closing);
    OBL_CHECK_EQ(env.posts.size(), size_t(3));
}

OBL_TEST_CASE("building_queue_clear.only_the_tail_and_the_deques_change")
{
    Fixture f(3);
    BuildingQueue before = f.queue;
    ClearBuildingQueue(f.env, f.queue, QueueClearKind::Closing);
    OBL_CHECK_EQ(f.queue.m_ownerId, before.m_ownerId);
    OBL_CHECK_EQ(f.queue.m_maxLength, before.m_maxLength);
    OBL_CHECK(f.queue.m_points == before.m_points);
    OBL_CHECK_EQ(f.queue.m_ServePoint[0], before.m_ServePoint[0]);
    OBL_CHECK_EQ(f.queue.m_QueueDirection[1], before.m_QueueDirection[1]);
}

OBL_TEST_CASE("building_queue_clear.board_serves_the_guest_then_tells_every_queued_guest_half_a_second_apart")
{
    Fixture f(4);
    BoardQueuedGuests(f.env, f.queue, 55);
    OBL_REQUIRE(f.env.served.size() == 3);
    OBL_CHECK_EQ(f.env.served[0], 7);
    OBL_CHECK_EQ(f.env.served[1], 55);
    OBL_CHECK_EQ(f.env.served[2], 4); // the queue length before the message
    OBL_REQUIRE(f.env.boardings.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        OBL_CHECK_EQ(f.env.boardings[i].sender, 7);
        OBL_CHECK_EQ(f.env.boardings[i].queued, int32_t(100 + i));
        OBL_CHECK_EQ(f.env.boardings[i].boarding, 55);
        OBL_CHECK_EQ(f.env.boardings[i].delay, float(i) * 0.5f);
    }
    OBL_CHECK(f.env.posts.empty());
}

OBL_TEST_CASE("building_queue_clear.board_empties_both_queues")
{
    Fixture f(70);
    BoardQueuedGuests(f.env, f.queue, 9);
    OBL_CHECK_EQ(f.env.boardings.size(), size_t(70));
    OBL_CHECK_EQ(f.env.boardings[69].delay, 34.5f);
    OBL_CHECK_EQ(f.queue.m_qlQueue.size(), size_t(0));
    OBL_CHECK_EQ(f.queue.m_qlCommmod.size(), size_t(0));
    OBL_CHECK_EQ(f.queue.m_tail[0], 10.0f);
}

OBL_TEST_CASE("building_queue_clear.board_with_an_empty_queue_only_serves")
{
    Fixture f(0);
    BoardQueuedGuests(f.env, f.queue, 12);
    OBL_CHECK_EQ(f.env.served[2], 0);
    OBL_CHECK(f.env.boardings.empty());
}
