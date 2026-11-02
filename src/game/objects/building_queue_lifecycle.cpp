#include "game/objects/building_queue.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_queue_lifecycle.h"
#include "game/world/save_objects_load.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Queue initialization, teardown and save loading use the logic in
// src/game/objects/building_queue_lifecycle.h, this adapter supplies the game services for queue initialization, teardown and loading.
namespace obl::object {
using game::GetContext;

namespace {

using object::Building;
using object::BuildingQueue;

// Call counters (`call-report <name>`).
test::CallCounter g_construct("objects.building-queue-construct");
test::CallCounter g_destruct("objects.building-queue-destruct");
test::CallCounter g_load("objects.building-queue-load");

// The game's side of building_queue_lifecycle.h: the points table on the game's heap.
class LifecycleEnv {
public:
    void SetStepDirections(const float (&values)[16]) { std::memcpy(GetContext().queueStepDirections.data(), values, sizeof(values)); }
    float* AllocatePoints(uint32_t bytes) { return static_cast<float*>(::operator new(bytes)); }
    void RebuildPoints(BuildingQueue& queue, float out[2]) { BuildingQueueRebuildPoints(&queue, out, 0); }
    void DeletePoints(float* points) { ::operator delete(points); }
};

} // namespace

void* BuildingQueueConstruct(
        void* queue, uint32_t maxLength, float serveX, float serveY, float directionX, float directionY, uint32_t ownerId)
{
    g_construct.Count();
    LifecycleEnv env;
    return ConstructBuildingQueue(env, *static_cast<BuildingQueue*>(queue), maxLength, serveX, serveY, directionX, directionY, ownerId);
}

void BuildingQueueDelete(void* queue)
{
    g_destruct.Count();
    LifecycleEnv env;
    auto* const record = static_cast<BuildingQueue*>(queue);
    DestructBuildingQueue(env, *record);
    delete record;
}

namespace {
// LoadBuildingQueue's reads answered from an obl.save entry's values in the record order (the deque counts before their elements).
class JsonEnv : public LifecycleEnv {
public:
    explicit JsonEnv(const world_save::BuildingQueueJson& in)
    {
        auto add = [this](auto value) {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            m_values.push_back(bits);
        };
        add(in.owner);
        add(in.maxLength);
        add(in.servePoint[0]);
        add(in.servePoint[1]);
        add(in.direction[0]);
        add(in.direction[1]);
        for (const auto* list : {&in.guests, &in.commodities}) {
            add(static_cast<uint32_t>(list->size()));
            for (const int32_t id : *list) add(id);
        }
    }
    void ReadInt(int32_t* destination) { std::memcpy(destination, &m_values[m_next++], 4); }
    void ReadUInt(uint32_t* destination) { *destination = m_values[m_next++]; }
    void ReadFloat(float* destination) { std::memcpy(destination, &m_values[m_next++], 4); }

private:
    std::vector<uint32_t> m_values;
    size_t m_next = 0;
};
} // namespace

void BuildingQueueLoadJson(void* queue, const world_save::BuildingQueueJson& in)
{
    g_load.Count();
    JsonEnv env(in);
    LoadBuildingQueue(env, *static_cast<BuildingQueue*>(queue));
}

namespace {

// ---- handlers ---------------------------------------------------------------------------------------------------------------------------------

} // namespace

} // namespace obl::object
