#include "game/cleaning/cleaning_manager.h"
#include "game/objects/object_base.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/cleaning/cleaning_lifecycle.h"
#include "game/cleaning/cleaning_logic.h"

#include "game/game/game_accessors.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "game/ui/map_view_math.h"
#include "game/objects/object_static.h"

namespace obl::cleaning {
namespace {

using object::Building;
using object::Prop;
using object::Scenery;
using world_data::World;

using BuildingJobPredicate = bool (*)(const Building*);
const BuildingJobPredicate kHasBuildJob{&object::building_api::HasBuildJob};
const BuildingJobPredicate kHasDemolishJob{&object::building_api::HasDemolishJob};

constexpr char kCleanerClass = 0xb; // ObjectType::Cleaner

object::Base* ObjectById(int id)
{
    return object::object_manager::ObjectById(game::GetObjectManager(), id);
}

// The game side of the Env contract (src/game/cleaning/cleaning_logic.h).
class GameEnv {
public:
    float GameTime() { return game::GetWorld()->m_gameTime; }
    bool ObjectExists(int id) { return ObjectById(id) != nullptr; }
    uint8_t ObjectClass(int id) { return static_cast<uint8_t>(ObjectById(id)->m_type); }
    const char* ObjectName(int id) { return ObjectById(id)->m_Name; }
    BuildingBehavior ObjectBehavior(int id)
    {
        object::Base* object = ObjectById(id);
        if (static_cast<uint8_t>(object->m_type) != static_cast<uint8_t>(object::ObjectType::Building)) return BuildingBehavior::None;
        return static_cast<Building*>(object)->m_definition->m_Behavior;
    }
    bool IsCleanerClaimed(int id) { return static_cast<Prop*>(ObjectById(id))->IsCleanerClaimed(); }
    bool IsMechanicClaimed(int id) { return static_cast<Prop*>(ObjectById(id))->IsMechanicClaimed(); }
    void SetCleanerClaimed(int id, bool claimed) { static_cast<Prop*>(ObjectById(id))->SetCleanerClaimed(claimed ? 1 : 0); }
    void SetCleaningDirected(int id) { static_cast<Prop*>(ObjectById(id))->m_urgentCleaningCalled = 1; }
    bool Vandalised(int id) { return static_cast<Scenery*>(ObjectById(id))->m_vandalised != 0; }
    bool HasBuildJob(int id) { return kHasBuildJob(static_cast<Building*>(ObjectById(id))); }
    bool HasDemolishJob(int id) { return kHasDemolishJob(static_cast<Building*>(ObjectById(id))); }
    void ObjectTile(int id, int out[2]) { ObjectById(id)->GetTilePosition(out); }
    void WorldToTile(const float* point, int out[2]) { ui::MapView::WorldToTile(out, point); }
    int CleanerCount() { return object::object_manager::CountByClass(game::GetObjectManager(), kCleanerClass); }
};

// ---- the queue methods ---------------------------------------------------------------------------------------------------------------

enum Method {
    MethodAddJob,
    MethodAddUrgentJob,
    MethodClaimJob,
    MethodReleaseJob,
    MethodRemoveJob,
    MethodGetJobStatus,
    MethodStartedCleaning,
    MethodDidNotClean,
    MethodFindUrgentJob,
    MethodFindBestScoredJob,
    MethodClaimObject,
    MethodCount
};

const char* const kCounterNames[MethodCount] = {"cleaning.manager-add-job", "cleaning.manager-add-urgent-job", "cleaning.manager-claim-job",
        "cleaning.manager-release-job", "cleaning.manager-remove-job", "cleaning.manager-get-job-status",
        "cleaning.manager-started-cleaning", "cleaning.manager-did-not-clean", "cleaning.manager-find-urgent-job",
        "cleaning.manager-find-best-scored-job", "cleaning.manager-claim-object"};

// Call counters (`call-report <name>`).
test::CallCounter g_calls[MethodCount] = {test::CallCounter(kCounterNames[0]), test::CallCounter(kCounterNames[1]),
        test::CallCounter(kCounterNames[2]), test::CallCounter(kCounterNames[3]), test::CallCounter(kCounterNames[4]),
        test::CallCounter(kCounterNames[5]), test::CallCounter(kCounterNames[6]), test::CallCounter(kCounterNames[7]),
        test::CallCounter(kCounterNames[8]), test::CallCounter(kCounterNames[9]), test::CallCounter(kCounterNames[10])};

test::CallCounter g_cancelCalls("cleaning.manager-cancel-job");

// Methods that take one object id and return nothing share their shape.
template<class Logic> void IdMethod(Method method, cleaning::Manager* manager, Logic&& logic)
{
    g_calls[method].Count();
    GameEnv env;
    logic(*manager, env);
}

} // namespace

namespace cleaning_facade {

// Every method runs the logic of src/game/cleaning/ on the manager.

void AddJob(cleaning::Manager* manager, int id)
{
    IdMethod(MethodAddJob, manager, [&](cleaning::Manager& m, auto& env) { cleaning::AddJob(m, env, id); });
}

void AddUrgentJob(cleaning::Manager* manager, int id)
{
    IdMethod(MethodAddUrgentJob, manager, [&](cleaning::Manager& m, auto& env) { cleaning::AddUrgentJob(m, env, id); });
}

void ReleaseJob(cleaning::Manager* manager, int id)
{
    g_calls[MethodReleaseJob].Count();
    cleaning::ReleaseJob(*manager, id);
}

// The cancel-job wrapper of the cleaning manager is a one-call wrapper of ReleaseJob; it counts itself and then the release.
void CancelJob(cleaning::Manager* manager, int id)
{
    g_cancelCalls.Count();
    ReleaseJob(manager, id);
}

void RemoveJob(cleaning::Manager* manager, int id)
{
    g_calls[MethodRemoveJob].Count();
    cleaning::RemoveJob(*manager, id);
}

void StartedCleaning(cleaning::Manager* manager, int id)
{
    IdMethod(MethodStartedCleaning, manager, [&](cleaning::Manager& m, auto& env) { cleaning::StartedCleaning(m, env, id); });
}

void DidNotClean(cleaning::Manager* manager, int id)
{
    IdMethod(MethodDidNotClean, manager, [&](cleaning::Manager& m, auto& env) { cleaning::DidNotClean(m, env, id); });
}

void ClaimJob(cleaning::Manager* manager, int id, uint32_t urgent)
{
    g_calls[MethodClaimJob].Count();
    cleaning::ClaimJob(*manager, id, static_cast<uint8_t>(urgent));
}

int GetJobStatus(cleaning::Manager* manager, int id)
{
    g_calls[MethodGetJobStatus].Count();
    GameEnv env;
    return cleaning::GetJobStatus(*manager, env, id);
}

int ClaimObject(cleaning::Manager* manager, int id)
{
    g_calls[MethodClaimObject].Count();
    GameEnv env;
    return cleaning::ClaimObject(*manager, env, id);
}

int FindUrgentJob(cleaning::Manager* manager, int* typeOut, bool bins, bool scenery, bool buildings)
{
    g_calls[MethodFindUrgentJob].Count();
    GameEnv env;
    return cleaning::FindUrgentJob(*manager, env, typeOut, bins, scenery, buildings);
}

int FindBestScoredJob(cleaning::Manager* manager, const float* point, int* typeOut, bool bins, bool litter, bool toilets)
{
    g_calls[MethodFindBestScoredJob].Count();
    GameEnv env;
    return cleaning::FindBestScoredJob(*manager, env, point, typeOut, bins, litter, toilets);
}

} // namespace cleaning_facade

namespace {

struct DriveRandom {
    uint32_t seed = 0x2545f491;
    uint32_t Next() { return (seed = seed * 1664525u + 1013904223u) >> 8; }
    float Unit() { return static_cast<float>(Next() & 0xffff) / 65535.0f; }
    float Span(float range) { return (Unit() * 2.0f - 1.0f) * range; }
};

void DriveFuzz(int count)
{
    World* world = game::GetWorld();
    object::Manager* objects = game::GetObjectManager();
    if (!world || !objects || !world->m_cleaningManager) {
        OBL_LOG("autotest: FAILED call-drive cleaning.manager-add-job: no world");
        return;
    }
    cleaning::Manager* manager = world->m_cleaningManager;
    auto known = [](const object::Base* object) { return cleaning::TypeOfName(object->m_Name) != cleaning::kTypeNone; };
    std::vector<int> queueable, others;
    for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) {
        const object::Base* object = objects->m_buildings[i];
        (known(object) ? queueable : others).push_back(static_cast<int>(object->m_id));
    }
    for (uint32_t i = 0; i < objects->m_others.size(); ++i) {
        const object::Base* object = objects->m_others[i];
        if (!object || object->m_type != object::ObjectType::Scenery) continue;
        if (known(object) && queueable.size() < 300)
            queueable.push_back(static_cast<int>(object->m_id));
        else if (others.size() < 40)
            others.push_back(static_cast<int>(object->m_id));
    }
    if (queueable.empty()) {
        OBL_LOG("autotest: FAILED call-drive cleaning.manager-add-job: no bins, litter or toilets in the world");
        return;
    }
    DriveRandom random;
    auto pick = [&]() { return queueable[random.Next() % queueable.size()]; };
    auto pickAny = [&]() { return random.Next() % 4 == 0 && !others.empty() ? others[random.Next() % others.size()] : pick(); };
    auto activeId = [&]() {
        std::vector<int> active;
        for (const cleaning::ActiveJob& job : manager->activeJobs) {
            const object::Base* object = ObjectById(job.id);
            if (object && known(object)) active.push_back(job.id); // a directed claim of another object stays active for good
        }
        return active.empty() ? -1 : active[random.Next() % active.size()];
    };
    for (int i = 0; i < count; ++i) {
        world->m_gameTime += random.Unit() * 2.0f;
        int id = pick();
        object::Base* object = ObjectById(id);
        float point[3] = {object->m_WorldXYZ[0] + random.Span(300), object->m_WorldXYZ[1] + random.Span(300), object->m_WorldXYZ[2]};
        int type = 0;
        auto flags = [&]() { return random.Next() % 4 != 0; };
        switch (random.Next() % 14) {
        case 0: cleaning_facade::AddJob(manager, id); break;
        case 1: cleaning_facade::AddUrgentJob(manager, id); break;
        case 2: cleaning_facade::ClaimJob(manager, id, random.Next() % 2); break;
        case 3: (i % 2 ? cleaning_facade::ReleaseJob : cleaning_facade::CancelJob)(manager, random.Next() % 3 == 0 ? pickAny() : id); break;
        case 4: cleaning_facade::RemoveJob(manager, id); break;
        case 5: cleaning_facade::GetJobStatus(manager, pickAny()); break;
        case 6: {
            int held = activeId();
            if (held >= 0) cleaning_facade::StartedCleaning(manager, held);
            break;
        }
        case 7: {
            int held = activeId();
            if (held >= 0) cleaning_facade::DidNotClean(manager, held);
            break;
        }
        case 8: cleaning_facade::FindUrgentJob(manager, &type, flags(), flags(), flags()); break;
        case 9:
        case 10: cleaning_facade::FindBestScoredJob(manager, point, &type, flags(), flags(), flags()); break;
        case 11: cleaning_facade::ClaimObject(manager, pickAny()); break;
        case 12:
            if (object::Base* o = ObjectById(pickAny())) static_cast<Prop*>(o)->m_repairing = random.Next() % 3 == 0; // a mechanic's claim
            break;
        default:
            if (object::Base* o = ObjectById(id)) {
                static_cast<Prop*>(o)->m_cleaning = 0; // a cleaner's claim ends
                if (o->m_type == object::ObjectType::Scenery) static_cast<Scenery*>(o)->m_vandalised = random.Next() % 4 == 0;
            }
            break;
        }
    }
}

} // namespace

namespace cleaning_facade {

// ---- life cycle (src/game/cleaning/cleaning_lifecycle.h) ----------------------------------------------------------------------------

namespace {
test::CallCounter g_createCalls("cleaning.manager-create"), g_destroyCalls("cleaning.manager-destroy");
} // namespace

cleaning::Manager* Create()
{
    g_createCalls.Count();
    return new cleaning::Manager();
}

void Destroy(cleaning::Manager* manager)
{
    g_destroyCalls.Count();
    delete manager;
}

} // namespace cleaning_facade

bool RegisterCleaningTestDriver()
{
    g_calls[MethodAddJob].SetDriver(&DriveFuzz);
    return true;
}

} // namespace obl::cleaning
