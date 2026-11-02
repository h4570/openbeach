#include "game/objects/staff_roster.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/staff_call.h"
#include "game/objects/staff_service.h"

#include "game/game/game_accessors.h"
#include "game/objects/staff_values.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "core/log.h"
#include "game/objects/staff_wages.h"
#include "core/call_counter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace obl::object {
using game::GetObjectManager;
using game::GetWorld;

namespace {

using object::Staff;
using tables::StaffDef;

// Call counters (`call-report <name>`).
test::CallCounter g_recalculate("objects.staff-recalculate-happiness-target");
test::CallCounter g_sack("objects.staff-sack");

// ---- RecalculateHappinessTarget ----------------------------------------------------------------------------------------------

// ---- Sack --------------------------------------------------------------------------------------------------------------------

void RunSack(Staff* employee)
{
    g_sack.Count();
    staff::Sack(*employee, *GetWorld());
}

} // namespace

void StaffRecalculateHappinessTarget(object::Staff& member)
{
    g_recalculate.Count();
    staff::RecalculateHappinessTarget(member);
}

object::Staff* FirstStaffMember()
{
    object::Manager* manager = GetObjectManager();
    if (!manager) return nullptr;
    for (int objectClass : {8, 11, 7, 9, 12, 10, 6, 19}) {
        const auto list = object_manager::GetListByClass(manager, objectClass);
        if (!list.empty()) return static_cast<object::Staff*>(list[0]);
    }
    return nullptr;
}

std::vector<object::Staff*> StaffOfEveryClass()
{
    std::vector<object::Staff*> members;
    object::Manager* manager = GetObjectManager();
    if (!manager) return members;
    for (int objectClass : {7, 8, 9, 10, 11, 12, 6, 19}) {
        const auto list = object_manager::GetListByClass(manager, objectClass);
        if (!list.empty()) members.push_back(static_cast<object::Staff*>(list[0]));
    }
    return members;
}

} // namespace obl::object

namespace obl::object {
void Staff::Sack()
{
    object::RunSack(this);
}
} // namespace obl::object
