#include "cleaning_manager.h"

#include "game/cleaning/cleaning_logic.h"
#include "game/cleaning/cleaning_state.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_scenery.h"
#include "core/log.h"

#include <cstdint>
#include <string>
#include <vector>

namespace obl::test {
namespace {

// Objects of the three kinds of cleaning work, buildings (toilets) first, then scenery (bins, litter), in list order.
std::vector<object::Base*> CleaningObjects(const object::Manager& objects, int kind)
{
    std::vector<object::Base*> result;
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i)
        if (cleaning::TypeOfName(objects.m_buildings[i]->m_Name) == kind) result.push_back(objects.m_buildings[i]);
    for (uint32_t i = 0; i < objects.m_others.size(); ++i) {
        object::Base* object = objects.m_others[i];
        if (object && object->m_type == object::ObjectType::Scenery && cleaning::TypeOfName(object->m_Name) == kind)
            result.push_back(object);
    }
    return result;
}

} // namespace

bool ExecuteCleaningCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "cleaning-report" && name != "cleaning-fixture") return false;
    layout::World* world = game::GetWorld();
    object::Manager* objects = game::GetObjectManager();
    if (!world || !objects || !world->m_cleaningManager) {
        OBL_LOG("autotest: FAILED %s: no world", name.c_str());
        return true;
    }
    cleaning::Manager& manager = *world->m_cleaningManager;
    if (name == "cleaning-report") {
        OBL_LOG("autotest: cleaning-report %s cleaners=%d litter=%d bins=%d toilets=%d pending=%d active=%d active_count=%d",
                args.empty() ? "-" : args[0].c_str(),
                object::object_manager::CountByClass(objects, static_cast<char>(object::ObjectType::Cleaner)),
                static_cast<int>(CleaningObjects(*objects, cleaning::kTypeLitter).size()),
                static_cast<int>(CleaningObjects(*objects, cleaning::kTypeBin).size()),
                static_cast<int>(CleaningObjects(*objects, cleaning::kTypeToilet).size()), static_cast<int>(manager.jobs.size()),
                static_cast<int>(manager.activeJobs.size()), manager.activeCount);
        return true;
    }

    // cleaning-fixture: alternate normal and urgent requests, ask twice for some (a count) and promote others (a normal request replaced by an
    // urgent one).
    size_t perKind = args.size() > 0 ? static_cast<size_t>(std::stoi(args[0])) : 6;
    auto addJob = [](cleaning::Manager* manager, int id) { cleaning::cleaning_facade::AddJob(manager, id); };
    auto addUrgent = [](cleaning::Manager* manager, int id) { cleaning::cleaning_facade::AddUrgentJob(manager, id); };
    size_t queued = 0;
    for (int kind : {cleaning::kTypeBin, cleaning::kTypeLitter, cleaning::kTypeToilet}) {
        std::vector<object::Base*> candidates = CleaningObjects(*objects, kind);
        for (size_t i = 0; i < perKind && i < candidates.size(); ++i) {
            int id = static_cast<int>(candidates[i]->m_id);
            if (i % 3 == 0) {
                addJob(&manager, id);
            } else if (i % 3 == 1) {
                addJob(&manager, id);
                addUrgent(&manager, id);
            } else {
                addUrgent(&manager, id);
                addJob(&manager, id);
            }
            ++queued;
        }
    }
    OBL_LOG("autotest: cleaning-fixture queued=%d pending=%d", static_cast<int>(queued), static_cast<int>(manager.jobs.size()));
    return true;
}

} // namespace obl::test
