#include "game/objects/building_queue.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/building_queue_clear.h"
#include "game/objects/building_queue_ops.h"
#include "game/objects/building_queue_points.h"
#include "game/map/grid_state.h"
#include "game/objects/building_queue_state.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>

namespace obl::object {
using game::GetGrid;
using game::GetWorld;
using game::Rand;

namespace {

using messages::Message;
using object::Building;
using object::BuildingQueue;
using MessageHandler = messages::Handler;

// The game's side of the Env of building_queue_clear.h: the message goes through the game's Post (delivered synchronously, so the guest's script
// runs inside the call).
class QueueEnv {
public:
    size_t GuestCount(const BuildingQueue& queue) const { return queue.m_qlQueue.size(); }
    int32_t GuestAt(const BuildingQueue& queue, size_t i) const { return queue.m_qlQueue[i]; }
    void PostSpotOpinion(int32_t sender, int32_t guest, int kind) const
    {
        // The three words after the data field are zero (Post writes time itself). Nothing reads them for this type.
        Message message{};
        message.sender = sender;
        message.receiver = guest;
        message.type = messages::MT_SPOT_OPINION;
        message.data = kind;
        messages::message_facade::Post(GetWorld()->m_messageHandler, &message);
    }
    void PostServeGuest(int32_t sender, int32_t guest, int32_t queued) const
    {
        Message message{};
        message.sender = sender;
        message.receiver = guest;
        message.type = messages::MT_SERVE_GUEST;
        message.data = queued;
        messages::message_facade::Post(GetWorld()->m_messageHandler, &message);
    }
    void PostBoardVehicle(int32_t sender, int32_t queuedGuest, int32_t boardingGuest, float delay) const
    {
        Message message{};
        message.sender = sender;
        message.receiver = queuedGuest;
        message.type = messages::MT_BOARD_VEHICLE;
        message.data = boardingGuest;
        messages::message_facade::PostDelayed(GetWorld()->m_messageHandler, &message, delay);
    }

    // building_queue_ops.h
    uint32_t CellFlags(float x, float y) const { return GetGrid()->GetCellAtWorldPoint(x, y)->m_flags; }
    float TableValue(const BuildingQueue& queue, uint32_t k) const { return queue.m_points[k]; }
    void SetTableValue(BuildingQueue& queue, uint32_t k, float value) const { queue.m_points[k] = value; }
    int Rand() const { return game::Rand(); }
    map::GridCell* CellAt(float x, float y) const { return GetGrid()->GetCellAtWorldPoint(x, y); }
    void Post(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data) const
    {
        // Unlike the ClearFor* records, the flags and data words carry the point; the three words after them (Post writes time
        // itself) are zero.
        Message message{};
        message.sender = sender;
        message.receiver = receiver;
        message.type = type;
        message.flags = flags;
        message.data = data;
        messages::message_facade::Post(GetWorld()->m_messageHandler, &message);
    }
    void PostDelayed(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data, float delay) const
    {
        Message message{};
        message.sender = sender;
        message.receiver = receiver;
        message.type = type;
        message.flags = flags;
        message.data = data;
        messages::message_facade::PostDelayed(GetWorld()->m_messageHandler, &message, delay);
    }
    void FindNextPoint(BuildingQueue& queue, float out[2]) { QueueFindNextPoint(*this, queue, out); }
    void RebuildPoints(BuildingQueue& queue, uint32_t index, float out[2]) { QueueRebuildPoints(*this, queue, index, out); }
};

void ClearQueue(BuildingQueue& queue, QueueClearKind kind)
{
    QueueEnv env;
    ClearBuildingQueue(env, queue, kind);
}

// ---- handlers ---------------------------------------------------------------------------------------------------------------

// Call counters (`call-report <name>`).
test::CallCounter g_closing("objects.building-queue-clear-for-closing");
test::CallCounter g_breakdown("objects.building-queue-clear-for-breakdown");
test::CallCounter g_guestsClosing("objects.building-queue-clear-for-closing-with-guests"); // calls that found somebody waiting
test::CallCounter g_guestsBreakdown("objects.building-queue-clear-for-breakdown-with-guests");
test::CallCounter g_board("objects.building-queue-board-queued-guests");
test::CallCounter g_findNext("objects.building-queue-find-next-point");
test::CallCounter g_rebuild("objects.building-queue-rebuild-points");
test::CallCounter g_enqueue("objects.building-queue-enqueue");
test::CallCounter g_dequeue("objects.building-queue-dequeue");
test::CallCounter g_tailPoint("objects.building-queue-get-tail-point");

void RunClearSlot(QueueClearKind kind, test::CallCounter& counter, test::CallCounter& withGuests, BuildingQueue* building)
{
    counter.Count();
    if (!building->m_qlQueue.empty()) withGuests.Count();
    ClearQueue(*building, kind);
}

} // namespace

// A building without a queue has nobody to send away: one under construction, or one a save left the queue out of.
void BuildingQueueClearForClosing(void* queue)
{
    if (!queue) return;
    RunClearSlot(QueueClearKind::Closing, g_closing, g_guestsClosing, static_cast<BuildingQueue*>(queue));
}
void BuildingQueueClearForBreakdown(void* queue)
{
    if (!queue) return;
    RunClearSlot(QueueClearKind::Breakdown, g_breakdown, g_guestsBreakdown, static_cast<BuildingQueue*>(queue));
}

void BuildingQueueBoardQueuedGuests(void* queue, uint32_t guestId)
{
    g_board.Count();
    QueueEnv env;
    BoardQueuedGuests(env, *static_cast<BuildingQueue*>(queue), static_cast<int32_t>(guestId));
}

float* BuildingQueueFindNextPoint(void* queue, float* out)
{
    g_findNext.Count();
    QueueEnv env;
    QueueFindNextPoint(env, *static_cast<BuildingQueue*>(queue), out);
    return out;
}

float* BuildingQueueRebuildPoints(void* queue, float* out, uint32_t index)
{
    g_rebuild.Count();
    QueueEnv env;
    QueueRebuildPoints(env, *static_cast<BuildingQueue*>(queue), index, out);
    return out;
}

bool BuildingQueueEnqueue(void* queue, uint32_t guestId, int32_t commodity, bool moveGuest)
{
    g_enqueue.Count();
    QueueEnv env;
    return EnqueueGuest(env, *static_cast<BuildingQueue*>(queue), guestId, commodity, moveGuest);
}

bool BuildingQueueDequeue(void* queue, uint32_t* guestId, int32_t* commodity)
{
    g_dequeue.Count();
    QueueEnv env;
    return DequeueGuest(env, *static_cast<BuildingQueue*>(queue), guestId, commodity);
}

float* BuildingQueueGetTailPoint(void* queue, float* out)
{
    g_tailPoint.Count();
    QueueEnv env;
    QueueTailPoint(env, *static_cast<BuildingQueue*>(queue), out);
    return out;
}

} // namespace obl::object
