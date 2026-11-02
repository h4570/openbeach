// Fixtures for the parts of Building::Update that a scenario rarely reaches (groups E and F):
//   building-list [NAME]                 log id, name, state, open status, visits and wear of every building (or of the ones named NAME) and
//                                        report `buildings=<n>`
//   building-visits <n> [NAME]           m_VisitsSinceCleaned = n and m_calledCleaning = 0 for every building (or the ones named NAME): the
//                                        toilet (LOO) then asks for a cleaner and keeps the flies buzzing, the others only count the visits
//   building-age <n> [NAME]             m_BuildingAge = n (the chance of a breakdown roll is FAIL_AGE * age + FAIL_PERCENT, capped at 90 %)
//   building-price <p> [NAME]            m_Commod_Price of all five slots = p for every building (or the ones named NAME): guests that are asked a price
//                                        above their tolerance answer MT_PRICE_NOT_OK, so a high one makes the queue's guests refuse
//   building-demolish <id|NAME>          the message the game sends when a building is demolished: MT_STATECHANGE to STATE_DEMOLISH (2),
//                                        OnEnterState starts the demolition job; the building waits in state 2 until it is empty
//   building-state <id|NAME>             log the state of the building with this id (or the first one named NAME) as `id= state= open= parts=`
//   building-poll-flag [NAME]            every BAYWATCH (or the ones named NAME) with a swimming area gets its flag put on "closed" (animation 1, frame 3)
//                                        and MT_RE_POLL_FLAG_STATE: the building looks at the area again, so an open area switches the flag back
//   building-dismiss-staff <id|NAME> [staffId]   BuildingDismissStaff (the call the dismiss confirmation makes) for the last serving staff member of the
//                                        building (or the given id, which may not be in the list); report `before= after= males= females=`
//   building-hire-staff <id|NAME>        BuildingHireStaff (the call the hire button makes); report `hired= males= females=` of the call
//   fail-arm-all lives in breakdowns.cpp (it arms every building of the world for a roll).
// Probes for `await`/`expect`: building-state-<n> counts the buildings in state n.

#include "core/ascii_case.h"
#include "building_fixture.h"
#include "game/objects/building_methods.h"

#include "game/objects/building_update.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "test_util.h"
#include "game/messages/message_handler.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace obl::test {
namespace {

std::vector<object::Building*> Buildings()
{
    std::vector<object::Building*> list;
    object::Manager* manager = game::GetObjectManager();
    for (uint32_t i = 0; manager && i < manager->m_buildings.size(); ++i)
        if (manager->m_buildings[i]) list.push_back(static_cast<object::Building*>(manager->m_buildings[i]));
    return list;
}

bool Named(const object::Building* building, const std::string& name)
{
    return name.empty() || ascii::EqualsNoCase(name, building->m_Name);
}

// "<id>" (digits) or a building name (the first one in the object manager's order).
object::Building* Find(const std::string& what)
{
    const bool numeric = !what.empty() && what.find_first_not_of("0123456789") == std::string::npos;
    for (object::Building* building : Buildings()) {
        if (numeric ? building->m_id == static_cast<uint32_t>(std::stoul(what)) : what == building->m_Name) return building;
    }
    return nullptr;
}

} // namespace

bool ReadBuildingUpdateProbe(const std::string& name, double& out)
{
    constexpr char kPrefix[] = "building-state-";
    if (name.rfind(kPrefix, 0) != 0 || name.size() != sizeof(kPrefix) || !game::GetObjectManager()) return false;
    const int wanted = name.back() - '0';
    int count = 0;
    for (const object::Building* building : Buildings())
        if (building->m_state == wanted) ++count;
    out = count;
    return true;
}

bool ExecuteBuildingUpdateCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "building-list" && name != "building-visits" && name != "building-age" && name != "building-demolish" &&
            name != "building-state" && name != "building-poll-flag" && name != "building-price" && name != "building-dismiss-staff" &&
            name != "building-hire-staff")
        return false;
    if (!World() || !game::GetObjectManager()) {
        OBL_LOG("autotest: FAILED %s requires a world", name.c_str());
        return true;
    }
    if (name == "building-list") {
        const std::string only = args.empty() ? "" : args[0];
        int count = 0;
        for (const object::Building* building : Buildings()) {
            if (!Named(building, only)) continue;
            ++count;
            const tables::BuildingDef& def = *building->m_definition;
            OBL_LOG("autotest: building id=%u name=%s state=%d open=%u visits=%d wear=%u age=%d failpercent=%d failage=%d avfail=%g",
                    building->m_id, building->m_Name, building->m_state, building->m_OpenStatus, building->m_VisitsSinceCleaned,
                    building->m_RepairState, building->m_BuildingAge, def.m_FailPercent, def.m_FailAge, def.m_AvFailTime);
        }
        OBL_LOG("autotest: building-list buildings=%d", count);
    } else if (name == "building-visits" && !args.empty()) {
        const int visits = std::stoi(args[0]);
        const std::string only = args.size() > 1 ? args[1] : "";
        int changed = 0;
        for (object::Building* building : Buildings()) {
            if (!Named(building, only)) continue;
            object::SetBuildingVisitsSinceCleaned(*building, visits);
            ++changed;
        }
        OBL_LOG("autotest: building-visits visits=%d changed=%d", visits, changed);
    } else if (name == "building-age" && !args.empty()) {
        const int age = std::stoi(args[0]);
        const std::string only = args.size() > 1 ? args[1] : "";
        int changed = 0;
        for (object::Building* building : Buildings()) {
            if (!Named(building, only)) continue;
            object::SetBuildingAge(*building, age);
            ++changed;
        }
        OBL_LOG("autotest: building-age age=%d changed=%d", age, changed);
    } else if (name == "building-price" && !args.empty()) {
        const float price = std::stof(args[0]);
        const std::string only = args.size() > 1 ? args[1] : "";
        int changed = 0;
        for (object::Building* building : Buildings()) {
            if (!Named(building, only)) continue;
            object::SetBuildingCommodityPrices(*building, price);
            ++changed;
        }
        OBL_LOG("autotest: building-price price=%g changed=%d", price, changed);
    } else if (name == "building-demolish" && args.size() == 1) {
        object::Building* building = Find(args[0]);
        if (!building) {
            OBL_LOG("autotest: FAILED building-demolish: no building %s", args[0].c_str());
            return true;
        }
        messages::Message message{};
        message.sender = static_cast<int>(building->m_id);
        message.receiver = static_cast<int>(building->m_id);
        message.type = messages::MT_STATECHANGE;
        message.data = kBuildingStateDemolish;
        messages::message_facade::Post(World()->m_messageHandler, &message);
        OBL_LOG("autotest: building-demolish id=%u name=%s", building->m_id, building->m_Name);
    } else if (name == "building-dismiss-staff" && !args.empty()) {
        object::Building* building = Find(args[0]);
        if (!building) {
            OBL_LOG("autotest: FAILED building-dismiss-staff: no building %s", args[0].c_str());
            return true;
        }
        const int before = building->m_numMalestaff + building->m_numFemalestaff;
        const uint32_t id =
                args.size() > 1 ? static_cast<uint32_t>(std::stoul(args[1])) : (before > 0 ? building->m_staff[before - 1] : 0u);
        object::BuildingDismissStaff(building, id);
        OBL_LOG("autotest: building-dismiss-staff id=%u staff=%u before=%d after=%d males=%d females=%d", building->m_id, id, before,
                building->m_numMalestaff + building->m_numFemalestaff, building->m_numMalestaff, building->m_numFemalestaff);
    } else if (name == "building-hire-staff" && !args.empty()) {
        object::Building* building = Find(args[0]);
        if (!building) {
            OBL_LOG("autotest: FAILED building-hire-staff: no building %s", args[0].c_str());
            return true;
        }
        const bool hired = object::BuildingHireStaff(building);
        OBL_LOG("autotest: building-hire-staff id=%u hired=%d males=%d females=%d", building->m_id, hired ? 1 : 0, building->m_numMalestaff,
                building->m_numFemalestaff);
    } else if (name == "building-poll-flag") {
        const std::string only = args.empty() ? "BAYWATCH" : args[0];
        int polled = 0;
        for (object::Building* building : Buildings()) {
            if (Named(building, only) && object::PollBuildingClosedFlag(*building)) ++polled;
        }
        OBL_LOG("autotest: building-poll-flag polled=%d", polled);
    } else if (name == "building-state" && args.size() == 1) {
        const object::Building* building = Find(args[0]);
        if (!building) {
            OBL_LOG("autotest: building-state id=-1 state=-1 open=-1 parts=-1");
            return true;
        }
        OBL_LOG("autotest: building-state id=%u state=%d open=%u parts=%d", building->m_id, building->m_state, building->m_OpenStatus,
                building->m_NumVehicles + static_cast<int>(building->m_numTables) + static_cast<int>(building->m_numLoungers));
    } else {
        OBL_LOG("autotest: FAILED %s: wrong arguments", name.c_str());
    }
    return true;
}

} // namespace obl::test
