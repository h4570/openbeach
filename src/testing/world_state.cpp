#include "game/objects/scenery_names.h"
#include "core/char16.h"
#include "game/world/world_update.h"
#include "game/objects/staff_direct.h"
#include "game/game/game_api.h"
#include "game/world/world_objects.h"
#include "game/world/world_create.h"
#include "world_state.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"

#include "game/map/grid_state.h"
#include "building_fixture.h"
#include "game/ui/map_view.h"
#include "game/objects/building_events_state.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_scenery.h"
#include "core/ascii_case.h"
#include "core/log.h"
#include "test_util.h"
#include "game/construction/construction_manager.h"
#include "game/messages/message_handler.h"
#include "game/ui/music_player.h"
#include "game/ui/sound_types.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "game/objects/object_create_params.h"
#include "game/ui/map_view_access.h"

namespace obl::test {
namespace {
int g_lastBuildingId = -1;
int g_lastBuilderId = -1;

bool Integer(const std::string& text, int& out)
{
    auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool Real(const std::string& text, float& out)
{
    char* end;
    out = std::strtof(text.c_str(), &end);
    return end != text.c_str() && *end == 0 && std::isfinite(out);
}

struct Profession {
    const char* name;
    object::ObjectType type;
};
const Profession kProfessions[] = {{"Lifeguard", object::ObjectType::Lifeguard}, {"Mechanic", object::ObjectType::Mechanic},
        {"Cleaner", object::ObjectType::Cleaner}, {"Rep", object::ObjectType::Rep}, {"Builder", object::ObjectType::Builder},
        {"SecurityGuard", object::ObjectType::SecurityGuard}, {"ServiceStaff", object::ObjectType::ServiceStaff}};

const Profession* FindProfession(const std::string& name, bool ignoreCase = false)
{
    for (const Profession& candidate : kProfessions) {
        if (name.size() != std::strlen(candidate.name)) continue;
        bool same = true;
        for (size_t i = 0; i < name.size(); ++i)
            same = same && (ignoreCase ? ascii::Lower(name[i]) == ascii::Lower(candidate.name[i]) : name[i] == candidate.name[i]);
        if (same) return &candidate;
    }
    return nullptr;
}

// The members of a profession; the list is kept in a static until the next call (the autotest commands run one at a time).
object::Base** Members(const Profession& profession, uint32_t& count)
{
    static object::object_manager::ObjectList members;
    count = 0;
    members.clear();
    auto manager = game::GetObjectManager();
    if (!manager) return nullptr;
    members = object::object_manager::GetListByClass(manager, static_cast<int>(profession.type));
    count = static_cast<uint32_t>(members.size());
    return members.data();
}
} // namespace

bool ReadWorldStateProbe(const std::string& name, double& out)
{
    // staff-<profession>-<state>: how many members of the profession are in that state (await it, then act on the member: `staff-direct`).
    constexpr char kPrefix[] = "staff-";
    const size_t dash = name.rfind('-');
    if (name.rfind(kPrefix, 0) != 0 || dash == std::string::npos || dash <= sizeof(kPrefix) - 1) return false;
    const Profession* profession = FindProfession(name.substr(sizeof(kPrefix) - 1, dash - (sizeof(kPrefix) - 1)), true);
    int state = 0;
    if (!profession || !Integer(name.substr(dash + 1), state) || !game::GetObjectManager()) return false;
    uint32_t count = 0;
    object::Base** members = Members(*profession, count);
    int inState = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (static_cast<int>(members[i]->m_state) == state) ++inState;
    out = inState;
    return true;
}

void ResetWorldStateCommands()
{
    g_lastBuildingId = g_lastBuilderId = -1;
}

object::Building* SpawnBuilding(const std::string& name, int tx, int ty, int age)
{
    auto world = World();
    tables::WorldParams* params = game::GetWorldParams();
    map::Grid* grid = game::GetGrid();
    auto manager = game::GetObjectManager();
    if (!world || !params || !grid || !manager || name.empty() || name.size() >= sizeof(object::BuildingCreateParams::m_key) || tx < 0 ||
            ty < 0 || tx >= static_cast<int>(grid->m_width) || ty >= static_cast<int>(grid->m_height) ||
            manager->m_buildings.size() >= object::Manager::kMaxBuildings) {
        OBL_LOG("autotest: FAILED spawn-building (world, name, coordinates or capacity)");
        return nullptr;
    }
    // GetBuildingId calls FatalError for unknown keys. Validate against definitions, not the scenario build tree.
    bool found = false;
    for (int i = 0; i < params->m_numBuildings; ++i)
        if (ascii::EqualsNoCase(name, params->m_buildings[i].m_key)) found = true;
    if (!found) {
        OBL_LOG("autotest: FAILED spawn-building no such name %s", name.c_str());
        return nullptr;
    }
    object::BuildingCreateParams record{};
    std::memcpy(record.m_key, name.c_str(), name.size() + 1);
    record.m_tileX = tx;
    record.m_tileY = ty;
    for (float& price : record.m_prices) price = -1.0f;
    record.m_beerType = -1;
    record.m_age = age;
    auto building = static_cast<object::Building*>(world::CreateObject(*world, object::ObjectType::Building, &record));
    if (building) g_lastBuildingId = building->m_id;
    return building;
}

bool ExecuteWorldStateCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "spawn-building" && name != "remove-building" && name != "set-cash" && name != "set-hour" && name != "spawn-guests" &&
            name != "zoom" && name != "camera" && name != "spawn-builder" && name != "spawn-staff" && name != "spawn-performer" &&
            name != "remove-staff" && name != "remove-builder" && name != "world-darken" && name != "queue-blocks" &&
            name != "staff-list" && name != "staff-direct" && name != "staff-direct-object" && name != "staff-demolish-target" &&
            name != "staff-sack" && name != "staff-setvar" && name != "building-setvar" && name != "staff-gettext" &&
            name != "object-select" && name != "force-pause" && name != "lounger-stale-seat")
        return false;
    auto world = World();
    if (!world || GameStateIndex() != 0 || Game()->m_guiReloadPending) {
        OBL_LOG("autotest: FAILED %s requires settled gameplay (await gameplay)", name.c_str());
        return true;
    }
    int x = 0, y = 0;
    float value;
    if (name == "spawn-building" && (args.size() == 3 || (args.size() == 4 && args[3] == "user")) && Integer(args[1], x) &&
            Integer(args[2], y)) {
        if (auto building = SpawnBuilding(args[0], x, y)) {
            // `user`: as if the player had built it (BarsBuilt and DiffConstructCount count only such buildings).
            if (args.size() == 4) building->m_builtByUser = 1;
            OBL_LOG("autotest: spawn-building %s id=%d", args[0].c_str(), building->m_id);
        }
    } else if (name == "remove-building" && args.size() <= 1 && (args.empty() || Integer(args[0], g_lastBuildingId))) {
        object::Manager* manager = world->m_objectManager;
        object::Base* building = nullptr;
        for (uint32_t i = 0; i < manager->m_buildings.size(); ++i) {
            if (static_cast<int>(manager->m_buildings[i]->m_id) == g_lastBuildingId) building = manager->m_buildings[i];
        }
        if (!building) {
            OBL_LOG("autotest: FAILED remove-building id=%d not found", g_lastBuildingId);
            return true;
        }
        int id = g_lastBuildingId;
        // Immediate fixture removal bypasses FinishBuild/FinishDemolish. Drop its jobs while the building still exists:
        // otherwise the next save contains a job whose work points cannot be reconstructed on load.
        auto* construction = world->m_constructionManager.get();
        int jobsDeleted = 0;
        while (construction::runtime::FindJob(construction, id)) {
            construction::runtime::DeleteJob(construction, id);
            ++jobsDeleted;
        }
        if (jobsDeleted) construction::runtime::ReallocateBuilders(construction);
        building->m_waitingForDeath = 1; // The deletion flag that setting value 0x4a raises.
        world::ProcessDeletedObjects(*world);
        world::UpdateBuildingTotals(*world);
        g_lastBuildingId = -1;
        OBL_LOG("autotest: remove-building id=%d constructionJobsDeleted=%d", id, jobsDeleted);
    } else if (name == "spawn-builder" && (args.empty() || args.size() == 2)) {
        // the builder (world::CreateObject). The record is BuilderCreateParams: key, world position, the
        // slot-0x68 value (0 as the world script's records) and the BuilderNumber, here the next free one. The builder starts in
        // state 0 and registers with the ConstructionManager when its script enters state 1 (Builder::OnEnterState): `step` first.
        tables::WorldParams* params = game::GetWorldParams();
        const char* key = nullptr;
        for (int i = 0; i < params->m_numStaff; ++i) {
            std::string candidate = params->m_staff[i].m_key;
            for (char& c : candidate) c = ascii::Lower(c);
            if (candidate.find("builder") != std::string::npos) key = params->m_staff[i].m_key.c_str();
        }
        if (!key) {
            OBL_LOG("autotest: FAILED spawn-builder no builder staff definition");
            return true;
        }
        object::BuilderCreateParams record{};
        std::strncpy(record.m_key, key, sizeof(record.m_key) - 1);
        if (args.empty()) {
            obl::world::World::RandomWalkPoint(record.m_pos);
        } else if (Real(args[0], record.m_pos[0]) && Real(args[1], record.m_pos[1])) {
            record.m_pos[2] = 0;
        } else {
            OBL_LOG("autotest: FAILED spawn-builder invalid arguments");
            return true;
        }
        for (object::Base* member :
                object::object_manager::GetListByClass(world->m_objectManager, static_cast<int>(object::ObjectType::Builder)))
            record.m_builderNumber = std::max(record.m_builderNumber, static_cast<object::Builder*>(member)->m_BuilderNumber + 1);
        auto* builder = static_cast<object::Base*>(world::CreateObject(*world, object::ObjectType::Builder, &record));
        if (builder) g_lastBuilderId = static_cast<int>(builder->m_id);
        OBL_LOG("autotest: spawn-builder id=%d number=%d", builder ? static_cast<int>(builder->m_id) : -1, record.m_builderNumber);
    } else if (name == "remove-builder" && args.size() <= 1 && (args.empty() || Integer(args[0], g_lastBuilderId))) {
        // The same deletion flag as remove-building; the sweep runs the Builder destructor, which unregisters the builder.
        object::Base* builder = nullptr;
        for (object::Base* member :
                object::object_manager::GetListByClass(world->m_objectManager, static_cast<int>(object::ObjectType::Builder)))
            if (static_cast<int>(member->m_id) == g_lastBuilderId || (args.empty() && g_lastBuilderId < 0)) builder = member;
        if (!builder) {
            OBL_LOG("autotest: FAILED remove-builder id=%d not found", g_lastBuilderId);
            return true;
        }
        const int id = static_cast<int>(builder->m_id);
        builder->m_waitingForDeath = 1;
        world::ProcessDeletedObjects(*world);
        g_lastBuilderId = -1;
        OBL_LOG("autotest: remove-builder id=%d", id);
    } else if (name == "remove-staff" && (args.size() == 1 || args.size() == 2) && (args.size() == 1 || Integer(args[1], x))) {
        // The same deletion flag and sweep for one member of a profession (the last one of its list, or the given id): its destructor takes it down.
        const Profession* profession = FindProfession(args[0]);
        uint32_t count = 0;
        object::Base** members = profession ? Members(*profession, count) : nullptr;
        object::Base* member = nullptr;
        for (uint32_t i = 0; i < count; ++i)
            if (args.size() == 1 || static_cast<int>(members[i]->m_id) == x) member = members[i];
        if (!member) {
            OBL_LOG("autotest: FAILED remove-staff %s: no such member", args[0].c_str());
            return true;
        }
        const int id = static_cast<int>(member->m_id);
        member->m_waitingForDeath = 1;
        world::ProcessDeletedObjects(*world);
        OBL_LOG("autotest: remove-staff %s id=%d", args[0].c_str(), id);
    } else if (name == "staff-sack" && (args.size() == 1 || args.size() == 2) && (args.size() == 1 || Integer(args[1], x))) {
        // The player sacking a member from the staff menu: the virtual Sack of the last member of the profession, or the given id, so the
        // <Profession>Object sack handler runs on a live member; the member leaves its job on its own afterwards (docs/specs/staff-duties.md).
        const Profession* profession = FindProfession(args[0]);
        uint32_t count = 0;
        object::Base** members = profession ? Members(*profession, count) : nullptr;
        object::Base* member = nullptr;
        for (uint32_t i = 0; i < count; ++i)
            if (args.size() == 1 || static_cast<int>(members[i]->m_id) == x) member = members[i];
        if (!member) {
            OBL_LOG("autotest: FAILED staff-sack %s: no such member", args[0].c_str());
            return true;
        }
        constexpr size_t kSackSlot = 28;
        static_cast<object::Staff*>(member)->Sack();
        OBL_LOG("autotest: staff-sack %s id=%u state=%d", args[0].c_str(), member->m_id, member->m_state);
    } else if (name == "staff-setvar" && (args.size() == 3 || args.size() == 4) && (args.size() == 3 || Integer(args[3], x))) {
        // The staff menu setting a keyword: the virtual SetVariable of the last member of the profession, or the given id. The value is one 4-byte
        // word: a decimal integer, or a float when it has a '.'.
        const Profession* profession = FindProfession(args[0]);
        uint32_t count = 0;
        object::Base** members = profession ? Members(*profession, count) : nullptr;
        object::Base* member = nullptr;
        for (uint32_t i = 0; i < count; ++i)
            if (args.size() == 3 || static_cast<int>(members[i]->m_id) == x) member = members[i];
        uint32_t word = 0;
        float real = 0;
        if (args[2].find('.') != std::string::npos && Real(args[2], real))
            std::memcpy(&word, &real, sizeof(word));
        else {
            int integer = 0;
            if (Integer(args[2], integer))
                word = static_cast<uint32_t>(integer);
            else
                member = nullptr;
        }
        if (!member) {
            OBL_LOG("autotest: FAILED staff-setvar %s: no such member or bad value", args[0].c_str());
            return true;
        }
        constexpr size_t kSetVariableSlot = 7;
        member->SetVariable(args[1].c_str(), &word);
        OBL_LOG("autotest: staff-setvar %s id=%u %s=0x%08x", args[0].c_str(), member->m_id, args[1].c_str(), word);
    } else if (name == "building-setvar" && (args.size() == 2 || args.size() == 3)) {
        // The building dialogs setting a keyword: the virtual SetVariable of the building with the given id (default: the last spawned). The value is one
        // 4-byte word: a decimal integer, or a float when it has a '.'. The report reads back what the keywords write (the show prices of the first slot too).
        // The target: an id, or the first building that has a queue (`queue`), a show schedule (`events`) or serves beer (`beer`; also with a queue): a spawned
        // building is a construction site without them.
        const std::string target = args.size() == 3 ? args[2] : std::string();
        int id = g_lastBuildingId;
        if (!target.empty() && Integer(target, x)) id = x;
        object::Manager* manager = world->m_objectManager;
        object::Building* building = nullptr;
        for (uint32_t i = 0; i < manager->m_buildings.size(); ++i) {
            auto* candidate = static_cast<object::Building*>(manager->m_buildings[i]);
            const bool match = target == "queue" ? candidate->m_queue != nullptr
                    : target == "events"         ? candidate->m_events != nullptr
                    : target == "beer"           ? candidate->m_queue != nullptr && candidate->m_BeerType != -1
                                                 : static_cast<int>(candidate->m_id) == id;
            if (match && !building) building = candidate;
        }
        uint32_t word = 0;
        float real = 0;
        if (args[1].find('.') != std::string::npos && Real(args[1], real))
            std::memcpy(&word, &real, sizeof(word));
        else {
            int integer = 0;
            if (Integer(args[1], integer))
                word = static_cast<uint32_t>(integer);
            else
                building = nullptr;
        }
        if (!building) {
            OBL_LOG("autotest: FAILED building-setvar %s: no such building or bad value", args[0].c_str());
            return true;
        }
        constexpr size_t kSetVariableSlot = 7;
        building->SetVariable(args[0].c_str(), &word);
        const float eventPrice = building->m_events ? building->m_events->m_slots[0].m_price : -1.0f;
        OBL_LOG("autotest: building-setvar id=%u user_repair=%d repair=%d beer_strength=%g price0=%g price1=%g cost0=%g cost1=%g "
                "open_status=%u user_close=%u event0_price=%g state=%d has_queue=%d has_events=%d",
                building->m_id, building->m_UserRepairInterval, building->m_RepairInterval, building->m_BeerStrength,
                building->m_Commod_Price[0], building->m_Commod_Price[1], building->m_Commod_Cost[0], building->m_Commod_Cost[1],
                building->m_OpenStatus, building->m_userClose, eventPrice, building->m_state, building->m_queue ? 1 : 0,
                building->m_events ? 1 : 0);
    } else if (name == "staff-gettext" && (args.size() == 2 || args.size() == 3) && (args.size() == 2 || Integer(args[2], x))) {
        // The staff menu asking for the text of a keyword: the virtual GetVariableText of the last member of the profession, or the given id.
        // The text is written to the log (narrowed), so a script can see what the menu would show.
        const Profession* profession = FindProfession(args[0]);
        uint32_t count = 0;
        object::Base** members = profession ? Members(*profession, count) : nullptr;
        object::Base* member = nullptr;
        for (uint32_t i = 0; i < count; ++i)
            if (args.size() == 2 || static_cast<int>(members[i]->m_id) == x) member = members[i];
        if (!member) {
            OBL_LOG("autotest: FAILED staff-gettext %s: no such member", args[0].c_str());
            return true;
        }
        constexpr size_t kGetVariableTextSlot = 6;
        Char16 text[512] = {};
        member->GetVariableText(args[1].c_str(), text);
        std::string narrow;
        for (const Char16* c = text; *c && c < text + 511; ++c) narrow += *c < 0x80 ? static_cast<char>(*c) : '?';
        OBL_LOG("autotest: staff-gettext %s id=%u %s=\"%s\"", args[0].c_str(), member->m_id, args[1].c_str(), narrow.c_str());
    } else if (name == "spawn-staff" && (args.size() == 1 || args.size() == 3)) {
        // The six template factories (CreateServiceStaff, Lifeguard, Mechanic, Cleaner,
        // SecurityGuard, Rep): world::CreateObject, the record is StaffCreateParams. One staff definition per class
        // (the factories do not check that the key belongs to the class); the home id is 0 as in the world script's records. The staff member
        // stays in the world (spawn-staff <Class> [x y]); the command checks the class byte and the home it was given.
        struct Factory {
            const char* name;
            const char* key;
            object::ObjectType type;
        };
        const Factory factories[] = {
                {"ServiceStaff", "BAR_M", object::ObjectType::ServiceStaff},
                {"Lifeguard", "LIFEG_M", object::ObjectType::Lifeguard},
                {"Mechanic", "MECHANIC", object::ObjectType::Mechanic},
                {"Cleaner", "CLEANER", object::ObjectType::Cleaner},
                {"SecurityGuard", "SECURITYGUARD", object::ObjectType::SecurityGuard},
                {"Rep", "REP_M", object::ObjectType::Rep},
        };
        const Factory* factory = nullptr;
        for (const Factory& candidate : factories)
            if (args[0] == candidate.name) factory = &candidate;
        tables::WorldParams* params = game::GetWorldParams();
        bool defined = false;
        if (factory)
            for (int i = 0; i < params->m_numStaff; ++i)
                if (ascii::EqualsNoCase(params->m_staff[i].m_key, factory->key)) defined = true;
        if (!factory || !defined) { // world::world_params::GetStaffId calls FatalError for an unknown key
            OBL_LOG("autotest: FAILED spawn-staff no such class %s or staff definition", args[0].c_str());
            return true;
        }
        object::StaffCreateParams record{};
        std::strncpy(record.m_key, factory->key, sizeof(record.m_key) - 1);
        if (args.size() == 1) {
            obl::world::World::RandomWalkPoint(record.m_pos);
        } else if (!(Real(args[1], record.m_pos[0]) && Real(args[2], record.m_pos[1]))) {
            OBL_LOG("autotest: FAILED spawn-staff invalid arguments");
            return true;
        }
        auto* staff = static_cast<object::Staff*>(world::CreateObject(*world, factory->type, &record));
        if (!staff || staff->m_type != factory->type || staff->m_HomeID != record.m_homeId) {
            OBL_LOG("autotest: FAILED spawn-staff %s: object %p type %d home %d", factory->name, static_cast<void*>(staff),
                    staff ? static_cast<int>(staff->m_type) : -1, staff ? staff->m_HomeID : -1);
            return true;
        }
        OBL_LOG("autotest: spawn-staff %s id=%u type=%d state=%d", factory->name, staff->m_id, static_cast<int>(staff->m_type),
                staff->m_state);
    } else if (name == "spawn-performer" && (args.size() == 1 || args.size() == 2)) {
        // The six performer factories (CreateWeightLifter, Singer, Aerobic, Cabaret, Catwalk,
        // WetTShirt): world::CreateObject, the record is PerformerCreateParams. The performer joins the building made last
        // (spawn-building / event-build: a building with an event schedule) and stays in the world (spawn-performer <Class> [variant]).
        struct Factory {
            const char* name;
            object::ObjectType type;
        };
        const Factory factories[] = {
                {"WeightLifter", object::ObjectType::WeightLifter},
                {"Singer", object::ObjectType::Singer},
                {"Aerobic", object::ObjectType::Aerobic},
                {"Cabaret", object::ObjectType::Cabaret},
                {"Catwalk", object::ObjectType::Catwalk},
                {"WetTShirt", object::ObjectType::WetTShirt},
        };
        const Factory* factory = nullptr;
        for (const Factory& candidate : factories)
            if (args[0] == candidate.name) factory = &candidate;
        object::PerformerCreateParams record{};
        object::Building* building = nullptr;
        for (uint32_t i = 0; i < world->m_objectManager->m_buildings.size(); ++i)
            if (static_cast<int>(world->m_objectManager->m_buildings[i]->m_id) == g_lastBuildingId)
                building = static_cast<object::Building*>(world->m_objectManager->m_buildings[i]);
        if (!factory || (args.size() == 2 && !Integer(args[1], record.m_variant)) || !building || !building->m_events) {
            OBL_LOG("autotest: FAILED spawn-performer no such class, bad variant or no building with events (id %d)", g_lastBuildingId);
            return true;
        }
        record.m_buildingId = g_lastBuildingId;
        obl::world::World::RandomWalkPoint(record.m_pos);
        auto* performer = static_cast<object::Base*>(world::CreateObject(*world, factory->type, &record));
        if (!performer || performer->m_type != factory->type) {
            OBL_LOG("autotest: FAILED spawn-performer %s: object %p type %d", factory->name, static_cast<void*>(performer),
                    performer ? static_cast<int>(performer->m_type) : -1);
            return true;
        }
        OBL_LOG("autotest: spawn-performer %s id=%u type=%d variant=%d building=%d", factory->name, performer->m_id,
                static_cast<int>(performer->m_type), record.m_variant, record.m_buildingId);
    } else if (name == "staff-list" && (args.size() == 1 || args.size() == 2) && (args.size() == 1 || Integer(args[1], x))) {
        // What the members of one profession are doing: each line is id, home and state; the summary counts the members whose home is the
        // given building id and the ones in state 0x4c (the state a member takes when its home goes). Natural coverage of the removal
        // branches of the profession's message handler is asserted from these counts (docs/specs/staff-duties.md).
        const Profession* profession = FindProfession(args[0]);
        if (!profession) {
            OBL_LOG("autotest: FAILED staff-list no such class %s", args[0].c_str());
            return true;
        }
        const int homeId = args.size() == 2 ? x : -1;
        uint32_t count = 0;
        object::Base** members = Members(*profession, count);
        int homed = 0, leaving = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto* member = static_cast<const object::Staff*>(members[i]);
            OBL_LOG("autotest: staff-member class=%s id=%u home=%d state=%u", profession->name, member->m_id, member->m_HomeID,
                    member->m_state);
            if (homeId >= 0 && member->m_HomeID == homeId) ++homed;
            if (member->m_state == 0x4c) ++leaving;
        }
        OBL_LOG("autotest: staff-list class=%s count=%u homed=%d leaving=%d", profession->name, count, homed, leaving);
    } else if (name == "staff-direct" && (args.size() == 2 || args.size() == 4) && Integer(args[1], x)) {
        // The player sending the selected staff to a tile (the send-staff GUI mode, left click on a free tile) calls StaffDirectToPoint on
        // each member, which posts MT_DIRECT_TO_POINT to the member itself. Here the members of one profession that are in the given state now
        // get the same call, so the message goes through the normal sender and delivery to a member in the state the receive
        // branch wants (docs/specs/staff-duties.md). The point is the one given or a random walkable one.
        const Profession* profession = FindProfession(args[0]);
        if (!profession) {
            OBL_LOG("autotest: FAILED staff-direct no such class %s", args[0].c_str());
            return true;
        }
        float point[3]{};
        const bool given = args.size() == 4;
        if (given && !(Real(args[2], point[0]) && Real(args[3], point[1]))) {
            OBL_LOG("autotest: FAILED staff-direct invalid arguments");
            return true;
        }
        uint32_t count = 0;
        object::Base** members = Members(*profession, count);
        std::vector<object::Staff*> chosen;
        for (uint32_t i = 0; i < count; ++i)
            if (static_cast<int>(members[i]->m_state) == x) chosen.push_back(static_cast<object::Staff*>(members[i]));
        for (object::Staff* member : chosen) {
            if (!given) {
                obl::world::World::RandomWalkPoint(point);
            }
            object::StaffDirectToPoint(*member, point);
        }
        OBL_LOG("autotest: staff-direct class=%s state=%d directed=%d", profession->name, x, static_cast<int>(chosen.size()));
    } else if (name == "object-select" && args.size() == 1) {
        // The player clicking an object: its OnSelect(clicked=1, centerCamera=0), which opens the object's info dialog with the object as the
        // provider (the first Cleaner / guest / building of the world; the golden GUI frames need a real provider, docs/testing/golden-images.md).
        object::Base* target = nullptr;
        bool cleaner = false;
        bool guest = false;
        if (args[0] == "Cleaner") {
            uint32_t count = 0;
            object::Base** members = Members(*FindProfession("Cleaner"), count);
            if (count > 0) target = members[0];
            cleaner = true;
        } else if (args[0] == "Guest" || args[0] == "Building") {
            guest = args[0] == "Guest";
            const auto list = object::object_manager::GetListByClass(
                    game::GetObjectManager(), static_cast<int>(guest ? object::ObjectType::Guest : object::ObjectType::Building));
            if (!list.empty()) target = list[0];
            if (!guest &&
                    g_lastBuildingId >=
                            0) // a spawn-building before it: the first building of the scenario is a pier piece without a window
                target = object::object_manager::ObjectById(game::GetObjectManager(), g_lastBuildingId);
        }
        if (!target) {
            OBL_LOG("autotest: FAILED object-select %s: no such class or no object", args[0].c_str());
            return true;
        }
        if (cleaner)
            static_cast<object::Cleaner*>(target)->object::Cleaner::OnSelect(1, 0);
        else if (guest)
            static_cast<object::Guest*>(target)->object::Guest::OnSelect(1, 0);
        else
            static_cast<object::Building*>(target)->object::Building::OnSelect(1, 0);
        OBL_LOG("autotest: object-select %s id=%u", args[0].c_str(), target->m_id);
    } else if (name == "staff-direct-object" && args.size() == 2) {
        // The player pointing the first member of the profession at an object (the profession's direct-to-object handler); the target is an
        // object id, the first guest / building / scenery of the world, the building spawned last (`last`), or `none` (an id no object has). Reports `target=`.
        const Profession* profession = FindProfession(args[0]);
        if (!profession) {
            OBL_LOG("autotest: FAILED staff-direct-object no such class %s", args[0].c_str());
            return true;
        }
        uint32_t count = 0;
        object::Base** members = Members(*profession, count);
        int target = -1;
        object::ObjectType targetType = object::ObjectType::Guest;
        const bool byType = args[1] == "guest" || args[1] == "building" || args[1] == "scenery";
        if (args[1] == "building") targetType = object::ObjectType::Building;
        if (args[1] == "scenery") targetType = object::ObjectType::Scenery;
        if (args[1] == "none")
            target = 0x7ffffff0;
        else if (args[1] == "last")
            target = g_lastBuildingId; // the building `spawn-building` made last
        else if (byType) {
            const auto list = object::object_manager::GetListByClass(game::GetObjectManager(), static_cast<int>(targetType));
            if (!list.empty()) target = static_cast<int>(list[0]->m_id);
        } else if (!Integer(args[1], target)) {
            OBL_LOG("autotest: FAILED staff-direct-object invalid target %s", args[1].c_str());
            return true;
        }
        if (count == 0 || target < 0) {
            OBL_LOG("autotest: FAILED staff-direct-object no member of %s or no target %s", args[0].c_str(), args[1].c_str());
            return true;
        }
        object::Base* targetObject = object::object_manager::ObjectById(game::GetObjectManager(), target);
        OBL_LOG("autotest: staff-direct-object class=%s target=%d object=%d state=%d", profession->name, target, targetObject ? 1 : 0,
                targetObject ? targetObject->m_state : -1);
        auto* member = static_cast<object::Staff*>(members[0]);
        static_cast<object::Staff*>(member)->DirectToObject(static_cast<int32_t>(target));
        OBL_LOG("autotest: staff-direct-object class=%s cantdojob=%d", profession->name, static_cast<int>(member->m_cantDoJob));
    } else if (name == "staff-demolish-target" && args.size() == 2 && Integer(args[1], x)) {
        // The first member of the profession in the given state loses its target: when the target is a building, it is demolished the way
        // `building-demolish` does (MT_BUILDING_BEING_REMOVED to every worker, the member's target is not its home). Reports `target=` and
        // `demolished=` (0 when no member is in the state or the target is not a building).
        const Profession* profession = FindProfession(args[0]);
        if (!profession) {
            OBL_LOG("autotest: FAILED staff-demolish-target no such class %s", args[0].c_str());
            return true;
        }
        uint32_t count = 0;
        object::Base** members = Members(*profession, count);
        int target = -1, demolished = 0;
        for (uint32_t i = 0; i < count && target < 0; ++i) {
            const auto* member = static_cast<const object::Staff*>(members[i]);
            if (static_cast<int>(member->m_state) == x) target = static_cast<int>(member->m_DestObjectID);
        }
        object::Base* object = target >= 0 ? object::object_manager::ObjectById(world->m_objectManager, target) : nullptr;
        if (object && object->m_type == object::ObjectType::Building) {
            demolished = ExecuteBuildingUpdateCommand("building-demolish", {std::to_string(target)}) ? 1 : 0;
        } else if (object && object->m_type == object::ObjectType::Scenery) {
            // What Scenery::RequestRemoval posts once the background is restored: MT_STATECHANGE (3), data 3, sender -3, receiver the scenery.
            messages::Message message{};
            message.sender = -3;
            message.receiver = target;
            message.type = messages::MT_STATECHANGE;
            message.data = 3;
            messages::message_facade::Post(world->m_messageHandler, &message);
            demolished = 1;
        }
        OBL_LOG("autotest: staff-demolish-target class=%s state=%d target=%d type=%d demolished=%d", profession->name, x, target,
                object ? static_cast<int>(object->m_type) : -1, demolished);
    } else if (name == "force-pause" && args.size() == 1) {
        // What a world script's PauseGame does (world::CallScriptFunction 0): the pause key ignores a forced pause.
        world->m_paused = world->m_forcedPaused = std::atoi(args[0].c_str()) != 0;
        OBL_LOG("autotest: force-pause paused=%d", world->m_paused);
    } else if (name == "set-cash" && args.size() == 1 && Real(args[0], value)) {
        world->m_player->m_cash = value;
        OBL_LOG("autotest: set-cash cash=%g", value);
    } else if (name == "set-hour" && args.size() == 1 && Real(args[0], value) && value >= 0 && value < 24) {
        int dayLength = 0;
        if (!game::game_parameters::GetInt(game::game_parameters::Current(), &dayLength, "iDAY_LENGTH_IN_SECONDS") || dayLength <= 0) {
            OBL_LOG("autotest: FAILED set-hour invalid day length");
            return true;
        }
        int day = world->m_gameDay;
        float elapsed = dayLength * (day + value / 24.0f);
        // The world update uses a strict `remaining > dayLength` loop: an exact later midnight becomes 24:00 yesterday.
        if (value == 0 && day > 0) elapsed = std::nextafter(elapsed, INFINITY);
        world->m_gameTime = elapsed;
        world->m_gameDayTime = value;
        world->m_previousDayTime = value;
        world->m_lastGameDayTime = value;
        audio::GetMusicPlayer()->SetNight(value >= 19 || value < 7);
        OBL_LOG("autotest: set-hour hour=%g dayLength=%d", value, dayLength);
    } else if (name == "lounger-stale-seat" && args.size() == 1 && (args[0] == "setup" || args[0] == "check")) {
        // A guest that is not lying down holding a lounger seat (scenery_state.cpp): `setup` seats the first walking guest on the first
        // free lounger with the guest's own sit-down message; `check` reports that lounger's seats after a few updates.
        static object::Scenery* s_lounger = nullptr;
        static int s_guest = -1;
        object::Manager& objects = *world->m_objectManager;
        if (args[0] == "setup") {
            s_lounger = nullptr;
            for (uint32_t i = 0; i < objects.m_others.size() && !s_lounger; ++i) {
                auto* object = objects.m_others[i];
                if (object->m_type != object::ObjectType::Scenery || !scenery_names::IsLounger(object->m_Name)) continue;
                auto* lounger = static_cast<object::Scenery*>(object);
                if (lounger->m_NumAttachedGuests == 0 && lounger->m_state == 0) s_lounger = lounger;
            }
            s_guest = -1;
            for (uint32_t i = 0; i < objects.m_guests.size() && s_guest == -1; ++i)
                if (objects.m_guests[i]->m_state == 12 || objects.m_guests[i]->m_state == 13) s_guest = objects.m_guests[i]->m_id;
            if (!s_lounger || s_guest == -1) {
                OBL_LOG("autotest: FAILED lounger-stale-seat setup: no free lounger or walking guest");
                return true;
            }
            messages::Message sitDown{};
            sitDown.sender = s_guest;
            sitDown.receiver = static_cast<int>(s_lounger->m_id);
            sitDown.type = messages::MT_STATECHANGE;
            sitDown.data = 4;
            s_lounger->ReceiveMessage(&sitDown);
        } else if (!s_lounger) {
            OBL_LOG("autotest: FAILED lounger-stale-seat check without setup");
            return true;
        }
        OBL_LOG("autotest: lounger-stale-seat %s lounger=%u guest=%d seated=%d seat0=%d state=%d", args[0].c_str(), s_lounger->m_id,
                s_guest, s_lounger->m_NumAttachedGuests, s_lounger->m_AttachedGuest[0], s_lounger->m_state);
    } else if (name == "zoom" && args.size() == 1 && Integer(args[0], x) && x >= 0 && x <= 2) {
        // Same request the zoom buttons post (the zoom-in and zoom-out actions write the field); the world update applies it on the next frame
        // (renderer level + scale, camera re-centre), so `step 1` / `await zoom-level` must follow.
        game::GetContext().view.m_zoomRequest = x;
        OBL_LOG("autotest: zoom requested level=%d", x);
    } else if (name == "world-darken" && args.size() == 1 && Integer(args[0], x) && (x == 0 || x == 1)) {
        // The view state's darken flag: the world draw then calls MapView::DarkenByDepth every frame (nothing in the game sets it).
        game::GetContext().view.m_darkenByDepth = static_cast<uint8_t>(x);
        OBL_LOG("autotest: world-darken on=%d", x);
    } else if (name == "queue-blocks" && args.size() == 4) {
        // MapView::QueueBlockRebuild through its address: the blocks (x, y, width, height in block units) the game queues when a building
        // is demolished; MapView::UpdateCamera then rebuilds one per game second (LoadBlockPair + baking the buildings over the block).
        int bx, by, w, h;
        if (!Integer(args[0], bx) || !Integer(args[1], by) || !Integer(args[2], w) || !Integer(args[3], h)) {
            OBL_LOG("autotest: FAILED queue-blocks needs four integers");
            return true;
        }
        ui::GetMapView()->QueueBlockRebuild(bx, by, w, h);
        OBL_LOG("autotest: queue-blocks x=%d y=%d w=%d h=%d", bx, by, w, h);
    } else if (name == "camera" && args.size() == 2 && Integer(args[0], x) && Integer(args[1], y)) {
        // MapView::SetCameraPos through its address: float camera clamped, the int one follows on
        // the next world update, so `step 1` must come before the camera-x/-y probes or a shot.
        ui::MapView* view = ui::GetMapView();
        view->SetCameraPos(x, y);
        OBL_LOG("autotest: camera x=%d y=%d fx=%g fy=%g", x, y, view->m_cameraPositionX, view->m_cameraPositionY);
    } else if (name == "spawn-guests" && args.size() == 1 && Integer(args[0], x) && x >= 0 &&
            x <= object::Manager::kMaxGuests - static_cast<int>(world->m_objectManager->m_guests.size())) {
        tables::WorldParams* params = game::GetWorldParams();
        if (params->m_numGuests < 1) {
            OBL_LOG("autotest: FAILED spawn-guests no guest definitions");
            return true;
        }
        // Use the first loaded guest type and the random walkable-point selection (game CRT rand).
        object::GuestCreateParams record{};
        std::strncpy(record.m_key, params->m_guests[0].m_key.c_str(), sizeof(record.m_key));
        for (int i = 0; i < x; ++i) {
            obl::world::World::RandomWalkPoint(record.m_pos);
            world::CreateObject(*world, object::ObjectType::Guest, &record);
        }
        OBL_LOG("autotest: spawn-guests added=%d guests=%d", x, static_cast<int>(world->m_objectManager->m_guests.size()));
    } else {
        OBL_LOG("autotest: FAILED %s invalid arguments", name.c_str());
    }
    return true;
}
} // namespace obl::test
