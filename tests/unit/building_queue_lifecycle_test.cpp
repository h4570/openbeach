#include "unit_test.h"

#include "game/objects/building_queue_lifecycle.h"
#include "game/objects/building_queue_points.h"

#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using object::BuildingQueue;

namespace {

// One call the code made of the Env, as text: the order of the allocator and stream calls is what the tests pin down.
struct Env {
    std::vector<std::string> calls;
    float directions[16] = {};
    std::vector<std::unique_ptr<std::vector<float>>> tables;
    // Load: what the stream hands out, in order (all reads take 4 bytes).
    std::vector<uint32_t> feed;
    size_t fed = 0;

    void SetStepDirections(const float (&values)[16])
    {
        std::memcpy(directions, values, sizeof(directions));
        calls.push_back("directions");
    }
    float* AllocatePoints(uint32_t bytes)
    {
        tables.push_back(std::make_unique<std::vector<float>>(bytes / 4, -1.0f));
        calls.push_back("new " + std::to_string(bytes));
        return tables.back()->data();
    }
    void RebuildPoints(BuildingQueue& queue, float out[2])
    {
        calls.push_back("rebuild");
        queue.m_points[0] = 5.0f; // a recognisable entry
        out[0] = out[1] = 0.0f;
    }
    void DeletePoints(float*) { calls.push_back("delete points"); }

    uint32_t Next() { return fed < feed.size() ? feed[fed++] : 0xdeadbeefu; }
    void ReadInt(int32_t* destination)
    {
        *destination = static_cast<int32_t>(Next());
        calls.push_back("read int");
    }
    void ReadUInt(uint32_t* destination)
    {
        *destination = Next();
        calls.push_back("read uint");
    }
    void ReadFloat(float* destination)
    {
        const uint32_t bits = Next();
        std::memcpy(destination, &bits, 4);
        calls.push_back("read float");
    }
};

std::vector<int32_t> Contents(const std::deque<int32_t>& deque)
{
    return {deque.begin(), deque.end()};
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

} // namespace

OBL_TEST_CASE("building_queue_lifecycle.construct_with_points_table")
{
    Env env;
    BuildingQueue queue;
    std::memset(&queue.m_tail, 0xcd, sizeof(queue.m_tail)); // what the allocation held
    BuildingQueue* result = ConstructBuildingQueue(env, queue, 4, 100.0f, 200.0f, 16.0f, -16.0f, 33);
    OBL_CHECK(result == &queue);
    OBL_CHECK_EQ(queue.m_ownerId, 33u);
    OBL_CHECK_EQ(queue.m_maxLength, 4u);
    OBL_CHECK_EQ(queue.m_ServePoint[0], 100.0f);
    OBL_CHECK_EQ(queue.m_ServePoint[1], 200.0f);
    OBL_CHECK_EQ(queue.m_QueueDirection[0], 16.0f);
    OBL_CHECK_EQ(queue.m_QueueDirection[1], -16.0f);
    // The table holds limit + 1 points; the tail keeps what the allocation held.
    OBL_CHECK(env.calls == std::vector<std::string>({"directions", "new 40", "rebuild"}));
    OBL_CHECK(queue.m_points == env.tables[0]->data());
    OBL_CHECK_EQ(queue.m_points[0], 5.0f);
    OBL_CHECK_EQ(Bits(queue.m_tail[0]), 0xcdcdcdcdu);
    OBL_CHECK_EQ(Bits(queue.m_tail[1]), 0xcdcdcdcdu);
    OBL_CHECK(queue.m_qlQueue.empty());
    OBL_CHECK(queue.m_qlCommmod.empty());
}

OBL_TEST_CASE("building_queue_lifecycle.construct_limits_without_table")
{
    for (uint32_t limit : {0u, 11u, 40u, 0xffffffffu}) {
        Env env;
        BuildingQueue queue;
        std::memset(&queue.m_tail, 0xcd, sizeof(queue.m_tail));
        ConstructBuildingQueue(env, queue, limit, 7.5f, -3.0f, -16.0f, 16.0f, 2);
        OBL_CHECK(queue.m_points == nullptr);
        OBL_CHECK_EQ(queue.m_tail[0], 7.5f);
        OBL_CHECK_EQ(queue.m_tail[1], -3.0f);
        OBL_CHECK(env.calls == std::vector<std::string>({"directions"}));
    }
    Env ten;
    BuildingQueue queue{};
    ConstructBuildingQueue(ten, queue, 10, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    OBL_CHECK_EQ(ten.calls[1], std::string("new 88"));
}

OBL_TEST_CASE("building_queue_lifecycle.step_directions")
{
    Env env;
    BuildingQueue queue{};
    ConstructBuildingQueue(env, queue, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    // The point helpers read W, E, A, B (the ones the point helpers read).
    auto at = [&](int k, float x, float y) {
        OBL_CHECK_EQ(env.directions[k], x);
        OBL_CHECK_EQ(env.directions[k + 1], y);
    };
    at(0, -16.0f, -16.0f);
    at(12, 16.0f, 16.0f);
    at(4, 16.0f, -16.0f);
    at(10, -16.0f, 16.0f);
    at(2, -32.0f, -32.0f);
    at(8, 32.0f, 32.0f);
    at(6, 32.0f, -32.0f);
    at(14, -32.0f, 32.0f);
    OBL_CHECK(queue_points::kW.x == env.directions[0] && queue_points::kW.y == env.directions[1]);
}

OBL_TEST_CASE("building_queue_lifecycle.destruct_clears_and_frees_the_table")
{
    BuildingQueue queue{};
    queue.m_qlQueue.assign(40, 1);
    queue.m_qlCommmod.assign(3, 2);
    float table[6] = {};
    queue.m_points = table;
    Env env;
    DestructBuildingQueue(env, queue);
    OBL_CHECK(env.calls == std::vector<std::string>({"delete points"}));
    OBL_CHECK(queue.m_qlQueue.empty());
    OBL_CHECK(queue.m_qlCommmod.empty());

    BuildingQueue empty{};
    Env none;
    DestructBuildingQueue(none, empty);
    OBL_CHECK(none.calls.empty()); // no table
}

OBL_TEST_CASE("building_queue_lifecycle.load_replaces_deques")
{
    // Old contents are replaced deque by deque.
    BuildingQueue queue{};
    queue.m_qlQueue.assign(35, 9);
    queue.m_qlCommmod.assign(2, 1);
    queue.m_tail[0] = 77.0f;
    float table[4] = {};
    queue.m_points = table;
    Env env;
    env.feed = {41, 3, Bits(1.0f), Bits(2.0f), Bits(16.0f), Bits(-16.0f), 2, 501, 502, 3, 2, 0, 4};
    LoadBuildingQueue(env, queue);
    OBL_CHECK_EQ(queue.m_ownerId, 41u);
    OBL_CHECK_EQ(queue.m_maxLength, 3u);
    OBL_CHECK_EQ(queue.m_ServePoint[1], 2.0f);
    OBL_CHECK_EQ(queue.m_QueueDirection[1], -16.0f);
    OBL_CHECK(Contents(queue.m_qlQueue) == std::vector<int32_t>({501, 502}));
    OBL_CHECK(Contents(queue.m_qlCommmod) == std::vector<int32_t>({2, 0, 4}));
    OBL_CHECK_EQ(queue.m_tail[0], 77.0f); // neither the tail nor the table are touched
    OBL_CHECK(queue.m_points == table);
    const std::vector<std::string> expected = {"read int", "read int", "read float", "read float", "read float", "read float", "read uint",
            "read int", "read int", "read uint", "read int", "read int", "read int"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("building_queue_lifecycle.load_signed_count")
{
    BuildingQueue queue{};
    Env env;
    // 5 guests; commodities: a count of 0x80000000 is negative and reads nothing.
    env.feed = {0, 0, 0, 0, 0, 0, 5, 1, 2, 3, 4, 5, 0x80000000u};
    LoadBuildingQueue(env, queue);
    OBL_CHECK(Contents(queue.m_qlQueue) == std::vector<int32_t>({1, 2, 3, 4, 5}));
    OBL_CHECK(queue.m_qlCommmod.empty());
    OBL_CHECK_EQ(env.fed, env.feed.size());
}

OBL_TEST_CASE("building_queue_lifecycle.load_shared_slots")
{
    struct Lazy : Env {
        void ReadUInt(uint32_t* destination)
        {
            calls.push_back("read uint");
            if (fed == 6) *destination = Next();
        }
        void ReadInt(int32_t* destination)
        {
            calls.push_back("read int");
            if (fed < 7) *destination = static_cast<int32_t>(Next());
        }
    } env;
    BuildingQueue queue{};
    env.feed = {0, 0, 0, 0, 0, 0, 2};
    LoadBuildingQueue(env, queue);
    OBL_CHECK(Contents(queue.m_qlQueue) == std::vector<int32_t>({0, 0}));
    OBL_CHECK(Contents(queue.m_qlCommmod) == std::vector<int32_t>({0, 0}));
}
