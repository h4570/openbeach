#pragma once

#include <cstdint>

namespace obl::world_save {
struct BuildingQueueJson;
} // namespace obl::world_save

namespace obl::object {

// BuildingQueue (docs/architecture/objects.md), the service queue behind Building::m_queue.
//
// ClearForClosing, ClearForBreakdown and BoardQueuedGuests share the logic in
// `src/game/objects/building_queue_clear.h`; Enqueue, Dequeue and GetTailPoint in `src/game/objects/building_queue_ops.h`
// and FindNextPoint / RebuildPoints in `src/game/objects/building_queue_points.h`.
// Construct, Destruct and Load in `src/game/objects/building_queue_lifecycle.h`; the
// functions live in building_queue_lifecycle.cpp. The stream Save is gone (obl.save writes the queue as JSON).

// The queue operations; `queue` is a Building::m_queue (object::BuildingQueue).
void BuildingQueueClearForClosing(void* queue);
void BuildingQueueClearForBreakdown(void* queue);
void BuildingQueueBoardQueuedGuests(void* queue, uint32_t guestId);
float* BuildingQueueFindNextPoint(void* queue, float* out);
float* BuildingQueueRebuildPoints(void* queue, float* out, uint32_t index);
bool BuildingQueueEnqueue(void* queue, uint32_t guestId, int32_t commodity, bool moveGuest);
bool BuildingQueueDequeue(void* queue, uint32_t* guestId, int32_t* commodity);
float* BuildingQueueGetTailPoint(void* queue, float* out);
// Construct runs on a `new object::BuildingQueue`; Delete destructs it and gives it back with `delete`.
void* BuildingQueueConstruct(
        void* queue, uint32_t maxLength, float serveX, float serveY, float directionX, float directionY, uint32_t ownerId);
void BuildingQueueDelete(void* queue);
// Load restores an obl.save building.queue record by applying its values in queue initialization order.
void BuildingQueueLoadJson(void* queue, const world_save::BuildingQueueJson& in);

} // namespace obl::object
