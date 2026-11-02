#include "game/ui/gui_action_entries.h"
#include "core/ascii_case.h"
#include "game/objects/building_events_state.h"
#include "game/world/world_control.h"
#include "game/map/grid_state.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/building_events.h"
#include "game/world/world_queries_api.h"
#include "game/game/command_executor.h"
#include "game/tables/building_behavior.h"
#include "game/game/command_controls.h"
#include "game/game/command_ranges.h"
#include "game/feedback/feedback_vocabulary.h"

#include "game/ui/gui_modes_place.h"
#include "game/world/world_params_queries.h"
#include "game/game/game_accessors.h"
#include "game/ui/gui_actions.h"
#include "game/ui/gui_modes_tools.h"
#include "game/objects/building_api.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/repair/repair_manager.h"
#include "game/map/grid_cell.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/gui_controls_types.h"
#include "game/objects/object_base.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/building_methods.h"
#include "game/objects/staff_roster.h"
#include "game/objects/staff_direct.h"
#include "game/objects/object_static.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

// The command executor (docs/architecture/commands.md): each command runs the game calls of the GUI path it replaces, after the checks that
// path makes before it acts. What only the GUI needs (dialogs, sounds, cursors, message boxes) stays in the GUI; a refusal is a logged
// result instead.
namespace obl::game {
namespace {
using namespace command;
using object::Building;
using object::ObjectType;

constexpr CommandResult kApplied{true, ""};
CommandResult Reject(const char* reason)
{
    return {false, reason};
}

// 0xffffffff is the game's "no object" id, and the id lists can hold it, so it never names an object here; the object found must carry the
// id it was asked for.
constexpr ObjectId kNoObject = 0xffffffffu;
object::Base* ObjectById(ObjectId id)
{
    if (id == kNoObject) return nullptr;
    object::Base* object = object::object_manager::ObjectById(GetObjectManager(), id);
    return object && object->m_id == id ? object : nullptr;
}
ObjectType TypeOf(const object::Base* object)
{
    return static_cast<ObjectType>(object->m_type);
}
bool IsPlayerStaff(const object::Base* object)
{
    return gui_modes_tools::IsStaffType(static_cast<int>(object->m_type));
}

// GetBuildingId / GetSceneryId end the game on an unknown key; a command names it as data, so a miss is a rejection.
const tables::BuildingDef* BuildingDefByKey(std::string_view key, int* id = nullptr)
{
    tables::WorldParams* params = GetWorldParams();
    for (int i = 0; i < params->m_numBuildings; ++i) {
        if (!ascii::EqualsNoCase(key, params->m_buildings[i].m_key)) continue;
        if (id) *id = i;
        return &params->m_buildings[i];
    }
    return nullptr;
}
const tables::SceneryDef* SceneryDefByKey(std::string_view key)
{
    tables::WorldParams* params = GetWorldParams();
    for (int i = 0; i < params->m_numScenery; ++i)
        if (ascii::EqualsNoCase(key, params->m_scenery[i].m_key)) return &params->m_scenery[i];
    return nullptr;
}

bool OnMap(int32_t x, int32_t y)
{
    const map::Grid* grid = GetGrid();
    return x >= 0 && y >= 0 && static_cast<uint32_t>(x) < grid->m_width && static_cast<uint32_t>(y) < grid->m_height;
}

bool HasBuildJob(object::Base* building)
{
    return object::building_api::HasBuildJob(static_cast<const object::Building*>(building));
}
bool HasDemolishJob(object::Base* building)
{
    return object::building_api::HasDemolishJob(static_cast<const object::Building*>(building));
}

// A building the player can act on through its own dialog: selecting a building under construction or demolition opens the construction or
// demolition dialog instead (Building::OnSelect), which has none of the building's buttons and sliders. A building with
// MENU NONE (the wharf, the bus stop) opens no dialog at all: without that check the chaos bot sold the wharf, and guests
// leaving the island then hit the "no wharf" FatalError (scenario 9 seed 6).
CommandResult PlayerBuilding(ObjectId id, object::Base*& out)
{
    out = ObjectById(id);
    if (!out) return Reject("no such object id");
    if (TypeOf(out) != ObjectType::Building) return Reject("not a building");
    if (ascii::EqualsNoCase(reinterpret_cast<Building*>(out)->m_definition->m_Menu, "NONE")) return Reject("building has no dialog");
    if (out->m_waitingForDeath) return Reject("building is being removed");
    if (HasBuildJob(out) || HasDemolishJob(out)) return Reject("building is under construction or demolition");
    return kApplied;
}
CommandResult PlayerStaff(ObjectId id, object::Base*& out)
{
    out = ObjectById(id);
    if (!out) return Reject("no such object id");
    if (!IsPlayerStaff(out)) return Reject("not a staff member");
    if (out->m_waitingForDeath) return Reject("staff member is being removed");
    return kApplied;
}

// The virtual SetVariable (key, word*) the GUI script's sliders and toggles call.
void CallSetVariable(object::Base* object, const char* key, uint32_t word)
{
    object->SetVariable(key, &word);
}
uint32_t FloatWord(float value)
{
    uint32_t word;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

// ---- one function per command -------------------------------------------------------------------------------------------------------------

// The place-building action refuses a locked, disallowed or unaffordable building before the mode starts; the mode's click then needs the
// footprint to fit and the cash to cover the price (GuiModePlaceBuilding::Update).
CommandResult Apply(const PlaceBuilding& c)
{
    int id = -1;
    const tables::BuildingDef* definition = BuildingDefByKey(c.key, &id);
    if (!definition) return Reject("no such building key");
    if (!definition->m_footprint) return Reject("building has no footprint");
    if (GetWorld()->BuildingLockedByTree(id)) return Reject("building locked by the tree");
    if (!GetWorld()->BuildingAllowed(id)) return Reject("building not allowed");
    if (c.direction < 0 || c.direction > 3) return Reject("direction outside 0..3");
    if (!OnMap(c.x, c.y)) return Reject("origin off the map");
    if (!gui::BuildingFits(*definition, static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), c.direction))
        return Reject("footprint does not fit");
    if (!gui::BuyBuilding(*definition, static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), c.direction))
        return Reject("not enough cash");
    return kApplied;
}

// GuiModePlaceScenery::OnMouse: the footprint must fit and the cash cover the price. Paths and grass (category 1) are painted tile by tile by
// the surface modes, which have no command yet.
CommandResult Apply(const PlaceScenery& c)
{
    const tables::SceneryDef* definition = SceneryDefByKey(c.key);
    if (!definition) return Reject("no such scenery key");
    if (definition->m_Category == 1) return Reject("paths and grass are painted, not placed");
    if (c.direction < 0 || c.direction > 7) return Reject("direction outside 0..7");
    if (!OnMap(c.x, c.y)) return Reject("origin off the map");
    if (!gui::SceneryFits(*definition, static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), c.direction))
        return Reject("footprint does not fit");
    if (!gui::BuyScenery(*definition, static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.y), c.direction))
        return Reject("not enough cash");
    return kApplied;
}

// GuiModePlaceGrass/the pavement mouse handler and PlaceTile: the tile must take the surface and the cash cover the price (cash - price >= 0).
CommandResult Apply(const PaintSurface& c)
{
    const tables::SceneryDef* definition = SceneryDefByKey(c.key);
    if (!definition) return Reject("no such scenery key");
    if (definition->m_Category != 1) return Reject("not a path or grass");
    if (!OnMap(c.x, c.y)) return Reject("tile off the map");
    const auto x = static_cast<uint32_t>(c.x), y = static_cast<uint32_t>(c.y);
    if (!gui::SurfaceFits(*definition, x, y)) return Reject("tile cannot take the surface");
    if (!gui_modes_place::CashCoversTile(GetWorld()->m_player->m_cash, definition->m_Cost2Build)) return Reject("not enough cash");
    gui::PaintSurfaceTile(*definition, x, y);
    return kApplied;
}

// GuiModeDozer::OnMouse: a click removes what GuiModeDozer::CanRemove allows on the tile, with the removal income.
CommandResult Apply(const Dozer& c)
{
    if (!OnMap(c.x, c.y)) return Reject("tile off the map");
    const auto x = static_cast<uint32_t>(c.x), y = static_cast<uint32_t>(c.y);
    if (!gui::DozerCanRemoveAt(x, y)) return Reject("nothing the dozer can remove");
    gui::DozerRemoveAt(x, y);
    return kApplied;
}

// Whether the building's dialog (its MENU, as Building::OnSelect opens it) has a SellButton among its widgets. The hotel,
// the builders' yard and the complaints hut have a dialog without one; the wharf and bus stop have none at all. Selling the
// wharf or the complaints hut let guests reach the "no wharf" / "no complaints hut" FatalError (scenario 9 seed 6).
constexpr int32_t kButtonWidget = 0; // GuiWidget::m_type of a Button (the gui button constructor)
constexpr int32_t kDefineWidget = 3; // a copy of a `Define` template (GuiDefine), where the shops keep their sell button

bool ListHasSellButton(const ui::GuiContainerChildNode* head)
{
    const ui::GuiAction sell = gui::gui_action_entries::SellButton;
    for (const auto* child = head->m_next; child != head; child = child->m_next) {
        const ui::GuiWidget* widget = child->m_widget;
        if (widget->m_type == kButtonWidget && static_cast<const ui::GuiButton*>(widget)->m_leftAction == sell) return true;
        if (widget->m_type == kDefineWidget && ListHasSellButton(static_cast<const ui::GuiDefine*>(widget)->m_children)) return true;
    }
    return false;
}

bool HasSellButton(const Building& building)
{
    const auto* gui = GetGuiManager();
    for (const auto* node = gui->m_definitions; node; node = node->m_next) {
        const auto* dialog = reinterpret_cast<const ui::GuiDialogData*>(node->m_dialog);
        if (building.m_definition->m_Menu == dialog->m_name) return ListHasSellButton(dialog->m_children);
    }
    return false;
}

// The confirm-sell action: the sell button of the building's dialog.
CommandResult Apply(const Demolish& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.object, building); !check.applied) return check;
    if (!HasSellButton(*reinterpret_cast<Building*>(building))) return Reject("building dialog has no sell button");
    reinterpret_cast<Building*>(building)->BeginDemolish();
    return kApplied;
}

// The hire-staff action: the first home building of the profession with a free place and no build or demolish job takes the hire.
CommandResult Apply(const HireStaff& c)
{
    using namespace gui_actions;
    const char* homeKey = nullptr;
    for (const feedback::Profession& profession : feedback::CurrentVocabulary().professions)
        if (c.profession == profession.name) homeKey = HireInfoOf(profession.hireType).home;
    if (!homeKey) return Reject("no such profession");
    const tables::BuildingDef* home = BuildingDefByKey(homeKey);
    if (!home) return Reject("no home building in this world");
    // A pack without the staff class the home hires (L0 has no staff table entry for its builder) refuses instead of
    // stopping the game in the staff constructor's lookup.
    const tables::WorldParams& params = *GetWorldParams();
    for (const std::string* key : {&home->m_MaleFileName, &home->m_FemaleFileName})
        if (world_params_queries::FindStaffId(params, key->c_str()) == world_params_queries::kNotFound)
            return Reject("pack has no such staff class");
    const auto capacity = static_cast<uint32_t>(home->m_MaxStaff);
    const auto homes = object::object_manager::FindObjectsByName(GetObjectManager(), homeKey);
    for (object::Base* candidate : homes) {
        const auto* building = reinterpret_cast<const Building*>(candidate);
        if (static_cast<uint32_t>(building->m_numFemalestaff + building->m_numMalestaff) >= capacity) continue;
        if (HasBuildJob(candidate) || HasDemolishJob(candidate)) continue;
        object::BuildingHireStaff(reinterpret_cast<Building*>(candidate));
        return kApplied;
    }
    return Reject(homes.empty() ? "no home building" : "no free place in a home building");
}

// The dismiss-confirm action: the member's home dismisses it.
CommandResult Apply(const DismissStaff& c)
{
    object::Base* member = nullptr;
    if (const CommandResult check = PlayerStaff(c.staff, member); !check.applied) return check;
    const int32_t homeId = reinterpret_cast<object::Staff*>(member)->m_HomeID;
    object::Base* home = homeId == -1 ? nullptr : ObjectById(static_cast<ObjectId>(homeId));
    if (!home) return Reject("staff member has no home");
    object::BuildingDismissStaff(reinterpret_cast<Building*>(home), c.staff);
    return kApplied;
}

CommandResult Apply(const SetStaffWage& c)
{
    object::Base* member = nullptr;
    if (const CommandResult check = PlayerStaff(c.staff, member); !check.applied) return check;
    CallSetVariable(member, "STAFF_WAGES", static_cast<uint32_t>(c.wage));
    return kApplied;
}

// One member of CopyWageToClass (gui_actions.h): m_Wage as given, without the slider's wage thought and minimum, then the happiness target.
CommandResult Apply(const CopyStaffWage& c)
{
    object::Base* member = nullptr;
    if (const CommandResult check = PlayerStaff(c.staff, member); !check.applied) return check;
    reinterpret_cast<object::Staff*>(member)->m_Wage = c.wage;
    object::StaffRecalculateHappinessTarget(*reinterpret_cast<object::Staff*>(member));
    return kApplied;
}

CommandResult Apply(const SetStaffShift& c)
{
    object::Base* member = nullptr;
    if (const CommandResult check = PlayerStaff(c.staff, member); !check.applied) return check;
    if (c.start < 0 || c.start > 23 || c.end < 0 || c.end > 23) return Reject("shift hours outside 0..23");
    reinterpret_cast<object::Staff*>(member)->SetShift(c.start, c.end);
    return kApplied;
}

// GuiModeSendStaff::OnMouse on the ground: the tile under the point must be walkable land without a building
// (the send-staff mode's can-direct check without a target).
CommandResult Apply(const SendStaff& c)
{
    object::Base* member = nullptr;
    if (const CommandResult check = PlayerStaff(c.staff, member); !check.applied) return check;
    float point[3] = {c.x, c.y, c.z};
    const grid_math::TileXY tile = GetGrid()->WorldPointToTile(point[0], point[1]);
    if (!OnMap(static_cast<int32_t>(tile.x), static_cast<int32_t>(tile.y))) return Reject("point off the map");
    if (!gui_modes_tools::StaffTileAllowed(GetGrid()->Cell(tile.x, tile.y).m_flags)) return Reject("tile cannot take the order");
    object::StaffDirectToPoint(*reinterpret_cast<object::Staff*>(member), point);
    return kApplied;
}

// GuiModeSendStaff::OnMouse on an object the mode picked: the member's own DirectToObject decides whether it takes the job.
CommandResult Apply(const SendStaffTo& c)
{
    object::Base* member = nullptr;
    if (const CommandResult check = PlayerStaff(c.staff, member); !check.applied) return check;
    if (!ObjectById(c.target)) return Reject("no such target id");
    object::StaffDirectToTarget(*reinterpret_cast<object::Staff*>(member), static_cast<int32_t>(c.target));
    return kApplied;
}

CommandResult Apply(const SetBuildingOpen& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.building, building); !check.applied) return check;
    if (c.open)
        object::BuildingOpen(reinterpret_cast<Building*>(building));
    else
        object::BuildingClose(reinterpret_cast<Building*>(building));
    return kApplied;
}

// The call-repair action / CallCleaner: an urgent job for the object and its "called" flag.
CommandResult Apply(const CallRepair& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.building, building); !check.applied) return check;
    repair::repair_facade::AddUrgentJob(GetWorld()->m_repairManager, static_cast<int>(c.building));
    reinterpret_cast<object::Prop*>(building)->SetUrgentRepairCalled(1);
    return kApplied;
}
CommandResult Apply(const CallCleaner& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.building, building); !check.applied) return check;
    cleaning::cleaning_facade::AddUrgentJob(GetWorld()->m_cleaningManager, static_cast<int>(c.building));
    reinterpret_cast<object::Prop*>(building)->SetUrgentCleaningCalled(1);
    return kApplied;
}

// The price slider of a commodity slot: the building's SetVariable("COMMOD_PRICE_<slot>"), which does not check the slot itself.
CommandResult Apply(const SetPrice& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.building, building); !check.applied) return check;
    if (c.slot < 0 || c.slot >= reinterpret_cast<Building*>(building)->m_definition->m_NumCommodities)
        return Reject("no such commodity slot");
    const std::string key = "COMMOD_PRICE_" + std::to_string(c.slot);
    CallSetVariable(building, key.c_str(), FloatWord(c.price));
    return kApplied;
}

// The price of a show of the building's schedule: SetVariable("EVENT<slot>_PRICE"), which writes into the schedule without checking that
// there is one and leaves a show in progress alone; both are refusals here.
CommandResult Apply(const SetEventPrice& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.building, building); !check.applied) return check;
    if (c.slot < 0 || c.slot > 2) return Reject("show slot outside 0..2");
    object::BuildingEvents* events = reinterpret_cast<Building*>(building)->m_events;
    if (!events) return Reject("building has no show schedule");
    if (events->IsSlotActive(c.slot)) return Reject("show in progress");
    const std::string key = "EVENT" + std::to_string(c.slot) + "_PRICE";
    CallSetVariable(building, key.c_str(), FloatWord(c.price));
    return kApplied;
}

// A row of the beer or cocktail list (the list's item-selected handler, kinds 4/12): the list offers only the beers the scenario script has switched
// on (m_scriptFlag), so another one is a refusal. A building without a beer slot just takes the type, as the game's setter does.
CommandResult Apply(const SetBeer& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.building, building); !check.applied) return check;
    const tables::WorldParams& params = *GetWorldParams();
    const int id = world_params_queries::FindBeerId(params, c.key.c_str());
    if (id == world_params_queries::kNotFound) return Reject("no such beer key");
    if (!world_params_queries::GetBeerDefinition(params, id)->m_scriptFlag) return Reject("beer not offered");
    object::building_api::SetBeerType(static_cast<object::Building*>(building), id);
    return kApplied;
}

// A row of a show list (kinds 9..11): only available shows are listed, and a slot whose show is in progress offers just the sentinel row.
CommandResult Apply(const SetShow& c)
{
    object::Base* building = nullptr;
    if (const CommandResult check = PlayerBuilding(c.building, building); !check.applied) return check;
    if (c.slot < 0 || c.slot > 2) return Reject("show slot outside 0..2");
    object::BuildingEvents* events = reinterpret_cast<Building*>(building)->m_events;
    if (!events) return Reject("building has no show schedule");
    const tables::WorldParams& params = *GetWorldParams();
    const int id = world_params_queries::FindEventId(params, c.key.c_str());
    if (id == world_params_queries::kNotFound) return Reject("no such show key");
    if (!world_params_queries::GetEventDefinition(params, id)->m_available) return Reject("show not available");
    if (events->IsSlotActive(c.slot)) return Reject("show in progress");
    events->SetSlotEvent(c.slot, id);
    return kApplied;
}

// The exit-dialog resume action: the pause box of a script's PauseGame; both flags drop at this tick.
CommandResult Apply(const ResumeGame&)
{
    world_data::World* world = GetWorld();
    world->m_forcedPaused = 0;
    world->m_paused = 0;
    return kApplied;
}

// The break-object action writes the flag on whatever object the dialog shows; no other check was made.
CommandResult Apply(const BreakObject& c)
{
    object::Base* object = ObjectById(c.object);
    if (!object) return Reject("no such object id");
    object->m_debugBreak = 1;
    return kApplied;
}

CommandResult Apply(const SetGameSpeed& c)
{
    if (c.level < 0 || c.level > 2) return Reject("speed level outside 0..2");
    world::SetGameSpeedLevel(*GetWorld(), c.level);
    return kApplied;
}

CommandResult Apply(const SetPaused& c)
{
    GetWorld()->m_paused = c.paused ? 1 : 0;
    return kApplied;
}

// Typed dispatch preserves the per-class checks before invoking the object's virtual setter. Policies keep their
// integer semantics: BI_BUILDING_OPEN closes on nonzero; STAFF_BEACH_OPEN has distinct building and lifeguard behavior.
template<class T> CommandResult ApplyObjectControl(const T& c, ObjectType owner, bool beachPolicy = false)
{
    object::Base* object = ObjectById(c.object);
    if (!object) return Reject("no such object id");
    const ObjectType type = TypeOf(object);
    if (type != owner && !(beachPolicy && type == ObjectType::Lifeguard)) return Reject("control not allowed for this object");
    object::Base* checked = nullptr;
    const CommandResult check = type == ObjectType::Building ? PlayerBuilding(c.object, checked) : PlayerStaff(c.object, checked);
    if (!check.applied) return check;
    const auto word = ObjectControlWord(c);
    if (!word) return Reject("control takes 0..255");
    CallSetVariable(checked, ObjectControlTraits<T>::kKeyword, *word);
    return kApplied;
}

CommandResult Apply(const SetRepairInterval& c)
{
    return ApplyObjectControl(c, ObjectType::Building);
}

CommandResult Apply(const SetBeerStrength& c)
{
    return ApplyObjectControl(c, ObjectType::Building);
}

CommandResult Apply(const SetBeerPrice& c)
{
    return ApplyObjectControl(c, ObjectType::Building);
}

CommandResult Apply(const SetBuildingOpenPolicy& c)
{
    return ApplyObjectControl(c, ObjectType::Building);
}

// Only the lifeguard tower's dialog has the beach toggle. Any other building takes the keyword too and opens its area -1,
// which walks the beach area chain from its head (a null read on a map without beach areas: chaos s12 seed 4, s9 seed 6).
CommandResult Apply(const SetBeachOpenPolicy& c)
{
    object::Base* object = ObjectById(c.object);
    if (object && TypeOf(object) == ObjectType::Building &&
            reinterpret_cast<Building*>(object)->m_definition->m_Behavior != BuildingBehavior::LifeguardPost)
        return Reject("building has no beach toggle");
    return ApplyObjectControl(c, ObjectType::Building, true);
}

CommandResult Apply(const SetCleanerToilets& c)
{
    return ApplyObjectControl(c, ObjectType::Cleaner);
}

CommandResult Apply(const SetCleanerBins& c)
{
    return ApplyObjectControl(c, ObjectType::Cleaner);
}

CommandResult Apply(const SetCleanerLitter& c)
{
    return ApplyObjectControl(c, ObjectType::Cleaner);
}

CommandResult Apply(const SetMechanicRepair& c)
{
    return ApplyObjectControl(c, ObjectType::Mechanic);
}

CommandResult Apply(const SetMechanicVandals& c)
{
    return ApplyObjectControl(c, ObjectType::Mechanic);
}

CommandResult Apply(const SetMechanicWater& c)
{
    return ApplyObjectControl(c, ObjectType::Mechanic);
}

CommandResult Apply(const SetRepGuestTime& c)
{
    return ApplyObjectControl(c, ObjectType::Rep);
}

CommandResult Apply(const SetSecurityHarshness& c)
{
    return ApplyObjectControl(c, ObjectType::SecurityGuard);
}

} // namespace

CommandResult ApplyCommand(const Command& command)
{
    if (!GetWorld() || !GetObjectManager() || !GetWorldParams() || !GetGrid()) return Reject("no world");
    return std::visit([](const auto& c) { return InGuiRange(c) ? Apply(c) : Reject("value outside GUI range"); }, command);
}

} // namespace obl::game
