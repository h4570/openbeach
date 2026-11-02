#include "commands.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"

#include "assertions.h"
#include "game/game/command.h"
#include "game/ui/gui_modes_place.h"
#include "game/game/command_loop.h"
#include "game/ui/gui_modes_tools.h"
#include "game/objects/building_events_state.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_base.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "core/ascii_case.h"
#include "core/log.h"
#include "test_util.h"

#include <cctype>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace obl::test {
namespace {

object::Base* ObjectById(uint32_t id)
{
    auto* manager = game::GetObjectManager();
    if (!manager) return nullptr;
    return object::object_manager::ObjectById(manager, id);
}

struct StaffClass {
    const char* name;
    object::ObjectType type;
};
constexpr StaffClass kStaffClasses[] = {{"Lifeguard", object::ObjectType::Lifeguard}, {"Mechanic", object::ObjectType::Mechanic},
        {"Rep", object::ObjectType::Rep}, {"Builder", object::ObjectType::Builder}, {"Cleaner", object::ObjectType::Cleaner},
        {"SecurityGuard", object::ObjectType::SecurityGuard}};

void FindObject(const std::vector<std::string>& args)
{
    auto* manager = game::GetObjectManager();
    const size_t index = args.size() == 3 ? std::strtoul(args[2].c_str(), nullptr, 10) : 0;
    if (!manager || (args.size() != 2 && args.size() != 3)) {
        OBL_LOG("autotest: FAILED object-find (needs a world and `building|scenery <KEY> [n]`, `staff <Class> [n]` or `guest * [n]`)");
        return;
    }
    object::object_manager::ObjectList list;
    if (args[0] == "staff") {
        const StaffClass* found = nullptr;
        for (const StaffClass& candidate : kStaffClasses)
            if (args[1] == candidate.name) found = &candidate;
        if (!found) {
            OBL_LOG("autotest: FAILED object-find unrecognized staff class %s", args[1].c_str());
            return;
        }
        list = object::object_manager::GetListByClass(manager, static_cast<int>(found->type));
    } else if (args[0] == "guest") {
        list = manager->m_guests;
    } else if (args[0] == "building" || args[0] == "scenery") {
        list = object::object_manager::FindObjectsByName(manager, args[1].c_str());
        // Buildings and scenery may share a name; keep the asked kind.
        const auto wanted = args[0] == "building" ? object::ObjectType::Building : object::ObjectType::Scenery;
        std::erase_if(list, [&](const object::Base* object) { return object->m_type != wanted; });
    } else {
        OBL_LOG("autotest: FAILED object-find kind %s (building, scenery, staff or guest)", args[0].c_str());
        return;
    }
    const long long id = index < list.size() ? static_cast<long long>(list[index]->m_id) : -1;
    OBL_LOG("autotest: object-find id=%lld count=%u", id, static_cast<uint32_t>(list.size()));
}

// The first origin (rows from the top, then columns) where the place mode's fit check passes for the definition and direction.
void FindPlace(const std::vector<std::string>& args)
{
    tables::WorldParams* params = game::GetWorldParams();
    map::Grid* grid = game::GetGrid();
    const int direction = args.size() == 3 ? std::atoi(args[2].c_str()) : 0;
    const auto found = [](uint32_t x, uint32_t y) { OBL_LOG("autotest: place-find found=1 x=%u y=%u", x, y); };
    if (params && grid && args.size() == 1 && args[0] == "dozer") {
        // The first tile the dozer can clear (scenery, a bare path or a surface).
        for (uint32_t y = 0; y < grid->m_height; ++y)
            for (uint32_t x = 0; x < grid->m_width; ++x)
                if (gui::DozerCanRemoveAt(x, y)) return found(x, y);
        OBL_LOG("autotest: place-find found=0 x=-1 y=-1");
        return;
    }
    if (params && grid && args.size() == 2 && args[0] == "surface") {
        // The first tile the surface mode of the definition can paint.
        for (int i = 0; i < params->m_numScenery; ++i) {
            const tables::SceneryDef& definition = params->m_scenery[i];
            if (!ascii::EqualsNoCase(args[1], definition.m_key) || definition.m_Category != 1) continue;
            for (uint32_t y = 0; y < grid->m_height; ++y)
                for (uint32_t x = 0; x < grid->m_width; ++x)
                    if (gui::SurfaceFits(definition, x, y)) return found(x, y);
            OBL_LOG("autotest: place-find found=0 x=-1 y=-1");
            return;
        }
        OBL_LOG("autotest: FAILED place-find surface %s is not a path or grass", args[1].c_str());
        return;
    }
    if (!params || !grid || (args.size() != 2 && args.size() != 3) || (args[0] != "building" && args[0] != "scenery")) {
        OBL_LOG("autotest: FAILED place-find (needs a world and `building|scenery <KEY> [dir]`, `surface <KEY>` or `dozer`)");
        return;
    }
    const tables::BuildingDef* building = nullptr;
    const tables::SceneryDef* scenery = nullptr;
    if (args[0] == "building") {
        for (int i = 0; i < params->m_numBuildings; ++i)
            if (ascii::EqualsNoCase(args[1], params->m_buildings[i].m_key)) building = &params->m_buildings[i];
    } else {
        for (int i = 0; i < params->m_numScenery; ++i)
            if (ascii::EqualsNoCase(args[1], params->m_scenery[i].m_key)) scenery = &params->m_scenery[i];
    }
    if ((!building && !scenery) || (building && !building->m_footprint)) {
        OBL_LOG("autotest: FAILED place-find unrecognized %s %s", args[0].c_str(), args[1].c_str());
        return;
    }
    for (uint32_t y = 0; y < grid->m_height; ++y) {
        for (uint32_t x = 0; x < grid->m_width; ++x) {
            const bool fits = building ? gui::BuildingFits(*building, x, y, direction) : gui::SceneryFits(*scenery, x, y, direction);
            if (!fits) continue;
            return found(x, y);
        }
    }
    OBL_LOG("autotest: place-find found=0 x=-1 y=-1");
}

void ReportObject(const std::vector<std::string>& args)
{
    if (args.size() != 1) {
        OBL_LOG("autotest: FAILED object-report <id>");
        return;
    }
    object::Base* object = ObjectById(static_cast<uint32_t>(std::strtoul(args[0].c_str(), nullptr, 10)));
    if (!object) {
        OBL_LOG("autotest: object-report exists=0");
        return;
    }
    char line[512];
    int used = std::snprintf(line, sizeof(line), "exists=1 id=%u type=%d state=%d waiting=%d pos-x=%.9g pos-y=%.9g", object->m_id,
            static_cast<int>(object->m_type), object->m_state, object->m_waitingForDeath, object->m_WorldXYZ[0], object->m_WorldXYZ[1]);
    const auto type = static_cast<object::ObjectType>(object->m_type);
    if (type == object::ObjectType::Building) {
        auto* building = reinterpret_cast<object::Building*>(object);
        const bool buildJob = object::building_api::HasBuildJob(building);
        const bool demolishJob = object::building_api::HasDemolishJob(building);
        used += std::snprintf(line + used, sizeof(line) - used,
                " open=%u user-close=%d build-job=%d demolish-job=%d repair-interval=%d called-repair=%d called-clean=%d staff=%d "
                "price0=%g "
                "price1=%g price2=%g price3=%g price4=%g beer=%d",
                building->m_OpenStatus, building->m_userClose, buildJob, demolishJob, building->m_UserRepairInterval,
                building->m_urgentRepairCalled, building->m_urgentCleaningCalled, building->m_numFemalestaff + building->m_numMalestaff,
                building->m_Commod_Price[0], building->m_Commod_Price[1], building->m_Commod_Price[2], building->m_Commod_Price[3],
                building->m_Commod_Price[4], building->m_BeerType);
        if (const object::BuildingEvents* events = building->m_events) {
            std::snprintf(line + used, sizeof(line) - used,
                    " events=1 event-price0=%g event-price1=%g event-price2=%g event-id0=%d event-id1=%d event-id2=%d",
                    events->m_slots[0].m_price, events->m_slots[1].m_price, events->m_slots[2].m_price, events->m_slots[0].m_EventID,
                    events->m_slots[1].m_EventID, events->m_slots[2].m_EventID);
        }
    } else if (type >= object::ObjectType::Lifeguard && type <= object::ObjectType::SecurityGuard) {
        const auto* member = reinterpret_cast<const object::Staff*>(object);
        used += std::snprintf(line + used, sizeof(line) - used,
                " wage=%d start=%d end=%d home=%d direct=%d direct-x=%g direct-y=%g direct-object=%d goto=%d", member->m_Wage,
                member->m_StartTime, member->m_EndTime, member->m_HomeID, member->m_DirectStaff, member->m_DirectPos[0],
                member->m_DirectPos[1], member->m_directedToObject, member->m_GotoObject);
        if (type == object::ObjectType::Cleaner) {
            const auto* cleaner = reinterpret_cast<const object::Cleaner*>(object);
            std::snprintf(line + used, sizeof(line) - used, " clean-litter=%d clean-toilets=%d clean-bins=%d", cleaner->m_cleanLitter,
                    cleaner->m_cleanToilets, cleaner->m_cleanBins);
        } else if (type == object::ObjectType::Mechanic) {
            // Only the three job bits: construction keeps whatever the allocation held above them (staff_construction.h).
            std::snprintf(
                    line + used, sizeof(line) - used, " job-mask=%u", reinterpret_cast<const object::Mechanic*>(object)->m_JobMask & 7u);
        } else if (type == object::ObjectType::Rep) {
            std::snprintf(
                    line + used, sizeof(line) - used, " guest-time=%g", reinterpret_cast<const object::Rep*>(object)->m_timeWithGuests);
        } else if (type == object::ObjectType::SecurityGuard) {
            std::snprintf(
                    line + used, sizeof(line) - used, " harshness=%g", reinterpret_cast<const object::SecurityGuard*>(object)->m_tolerance);
        }
    }
    OBL_LOG("autotest: object-report %s", line);
}

// `tile-report x y`: the grid flags of a tile and whether the surface bits of grass and path are all set.
void ReportTile(const std::vector<std::string>& args)
{
    map::Grid* grid = game::GetGrid();
    if (!grid || args.size() != 2) {
        OBL_LOG("autotest: FAILED tile-report (needs a world and `<x> <y>`)");
        return;
    }
    const auto x = static_cast<uint32_t>(std::strtoul(args[0].c_str(), nullptr, 10));
    const auto y = static_cast<uint32_t>(std::strtoul(args[1].c_str(), nullptr, 10));
    if (x >= grid->m_width || y >= grid->m_height) {
        OBL_LOG("autotest: FAILED tile-report %u %u off the map", x, y);
        return;
    }
    const uint32_t flags = grid->Cell(x, y).m_flags;
    const auto all = [&](uint32_t bits) { return (flags & bits) == bits ? 1 : 0; };
    OBL_LOG("autotest: tile-report flags=%u grass=%d path=%d", flags, all(gui_modes_place::kGrassFlags),
            all(gui_modes_place::kPavementFlags));
}

} // namespace

// `$key` -> the value of a probe or report key; false (with the key) when one is missing.
bool SubstituteValues(const std::string& text, std::string& out, std::string& missing)
{
    out.clear();
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '$') {
            out += text[i++];
            continue;
        }
        size_t end = i + 1;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '-' || text[end] == '_')) ++end;
        const std::string key = text.substr(i + 1, end - i - 1);
        double value = 0;
        if (key.empty() || !ReadValue(key, value) || !std::isfinite(value)) {
            missing = key;
            return false;
        }
        // Whole values as integers (ids, tiles); others in the shortest form of the float they round to (positions, prices).
        out += std::trunc(value) == value ? std::to_string(static_cast<long long>(value)) : command::FormatFloat(static_cast<float>(value));
        i = end;
    }
    return true;
}

bool ExecuteCommandLayerCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "command") {
        std::string resolved, error;
        for (const std::string& arg : args) resolved += (resolved.empty() ? "" : " ") + arg;
        auto command = command::Parse(resolved, &error);
        if (!command) {
            OBL_LOG("autotest: FAILED command %s: %s", resolved.c_str(), error.c_str());
            return true;
        }
        OBL_LOG("autotest: command %s", resolved.c_str());
        game::SubmitCommand(std::move(*command));
    } else if (name == "command-report") {
        const game::CommandLoopStats stats = game::GetCommandLoopStats();
        OBL_LOG("autotest: command-report tick=%u applied=%u rejected=%u dropped=%u pending=%u last-applied=%d in-world=%d", stats.tick,
                stats.applied, stats.rejected, stats.dropped, stats.pending, stats.lastApplied, stats.inWorld);
    } else if (name == "object-find") {
        FindObject(args);
    } else if (name == "place-find") {
        FindPlace(args);
    } else if (name == "object-report") {
        ReportObject(args);
    } else if (name == "tile-report") {
        ReportTile(args);
    } else {
        return false;
    }
    return true;
}

} // namespace obl::test
