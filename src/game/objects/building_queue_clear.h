#pragma once

#include "game/objects/building_queue_state.h"

#include <cstdint>

// ClearForClosing, ClearForBreakdown and BoardQueuedGuests of the building queue
// (docs/architecture/objects.md): the first two are the same code with a different "spot opinion" kind. Every queued guest hears (MT_SPOT_OPINION = 73, data 6 = the building closed, 7 = it broke down),
// then both deques are emptied and the tail returns to the serve point.
//
// No game memory here: the glue in src/game/objects/building_queue.cpp supplies the Env and the unit tests a fake.
//   size_t GuestCount(const BuildingQueue&)                          the guests in the queue, asked again after every message
//   int32_t GuestAt(const BuildingQueue&, size_t i)                  the i-th queued guest id
//   void PostSpotOpinion(int32_t sender, int32_t guest, int kind)   posts {sender, guest, 73, flags 0, data kind}
// BoardQueuedGuests also needs
//   void PostServeGuest(int32_t sender, int32_t guest, int32_t queued)
//                                                                    posts {sender, guest, 36 (MT_SERVE_GUEST), flags 0, data queued}
//   void PostBoardVehicle(int32_t sender, int32_t queuedGuest, int32_t boardingGuest, float delay)
//                                                                    posts delayed {sender, queuedGuest, 78 (MT_BOARD_VEHICLE), 0, data
//                                                                    boardingGuest}, `delay` seconds
namespace obl {

enum class QueueClearKind : int { Closing = 6, Breakdown = 7 };

template<class Env> void ClearBuildingQueue(Env& env, object::BuildingQueue& queue, QueueClearKind kind)
{
    // The size and the head are re-read after every message (a guest's script may react by leaving the queue).
    for (size_t i = 0; i < env.GuestCount(queue); ++i)
        env.PostSpotOpinion(static_cast<int32_t>(queue.m_ownerId), env.GuestAt(queue, i), static_cast<int>(kind));
    queue.m_qlCommmod.clear();
    queue.m_qlQueue.clear();
    queue.m_tail[0] = queue.m_ServePoint[0];
    queue.m_tail[1] = queue.m_ServePoint[1];
}

// BoardQueuedGuests: the guest `guestId` (the message's sender in the building's ReceiveMessage) is served and the
// queued guests are told to board a vehicle, half a second apart in queue order; then both queues are emptied - the guest queue first, the
// commodity queue second, the other way round from ClearFor* - and the tail returns to the serve point. The count the served guest hears is taken
// before that message (a synchronous script may change the queue); the loop re-reads the live queue like the ClearFor* loop.
template<class Env> void BoardQueuedGuests(Env& env, object::BuildingQueue& queue, int32_t guestId)
{
    env.PostServeGuest(static_cast<int32_t>(queue.m_ownerId), guestId, static_cast<int32_t>(env.GuestCount(queue)));
    for (size_t i = 0; i < env.GuestCount(queue); ++i)
        env.PostBoardVehicle(static_cast<int32_t>(queue.m_ownerId), env.GuestAt(queue, i), guestId, static_cast<float>(i) * 0.5f);
    queue.m_qlQueue.clear();
    queue.m_qlCommmod.clear();
    queue.m_tail[0] = queue.m_ServePoint[0];
    queue.m_tail[1] = queue.m_ServePoint[1];
}

} // namespace obl
