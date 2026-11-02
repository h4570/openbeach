#include "repair_manager.h"

#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/repair/repair_manager.h"
#include "game/repair/repair_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"
#include "core/log.h"

#include <cstdint>
#include <string>
#include <vector>

namespace obl::test {
namespace {

using RepairManager = repair::Manager;

// Scenery objects live in the "others" list of the ObjectManager.
std::vector<object::Scenery*> SceneryObjects(const object::Manager& objects)
{
    std::vector<object::Scenery*> result;
    for (uint32_t i = 0; i < objects.m_others.size(); ++i) {
        object::Base* object = objects.m_others[i];
        if (object && object->m_type == object::ObjectType::Scenery) result.push_back(static_cast<object::Scenery*>(object));
    }
    return result;
}

} // namespace

bool ExecuteRepairCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "repair-report" && name != "repair-fixture") return false;
    layout::World* world = game::GetWorld();
    object::Manager* objects = game::GetObjectManager();
    if (!world || !objects || !world->m_repairManager) {
        OBL_LOG("autotest: FAILED %s: no world", name.c_str());
        return true;
    }
    RepairManager& manager = *world->m_repairManager;
    if (name == "repair-report") {
        std::vector<object::Scenery*> scenery = SceneryObjects(*objects);
        int waterable = 0;
        for (const object::Scenery* s : scenery) waterable += s->m_definition && s->m_definition->m_water ? 1 : 0;
        size_t active = manager.activeJobs.size();
        OBL_LOG("autotest: repair-report %s mechanics=%d cleaners=%d scenery=%d waterable=%d pending_building=%d pending_scenery=%d "
                "active=%d",
                args.empty() ? "-" : args[0].c_str(),
                object::object_manager::CountByClass(objects, static_cast<char>(object::ObjectType::Mechanic)),
                object::object_manager::CountByClass(objects, static_cast<char>(object::ObjectType::Cleaner)),
                static_cast<int>(scenery.size()), waterable, static_cast<int>(manager.buildingJobs.size()),
                static_cast<int>(manager.sceneryJobs.size()), static_cast<int>(active));
        return true;
    }

    // repair-fixture: a mix that reaches every queue branch - normal and urgent, inspections and breakdowns, repeated requests that
    // promote an inspection, and scenery requests (bins).
    size_t buildings = args.size() > 0 ? static_cast<size_t>(std::stoi(args[0])) : 6;
    size_t sceneryCount = args.size() > 1 ? static_cast<size_t>(std::stoi(args[1])) : 6;
    auto addJob = [](RepairManager* manager, int id, uint32_t priority) { repair::repair_facade::AddJob(manager, id, priority); };
    auto addUrgent = [](RepairManager* manager, int id) { repair::repair_facade::AddUrgentJob(manager, id); };
    size_t queued = 0;
    for (size_t i = 0; i < buildings && i < objects->m_buildings.size(); ++i) {
        int id = static_cast<int>(objects->m_buildings[i]->m_id);
        addJob(&manager, id, i % 3 == 0 ? 0 : 2);
        if (i % 3 == 1) addJob(&manager, id, 2);
        if (i % 4 == 2) addUrgent(&manager, id);
        ++queued;
    }
    std::vector<object::Scenery*> scenery = SceneryObjects(*objects);
    for (size_t i = 0; i < sceneryCount && i < scenery.size(); ++i) {
        int id = static_cast<int>(scenery[i]->m_id);
        if (i % 2 == 0)
            addJob(&manager, id, 1);
        else
            addUrgent(&manager, id);
        ++queued;
    }
    OBL_LOG("autotest: repair-fixture queued=%d pending_building=%d pending_scenery=%d", static_cast<int>(queued),
            static_cast<int>(manager.buildingJobs.size()), static_cast<int>(manager.sceneryJobs.size()));
    return true;
}

} // namespace obl::test
