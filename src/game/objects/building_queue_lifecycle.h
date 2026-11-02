#pragma once

#include "game/objects/building_queue_clear.h"
#include "game/objects/building_queue_ops.h"
#include "game/objects/building_queue_state.h"

#include <cstdint>

// Queue initialization, teardown and save loading share an environment for game services.
// The stream Save is gone: obl.save writes the queue as JSON. No game memory here: the glue in src/game/objects/building_queue_lifecycle.cpp supplies the Env and the unit tests a fake.
// Construct and Destruct need
// void SetStepDirections(const float (&)[16]) the 16 floats (g_QueueStepDirections) the point helpers read
//   float* AllocatePoints(uint32_t bytes)        the game's operator new (Destruct gives the table back with operator delete)
//   void RebuildPoints(BuildingQueue&, float out[2])
//                                                rebuilds queue points with position offset zero
//   void DeletePoints(float*)                    the game's operator delete
// Load needs
// ReadInt (27), ReadUInt (26) and ReadFloat (29) as `(destination*)`.
// The deques are std::deque members: the queue is built with `new BuildingQueue` and freed with `delete`.
namespace obl {

namespace queue_lifecycle {

// The four diagonal steps of a queue and their doubles, in the order they are written: W, E, A,
// B, then 2W, 2E, 2A, 2B. Indexed by (address -) / 4. The doubles are
// written and never read.
inline constexpr float kStepDirections[16] = {
        -16.0f,
        -16.0f, // W
        -32.0f,
        -32.0f, // 2W
        16.0f,
        -16.0f, // A
        32.0f,
        -32.0f, // 2A
        32.0f,
        32.0f, // 2E
        -16.0f,
        16.0f, // B
        16.0f,
        16.0f, // E
        -32.0f,
        32.0f, // 2B
};

} // namespace queue_lifecycle

// ConstructBuildingQueue (returns the queue), called on the record the building's exit-state handler or its load got from
// operator new. A limit of 1..10 gets a points table of limit + 1 points, computed at once by RebuildPoints(0) (its result is dropped). The tail is then NOT written: it keeps whatever the allocation held until the first Enqueue/Dequeue writes it. Any
// other limit has no table and the tail starts at the serve point.
template<class Env>
object::BuildingQueue* ConstructBuildingQueue(Env& env, object::BuildingQueue& queue, uint32_t maxLength, float serveX, float serveY,
        float directionX, float directionY, uint32_t ownerId)
{
    queue.m_maxLength = maxLength;
    queue.m_ServePoint[0] = serveX;
    queue.m_ServePoint[1] = serveY;
    queue.m_QueueDirection[0] = directionX;
    queue.m_QueueDirection[1] = directionY;
    queue.m_ownerId = ownerId;
    env.SetStepDirections(queue_lifecycle::kStepDirections);
    if (queue.m_maxLength >= 1 && queue.m_maxLength <= queue_ops::kMaxPointsLimit) {
        queue.m_points = env.AllocatePoints(queue.m_maxLength * 8 + 8);
        float ignored[2];
        env.RebuildPoints(queue, ignored);
    } else {
        queue.m_points = nullptr;
        queue.m_tail[0] = queue.m_ServePoint[0];
        queue.m_tail[1] = queue.m_ServePoint[1];
    }
    return &queue;
}

// DestructBuildingQueue (the building's destructor frees the record afterwards): both deques are cleared and the points table goes back
// to operator delete; the deques themselves go with the record's `delete`.
template<class Env> void DestructBuildingQueue(Env& env, object::BuildingQueue& queue)
{
    queue.m_qlQueue.clear();
    queue.m_qlCommmod.clear();
    if (queue.m_points) env.DeletePoints(queue.m_points);
}

// LoadBuildingQueue, called by the building's load right after Construct (with the limit of the building's definition). The header goes
// straight into the fields; the limit read here does not reallocate the points table and neither the tail nor the table are recomputed (both stay
// what Construct made of the definition's limit). Each deque is cleared just before its own elements are read and appended. The counts are read
// unsigned and compared signed, so a count of 0x80000000 or more reads no elements.
template<class Env> void LoadBuildingQueue(Env& env, object::BuildingQueue& queue)
{
    env.ReadInt(reinterpret_cast<int32_t*>(&queue.m_ownerId));
    env.ReadInt(reinterpret_cast<int32_t*>(&queue.m_maxLength));
    env.ReadFloat(&queue.m_ServePoint[0]);
    env.ReadFloat(&queue.m_ServePoint[1]);
    env.ReadFloat(&queue.m_QueueDirection[0]);
    env.ReadFloat(&queue.m_QueueDirection[1]);
    // One count and one value slot for both deques: a read that does not write its destination keeps the previous value (both start at 0).
    uint32_t count = 0;
    int32_t value = 0;
    auto readDeque = [&](std::deque<int32_t>& deque) {
        deque.clear();
        env.ReadUInt(&count);
        for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
            env.ReadInt(&value);
            deque.push_back(value);
        }
    };
    readDeque(queue.m_qlQueue);
    readDeque(queue.m_qlCommmod);
}

} // namespace obl
