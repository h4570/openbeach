#pragma once

#include "game/objects/building_queue_state.h"

#include <cstdint>

// Adding a guest to the queue, removing the next guest and the queue tail point (docs/architecture/objects.md).
// No game memory here: the glue in src/game/objects/building_queue.cpp supplies the Env and the unit tests a fake.
//   uint32_t CellFlags(float x, float y)                 reads the flags of the grid cell containing (x, y)
//   float TableValue(const BuildingQueue&, uint32_t k)    the k-th float of the points table (point i = floats 2i, 2i+1): the game's Env reads
//                                                         m_points (the helpers rewrite it)
//   void Post(sender, receiver, type, flags, data)        posts {sender, receiver, type, flags, data} to the message handler (delivered synchronously)
//   void PostDelayed(sender, receiver, type, flags, data, delay)
// void FindNextPoint(BuildingQueue&, float out[2]) the next stand point of a long queue; writes the
//                                                         direction, draws rand()
//   void RebuildPoints(BuildingQueue&, uint32_t index, float out[2])
// RebuildPoints: recomputes the points table from `index`
// The two deques are std::deque.
namespace obl {

namespace queue_ops {

inline constexpr int32_t kJoinQueue = 33; // MT_JOIN_QUEUE: the guest is in the queue
inline constexpr int32_t kMoveToQueuePoint = 31; // MT_MOVE2QUEUEPOINT: flags = x, data = y (truncated)
inline constexpr int32_t kQueueFull = 32; // MT_QUEUE_FULL
inline constexpr int32_t kBuildingFull = 34; // MT_BUILDING_FULL: the building has no queue at all (limit 0)
inline constexpr uint32_t kMaxPointsLimit = 10; // limits above this have no points table, the tail follows the guests
inline constexpr uint32_t kWalkableMask = 0x10000082;
inline constexpr float kReorderDelay = 3.0f;

} // namespace queue_ops

// EnqueueGuest: a guest joins at the tail. Refused (MT_QUEUE_FULL, or MT_BUILDING_FULL when the limit is 0) when
// either deque is at the limit; otherwise both deques get the guest and the commodity and, when `moveGuest`, the tail moves on - the point table
// entry for the new length (limit <= 10) or FindNextPoint (longer queues) - the guest hears MT_JOIN_QUEUE and then MT_MOVE2QUEUEPOINT to the tail
// (after RebuildPoints when the cell under the tail is not an acceptable stand point).
template<class Env> bool EnqueueGuest(Env& env, object::BuildingQueue& queue, uint32_t guestId, int32_t commodity, bool moveGuest)
{
    using namespace queue_ops;
    const int32_t owner = static_cast<int32_t>(queue.m_ownerId);
    const int32_t guest = static_cast<int32_t>(guestId);
    uint32_t limit = queue.m_maxLength;
    if (static_cast<uint32_t>(queue.m_qlQueue.size()) >= limit) {
        env.Post(owner, guest, limit == 0 ? kBuildingFull : kQueueFull, 0, 0);
        return false;
    }
    limit = queue.m_maxLength;
    if (static_cast<uint32_t>(queue.m_qlCommmod.size()) >= limit) {
        env.Post(owner, guest, limit == 0 ? kBuildingFull : kQueueFull, 0, 0);
        return false;
    }
    queue.m_qlQueue.push_back(guest);
    queue.m_qlCommmod.push_back(commodity);
    if (!moveGuest) return true;

    if (queue.m_maxLength <= kMaxPointsLimit) {
        const uint32_t length = static_cast<uint32_t>(queue.m_qlQueue.size());
        queue.m_tail[0] = env.TableValue(queue, length * 2);
        queue.m_tail[1] = env.TableValue(queue, length * 2 + 1);
    } else {
        float next[2];
        env.FindNextPoint(queue, next);
        queue.m_tail[0] = next[0];
        queue.m_tail[1] = next[1];
    }
    env.Post(owner, guest, kJoinQueue, 0, 0);

    // The tail is re-read at every step: a script the message ran may have moved it.
    const uint32_t first = env.CellFlags(queue.m_tail[0], queue.m_tail[1]);
    bool stands = false;
    if (first & kWalkableMask) {
        if (!(env.CellFlags(queue.m_tail[0], queue.m_tail[1]) & 0x10)) stands = !(env.CellFlags(queue.m_tail[0], queue.m_tail[1]) & 0x8000);
    }
    if (!stands) {
        float rebuilt[2];
        env.RebuildPoints(queue, static_cast<uint32_t>(queue.m_qlQueue.size()), rebuilt);
        queue.m_tail[0] = rebuilt[0];
        queue.m_tail[1] = rebuilt[1];
    }
    env.Post(owner, guest, kMoveToQueuePoint, static_cast<int32_t>(queue.m_tail[0]), static_cast<int32_t>(queue.m_tail[1]));
    return true;
}

// DequeueGuest: the head guest and its commodity leave; false when either deque is empty. Every guest behind moves up:
// the tail takes the spot of the guest at that position (the points table entry i + 1, or serve point + i x direction for a queue without a table),
// RebuildPoints(i) corrects it and the guest hears MT_MOVE2QUEUEPOINT to it 3 s later (PostDelayed). No cell checks are made around the
// tail (they would have no effect).
template<class Env> bool DequeueGuest(Env& env, object::BuildingQueue& queue, uint32_t* guestId, int32_t* commodity)
{
    using namespace queue_ops;
    if (static_cast<uint32_t>(queue.m_qlQueue.size()) == 0 || static_cast<uint32_t>(queue.m_qlCommmod.size()) == 0) return false;
    *guestId = static_cast<uint32_t>(queue.m_qlQueue.front());
    *commodity = queue.m_qlCommmod.front();
    queue.m_qlQueue.pop_front();
    queue.m_qlCommmod.pop_front();
    for (uint32_t i = 0; i < static_cast<uint32_t>(queue.m_qlQueue.size()); ++i) {
        if (queue.m_points) {
            queue.m_tail[0] = env.TableValue(queue, (i + 1) * 2);
            queue.m_tail[1] = env.TableValue(queue, (i + 1) * 2 + 1);
        } else {
            // At 53-bit precision: the product and the sum are rounded to double, only the store rounds to float.
            const double index = static_cast<double>(i);
            queue.m_tail[0] = static_cast<float>(index * queue.m_QueueDirection[0] + queue.m_ServePoint[0]);
            queue.m_tail[1] = static_cast<float>(index * queue.m_QueueDirection[1] + queue.m_ServePoint[1]);
        }
        float rebuilt[2];
        env.RebuildPoints(queue, i, rebuilt);
        queue.m_tail[0] = rebuilt[0];
        queue.m_tail[1] = rebuilt[1];
        env.PostDelayed(static_cast<int32_t>(queue.m_ownerId), queue.m_qlQueue[i], kMoveToQueuePoint, static_cast<int32_t>(queue.m_tail[0]),
                static_cast<int32_t>(queue.m_tail[1]), kReorderDelay);
    }
    return true;
}

// QueueTailPoint: where the next guest should stand. For a queue with a points table (limit 1..10) the tail is
// the table entry for the limit - the last place; when the cell under it is not walkable, or is a path end (0x12 both set) or carries 0x8000,
// RebuildPoints(0) recomputes the table first. Longer queues and limit 0 return the stored tail.
template<class Env> void QueueTailPoint(Env& env, object::BuildingQueue& queue, float out[2])
{
    using namespace queue_ops;
    const uint32_t limit = queue.m_maxLength;
    if (limit <= kMaxPointsLimit && limit != 0) {
        queue.m_tail[0] = env.TableValue(queue, limit * 2);
        queue.m_tail[1] = env.TableValue(queue, limit * 2 + 1);
        bool rebuild = true;
        if (env.CellFlags(queue.m_tail[0], queue.m_tail[1]) & kWalkableMask) {
            if ((env.CellFlags(queue.m_tail[0], queue.m_tail[1]) & 0x12) != 0x12)
                rebuild = (env.CellFlags(queue.m_tail[0], queue.m_tail[1]) & 0x8000) != 0;
        }
        if (rebuild) {
            float unused[2];
            env.RebuildPoints(queue, 0, unused);
            queue.m_tail[0] = env.TableValue(queue, queue.m_maxLength * 2);
            queue.m_tail[1] = env.TableValue(queue, queue.m_maxLength * 2 + 1);
        }
    }
    out[0] = queue.m_tail[0];
    out[1] = queue.m_tail[1];
}

} // namespace obl
