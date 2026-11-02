#include "game/repair/repair_manager.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/repair/repair_lifecycle.h"
#include "game/repair/repair_logic.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/objects/object_static.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace obl::repair {
namespace {

using layout::World;
using object::Prop;
using object::Scenery;
using repair::PlantInfo;
using RepairManager = repair::Manager;

constexpr char kMechanicClass = 8; // ObjectType::Mechanic

// Scenery is object class 1; FindPlantToWater draws from this list.
constexpr int kSceneryClass = 1;

object::Base* ObjectById(int id)
{
    return object::object_manager::ObjectById(game::GetObjectManager(), id);
}

// The game side of the Env contract (src/game/repair/repair_logic.h): object data through the game's own accessors, RNG through the
// game's imported rand().
class GameEnv {
public:
    float GameTime() { return game::GetWorld()->m_gameTime; }
    bool ObjectExists(int id) { return ObjectById(id) != nullptr; }
    uint8_t ObjectClass(int id) { return static_cast<uint8_t>(ObjectById(id)->m_type); }
    bool Waterable(int id) { return static_cast<Scenery*>(ObjectById(id))->m_definition->m_water != 0; }
    int MechanicCount() { return object::object_manager::CountByClass(game::GetObjectManager(), kMechanicClass); }
    void SetDirected(int id) { static_cast<Prop*>(ObjectById(id))->m_urgentRepairCalled = 1; }
    void Position(int id, float out[3]) { std::memcpy(out, ObjectById(id)->m_WorldXYZ, sizeof(float) * 3); }
    bool IsCleanerClaimed(int id) { return static_cast<Prop*>(ObjectById(id))->IsCleanerClaimed(); }
    bool IsMechanicClaimed(int id) { return static_cast<Prop*>(ObjectById(id))->IsMechanicClaimed(); }
    void SetMechanicClaimed(int id, bool claimed)
    {
        if (object::Base* object = ObjectById(id)) static_cast<Prop*>(object)->SetMechanicClaimed(claimed ? 1 : 0);
    }
    int Rand() { return game::Rand(); }

    size_t PlantCount()
    {
        if (!m_plantsRead) {
            m_plants = object::object_manager::GetListByClass(game::GetObjectManager(), kSceneryClass);
            m_plantsRead = true;
        }
        return m_plants.size();
    }
    PlantInfo PlantAt(size_t index)
    {
        const object::Base* object = m_plants[index];
        PlantInfo plant;
        plant.id = static_cast<int>(object->m_id);
        std::memcpy(plant.pos, object->m_WorldXYZ, sizeof(plant.pos));
        plant.waterable = static_cast<const Scenery*>(object)->m_definition->m_water != 0;
        plant.inactive = object->m_waitingForDeath != 0;
        return plant;
    }

private:
    object::object_manager::ObjectList m_plants;
    bool m_plantsRead = false;
};

// ---- the queue methods ---------------------------------------------------------------------------------------------------------------
// Counted per call under the names the call-report tests use; the drivers below are registered on them.

enum Method {
    MethodAddJob,
    MethodAddUrgentJob,
    MethodScanJobs,
    MethodFindJob,
    MethodLookForBetterJob,
    MethodFindWateringJob,
    MethodClaimJob,
    MethodReleaseJob,
    MethodStartedJob,
    MethodReturnJob,
    MethodFindPlantToWater,
    MethodClaimObject,
    MethodGetJobStatus,
    MethodGetJobType,
    MethodCount
};

const char* const kCounterNames[MethodCount] = {"repair.manager-add-job", "repair.manager-add-urgent-job", "repair.manager-scan-jobs",
        "repair.manager-find-job", "repair.manager-look-for-better-job", "repair.manager-find-watering-job", "repair.manager-claim-job",
        "repair.manager-release-job", "repair.manager-started-job", "repair.manager-return-job", "repair.manager-find-plant-to-water",
        "repair.manager-claim-object", "repair.manager-get-job-status", "repair.manager-get-job-type"};

// Calls of the facade (`call-report repair.manager-<method>`).
test::CallCounter g_calls[MethodCount] = {test::CallCounter(kCounterNames[0]), test::CallCounter(kCounterNames[1]),
        test::CallCounter(kCounterNames[2]), test::CallCounter(kCounterNames[3]), test::CallCounter(kCounterNames[4]),
        test::CallCounter(kCounterNames[5]), test::CallCounter(kCounterNames[6]), test::CallCounter(kCounterNames[7]),
        test::CallCounter(kCounterNames[8]), test::CallCounter(kCounterNames[9]), test::CallCounter(kCounterNames[10]),
        test::CallCounter(kCounterNames[11]), test::CallCounter(kCounterNames[12]), test::CallCounter(kCounterNames[13])};

test::CallCounter g_removeJobCalls("repair.manager-remove-job"), g_countUrgentCalls("repair.manager-count-urgent-jobs");
test::CallCounter g_createCalls("repair.manager-create"), g_destroyCalls("repair.manager-destroy");

object::object_manager::ObjectList PlantList()
{
    return object::object_manager::GetListByClass(game::GetObjectManager(), kSceneryClass);
}

} // namespace

namespace repair_facade {

// Every method runs the logic of src/game/repair/ on the manager.

RepairManager* Create()
{
    g_createCalls.Count();
    return new RepairManager();
}

void Destroy(RepairManager* manager)
{
    g_destroyCalls.Count();
    delete manager;
}

void AddJob(RepairManager* manager, int id, uint32_t priority)
{
    g_calls[MethodAddJob].Count();
    GameEnv env;
    repair::AddJob(*manager, env, id, priority);
}

void AddUrgentJob(RepairManager* manager, int id)
{
    g_calls[MethodAddUrgentJob].Count();
    GameEnv env;
    repair::AddUrgentJob(*manager, env, id);
}

void ScanJobs(RepairManager* manager, repair::Scan* result, const float* point, int kind)
{
    g_calls[MethodScanJobs].Count();
    GameEnv env;
    repair::ScanJobs(*manager, env, *result, point, kind);
}

int FindJob(RepairManager* manager, const float* point, int kind, uint32_t* typeOut)
{
    g_calls[MethodFindJob].Count();
    GameEnv env;
    return repair::FindJob(*manager, env, point, kind, typeOut);
}

int LookForBetterJob(RepairManager* manager, const float* point, int currentId, uint32_t* typeOut)
{
    g_calls[MethodLookForBetterJob].Count();
    GameEnv env;
    return repair::LookForBetterJob(*manager, env, point, currentId, typeOut);
}

int FindWateringJob(RepairManager* manager, const float* point)
{
    g_calls[MethodFindWateringJob].Count();
    GameEnv env;
    return repair::FindWateringJob(*manager, env, point);
}

void ClaimJob(RepairManager* manager, int id, uint32_t urgent, uint32_t type)
{
    g_calls[MethodClaimJob].Count();
    GameEnv env;
    repair::ClaimJob(*manager, env, id, static_cast<uint8_t>(urgent), type);
}

void ReleaseJob(RepairManager* manager, int id)
{
    g_calls[MethodReleaseJob].Count();
    GameEnv env;
    repair::ReleaseJob(*manager, env, id);
}

void StartedJob(RepairManager* manager, int id)
{
    g_calls[MethodStartedJob].Count();
    GameEnv env;
    repair::StartedJob(*manager, env, id);
}

void ReturnJob(RepairManager* manager, int id, uint32_t type)
{
    g_calls[MethodReturnJob].Count();
    GameEnv env;
    repair::ReturnJob(*manager, env, id, type);
}

int FindPlantToWater(RepairManager* manager, const float* point, uint32_t radius)
{
    g_calls[MethodFindPlantToWater].Count();
    GameEnv env;
    return repair::FindPlantToWater(*manager, env, point, radius);
}

int ClaimObject(RepairManager* manager, int id, uint32_t* typeOut)
{
    g_calls[MethodClaimObject].Count();
    GameEnv env;
    return repair::ClaimObject(*manager, env, id, typeOut);
}

char GetJobStatus(RepairManager* manager, int id)
{
    g_calls[MethodGetJobStatus].Count();
    GameEnv env;
    return static_cast<char>(repair::GetJobStatus(*manager, env, id));
}

int GetJobType(RepairManager* manager, int id)
{
    g_calls[MethodGetJobType].Count();
    GameEnv env;
    return repair::GetJobType(*manager, env, id);
}

void RemoveJob(RepairManager* manager, int id)
{
    g_removeJobCalls.Count();
    repair::RemoveJob(*manager, id);
}

uint32_t CountUrgentJobs(RepairManager* manager)
{
    g_countUrgentCalls.Count();
    return repair::CountUrgentJobs(*manager);
}

} // namespace repair_facade

namespace {

struct DriveRandom {
    uint32_t seed = 0x2545f491;
    uint32_t Next() { return (seed = seed * 1664525u + 1013904223u) >> 8; }
    float Unit() { return static_cast<float>(Next() & 0xffff) / 65535.0f; }
    float Span(float range) { return (Unit() * 2.0f - 1.0f) * range; }
};

void DrivePlants(int count)
{
    RepairManager* manager = game::GetWorld() ? game::GetWorld()->m_repairManager.get() : nullptr;
    if (!manager) {
        OBL_LOG("autotest: FAILED call-drive repair.manager-find-plant-to-water: no world");
        return;
    }
    const auto list = PlantList();
    const auto plants = static_cast<uint32_t>(list.size());
    if (plants == 0) {
        OBL_LOG("autotest: FAILED call-drive repair.manager-find-plant-to-water: no scenery objects");
        return;
    }
    DriveRandom random;
    static const uint32_t radii[] = {0, 0, 60, 120, 150, 200, 400};
    for (int i = 0; i < count; ++i) {
        const object::Base* anchor = list[random.Next() % plants];
        float point[3] = {anchor->m_WorldXYZ[0] + random.Span(260), anchor->m_WorldXYZ[1] + random.Span(260),
                anchor->m_WorldXYZ[2] + random.Span(60)};
        repair_facade::FindPlantToWater(manager, point, radii[random.Next() % (sizeof(radii) / sizeof(radii[0]))]);
    }
}

void DriveScan(int count)
{
    RepairManager* manager = game::GetWorld() ? game::GetWorld()->m_repairManager.get() : nullptr;
    if (!manager) {
        OBL_LOG("autotest: FAILED call-drive repair.manager-scan-jobs: no world");
        return;
    }
    DriveRandom random;
    for (int i = 0; i < count; ++i) {
        // Aim at a queued object when there is one, otherwise anywhere near the first building.
        float point[3] = {random.Span(900), random.Span(900), random.Span(60)};
        std::vector<int> queued;
        for (const repair::JobList* list : {&manager->buildingJobs, &manager->sceneryJobs})
            for (const repair::Job& job : *list) queued.push_back(job.id);
        if (!queued.empty())
            if (const object::Base* object = ObjectById(queued[random.Next() % queued.size()]))
                for (int axis = 0; axis < 3; ++axis) point[axis] = object->m_WorldXYZ[axis] + random.Span(axis == 2 ? 20.0f : 300.0f);
        repair::Scan scan;
        repair_facade::ScanJobs(manager, &scan, point, static_cast<int>(random.Next() % 5));
    }
}

// A random but valid sequence of calls of every method against the living world: requests for buildings, scenery and a few non-job objects,
// mechanics that find, replace, start, release and return jobs, directed claims, flags set by cleaners, and a clock that moves on so that
// young requests become eligible. It wrecks the scenario (objects stay claimed, the mechanics lose jobs behind their back), so a script ends
// right after it. Only active records are returned or released, as the game's callers do.
void DriveFuzz(int count)
{
    World* world = game::GetWorld();
    object::Manager* objects = game::GetObjectManager();
    if (!world || !objects || !world->m_repairManager) {
        OBL_LOG("autotest: FAILED call-drive repair.manager-find-job: no world");
        return;
    }
    RepairManager* manager = world->m_repairManager;
    std::vector<int> ids;
    for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) ids.push_back(static_cast<int>(objects->m_buildings[i]->m_id));
    size_t buildings = ids.size();
    for (uint32_t i = 0; i < objects->m_others.size() && ids.size() < buildings + 400; ++i)
        if (objects->m_others[i]) ids.push_back(static_cast<int>(objects->m_others[i]->m_id));
    for (uint32_t i = 0; i < objects->m_guests.size() && i < 4; ++i) ids.push_back(static_cast<int>(objects->m_guests[i]->m_id));
    if (ids.empty()) return;

    DriveRandom random;
    auto pick = [&]() { return ids[random.Next() % ids.size()]; };
    auto activeId = [&]() {
        std::vector<int> active;
        for (const repair::ActiveJob& job : manager->activeJobs) active.push_back(job.id);
        return active.empty() ? -1 : active[random.Next() % active.size()];
    };
    auto pointNear = [&](int id, float point[3]) {
        const object::Base* object = ObjectById(id);
        for (int axis = 0; axis < 3; ++axis)
            point[axis] = (object ? object->m_WorldXYZ[axis] : 0.0f) + random.Span(axis == 2 ? 20.0f : 250.0f);
    };
    for (int i = 0; i < count; ++i) {
        world->m_gameTime += random.Unit() * 2.0f;
        int id = pick();
        float point[3];
        pointNear(id, point);
        uint32_t type = 0;
        switch (random.Next() % 15) {
        case 0: repair_facade::AddJob(manager, id, random.Next() % 3); break;
        case 1: repair_facade::AddUrgentJob(manager, id); break;
        case 2: repair_facade::FindJob(manager, point, static_cast<int>(random.Next() % 4), &type); break;
        case 3: {
            int held = activeId();
            if (held >= 0) repair_facade::LookForBetterJob(manager, point, held, &type);
            break;
        }
        case 4: repair_facade::FindWateringJob(manager, point); break;
        case 5: repair_facade::ClaimObject(manager, id, &type); break;
        case 6: {
            int held = activeId();
            if (held >= 0) repair_facade::StartedJob(manager, held);
            break;
        }
        case 7: {
            int held = activeId();
            if (held >= 0) repair_facade::ReleaseJob(manager, held);
            break;
        }
        case 8: {
            int held = activeId();
            if (held >= 0) repair_facade::ReturnJob(manager, held, random.Next() % 4);
            break;
        }
        case 9: repair_facade::ClaimJob(manager, id, random.Next() % 2, random.Next() % 4); break;
        case 10:
            if (object::Base* object = ObjectById(id)) static_cast<Prop*>(object)->m_cleaning = random.Next() % 3 == 0; // a cleaner's claim
            break;
        case 11:
            if (object::Base* object = ObjectById(id)) static_cast<Prop*>(object)->m_repairing = 0; // a mechanic's claim ends
            break;
        case 12:
            if (ObjectById(id)) repair_facade::GetJobStatus(manager, id);
            break;
        case 13: repair_facade::GetJobType(manager, id); break;
        default: {
            repair::Scan scan;
            repair_facade::ScanJobs(manager, &scan, point, static_cast<int>(random.Next() % 4));
            break;
        }
        }
    }
}

// `call-drive repair.manager-remove-job <n>`: removes queued requests (alternately from the front and the back of each pending list), a request
// that is not queued, and counts the urgent jobs after each step. It empties the queues, so a script ends right after it.
void DriveRemove(int count)
{
    World* world = game::GetWorld();
    if (!world || !world->m_repairManager) {
        OBL_LOG("autotest: FAILED call-drive repair.manager-remove-job: no world");
        return;
    }
    RepairManager* manager = world->m_repairManager;
    for (int i = 0; i < count; ++i) {
        const repair::JobList& list = i % 2 ? manager->sceneryJobs : manager->buildingJobs;
        int id = -1 - i; // nothing queued under a negative id
        if (!list.empty() && i % 5 != 4) id = (i % 4 < 2 ? list.front() : list.back()).id;
        repair_facade::RemoveJob(manager, id);
        repair_facade::CountUrgentJobs(manager);
    }
}

} // namespace

bool RegisterRepairTestDrivers()
{
    g_calls[MethodFindPlantToWater].SetDriver(&DrivePlants);
    g_calls[MethodScanJobs].SetDriver(&DriveScan);
    g_calls[MethodFindJob].SetDriver(&DriveFuzz);
    g_removeJobCalls.SetDriver(&DriveRemove);
    return true;
}

} // namespace obl::repair
