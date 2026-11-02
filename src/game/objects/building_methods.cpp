#include "core/char16.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/loop_sound_control.h"
#include "game/objects/object_base.h"
#include "game/objects/object_building.h"
#include "game/objects/building_events_state.h"
#include "game/objects/building_queue.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_static.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/building_events.h"
#include "game/ui/scripted_sound_runtime.h"
#include "game/world/world_update.h"
#include "game/world/world_receive.h"
#include "game/objects/building_methods.h"
#include "game/objects/guest.h"
#include "game/ui/furniture_runtime.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/feedback/feedback_manager.h"
#include "game/world/world_queries_api.h"

#include "game/map/grid_state.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/game/host_text.h"
#include "game/tables/building_behavior.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/building_breakdown.h"
#include "game/objects/building_lifecycle.h"
#include "game/objects/building_spawn.h"
#include "game/objects/building_open_state.h"
#include "game/objects/building_set_variable.h"
#include "game/objects/building_staff_visibility.h"
#include "game/objects/building_update.h"
#include "game/construction/construction_manager.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_table.h"
#include "game/objects/object_create_params.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/repair/repair_manager.h"
#include "game/ui/text_table.h"
#include "game/world/world_params.h"
#include "game/objects/object_manager_queries.h"
#include "game/game/diagnostics.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace obl::object {
using audio::CurrentLoopSounds;
using feedback::Feedback;
using game::FatalError;
using game::GetGrid;
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetScriptedSound;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;
using game::ServedBeerCounter;
using ui::TableSeatGuest;

namespace {

using messages::Message;
using object::Building;
using MessageHandler = messages::Handler;
using layout::World;
using tables::OpeningHoursDef;

const auto kHideStaff = &BuildingHideStaffAndEvents;
const auto kShowStaff = &BuildingShowStaffAndEvents;
const auto kClearQueueForClosing = &BuildingQueueClearForClosing;
const auto kClearQueueForBreakdown = &BuildingQueueClearForBreakdown;
// The managers behind the breakdown notices stay with their own modules; the building calls them.
const auto kRepairAddJob = &repair::repair_facade::AddJob;
// What the building's Update asks of the rest of the game (groups E/F, building_update.h).
const auto kUpdateOpenState = &BuildingUpdateOpenState;
const auto kCleaningAddJob = &cleaning::cleaning_facade::AddJob;
const auto kSetMoving = &object::SetMovingFlag;
// The vehicles' and the queue's side of ReceiveMessage group 2; all of them are other modules' functions (a vehicle's route, a table's seats, the queue).
const auto kSeatGuest = &TableSeatGuest;
const auto kQueueEnqueue = &BuildingQueueEnqueue;
// ReceiveMessage group 3 (guests and goods): the queue's size and hand-over, the lookup of a good's key, the objects around the building, the delayed send.
const auto kQueueDequeue = &BuildingQueueDequeue;
const auto kQueueBoardGuests = &BuildingQueueBoardQueuedGuests;
// ReceiveMessage group 4 (a guest is ready to be served): the key lookups of the world's tables, the beer table, the resort rating, the show's audience and
// the one guest method the pollution of the water calls.

// What the start and the end of a building's life ask of the game (group H, building_lifecycle.h).
const auto kSpawnInitialStaff = &BuildingSpawnInitialStaff;
const auto kSpawnVehicles = &BuildingSpawnVehicles;
const auto kSpawnTables = &BuildingSpawnTables;
const auto kQueueConstruct = &BuildingQueueConstruct;

// ---- helpers the building delegates to ---------------------------------------------------------------------------------

// world::PostMessageToHumans: the same message record, with the receiver rewritten, goes to every object of
// the classes 6..12 (the staff) and then to the guests (class 3). The list of a class is asked for once and walked by index.
void PostToHumans(World& world, Message& message)
{
    constexpr int kClasses[] = {6, 7, 8, 9, 10, 11, 12, 3};
    for (int objectClass : kClasses) {
        for (object::Base* object : object_manager::GetListByClass(GetObjectManager(), objectClass)) {
            message.receiver = static_cast<int>(object->m_id);
            messages::message_facade::Post(world.m_messageHandler, &message);
        }
    }
}

// "The building is closing" (MT_BUILDING_CLOSING, 0x2d) from the building to everybody who can be inside.
// The two payload words after the data field are zero (nothing reads them for this type).
void PostBuildingClosing(Building& building)
{
    Message message{};
    message.sender = static_cast<int>(building.m_id);
    message.receiver = -1;
    message.type = messages::MT_BUILDING_CLOSING;
    PostToHumans(*GetWorld(), message);
}

const OpeningHoursDef& HoursOf(const Building& building)
{
    return GetWorldParams()->m_openingHours[building.m_definition->m_OpeningHours];
}

OpeningHours HoursValues(const Building& building)
{
    const OpeningHoursDef& hours = HoursOf(building);
    return {hours.m_open, hours.m_close};
}

// ---- the methods -------------------------------------------------------------------------------------------------------

bool ClosedDuringHours(const Building& building)
{
    // Nothing but the two flags is read when neither is set.
    if (!building.m_userClose && !building.m_closedForPower) return false;
    return ClosedByUserOrPowerDuringHours(
            building.m_userClose != 0, building.m_closedForPower != 0, GetWorld()->m_gameDayTime, HoursValues(building));
}

int PowerOutputOf(const Building& building)
{
    return PowerOutput(building.m_definition->m_Power, building.m_OpenStatus, building.m_RepairState);
}

void OpenBuilding(Building& building)
{
    building.m_OpenStatus = 0;
    building.m_userClose = 0;
    kShowStaff(&building);
}

void CloseBuilding(Building& building)
{
    building.m_OpenStatus = 1;
    kClearQueueForClosing(building.m_queue);
    building.m_userClose = 1;
    PostBuildingClosing(building);
    kHideStaff(&building);
}

void ShedPowerOf(Building& building)
{
    building.m_closedForPower = 1;
    building.m_OpenStatus = 1;
    kHideStaff(&building);
    kClearQueueForClosing(building.m_queue);
    PostBuildingClosing(building);
}

// ---- breakdown: lightning and SetBroken (group D) ----------------------------------------------------------------------

// The wear moves one step per strike (ApplyLightningDamage, called by weather.update-lighting-driver when a bolt hits a building) or
// jumps to 3 (SetBroken, from world CreateBuilding for a scripted `Broken` building). The fault is stamped with the clock; the queue is told
// (ClearForBreakdown), the staff hidden, the player notified and a repair job queued through the managers.
void StampFault(Building& building)
{
    World* world = GetWorld();
    building.m_lastFaultDayTime = world->m_gameDayTime;
    ++building.m_NumFaults;
    building.m_LastFaultDay = world->m_gameDay;
}

void SetBrokenOf(Building& building)
{
    World* world = GetWorld();
    building.m_RepairState = 3;
    StampFault(building);
    kRepairAddJob(world->m_repairManager, static_cast<int32_t>(building.m_id), 2);
    building.m_calledForRepair = 1;
}

// "<building name> broke down": the format FEEDBACK_BUILDING_BROKEN with the name BUILDING_NAME_<key>, through the game's own (msvcrt) swprintf.
// The key buffer holds 52 bytes; the keys of the data are far shorter.
void BuildingNotice(const Building& building, const char* formatKey, Char16 (&buffer)[256])
{
    std::string key = "BUILDING_NAME_";
    key += building.m_definition->m_key;
    const Char16* name = ui::text_table::Lookup(key.c_str());
    const Char16* format = ui::text_table::Lookup(formatKey);
    ::obl::ui::host_text::Print(buffer, format, name);
}

void BrokenNotice(const Building& building, Char16 (&buffer)[256])
{
    BuildingNotice(building, "FEEDBACK_BUILDING_BROKEN", buffer);
}

LightningStep ApplyLightningStep(Building& building)
{
    const LightningDecision decision = DecideLightning(building.m_state, building.m_RepairState);
    switch (decision.step) {
    case LightningStep::None: return decision.step;
    case LightningStep::Wear: building.m_RepairState = static_cast<uint32_t>(decision.newRepairState); return decision.step;
    case LightningStep::Break: break;
    }
    StampFault(building);
    building.m_RepairState = 3;
    if (building.m_queue) kClearQueueForBreakdown(building.m_queue);
    World* world = GetWorld();
    building.m_wasBroken = 1;
    building.m_OpenStatus = 1;
    kHideStaff(&building);
    Char16 notice[256];
    BrokenNotice(building, notice);
    feedback::Feedback(world->m_feedbackManager).AddText(notice, static_cast<int32_t>(building.m_id), 4);
    if (!building.m_calledForRepair) {
        kRepairAddJob(world->m_repairManager, static_cast<int32_t>(building.m_id), 2);
        building.m_calledForRepair = 1;
    }
    return decision.step;
}

// ---- staff visibility (group B) ----------------------------------------------------------------------------------------

StaffLists ListsOf(Building& building)
{
    StaffLists lists;
    lists.serving = static_cast<uint32_t>(building.m_numMalestaff + building.m_numFemalestaff);
    lists.staff = building.m_staff;
    lists.busy = building.m_staff_busy;
    lists.nonServe = static_cast<uint32_t>(building.m_NumNonServestaff);
    lists.nonServeIds = building.m_NonServeStaff;
    lists.tables = static_cast<int32_t>(building.m_numTables);
    lists.tableIds = building.m_Tables;
    return lists;
}

// The game's side of the Env of building_staff_visibility.h. A missing object (a stale id in a staff or table
// list) is skipped.
class StaffEnv {
public:
    explicit StaffEnv(Building& building) : m_building(building) {}
    int ClassOf(int32_t id) const
    {
        const object::Base* object = object_manager::ObjectById(GetObjectManager(), id);
        return object ? static_cast<int>(object->m_type) : -1;
    }
    void SetVisible(int32_t id, bool visible) const
    {
        if (object::Base* object = object_manager::ObjectById(GetObjectManager(), id)) object->m_visible = visible ? 1 : 0;
    }
    bool WaiterOf(int32_t tableId, int32_t& waiterId) const
    {
        const auto* table = static_cast<const object::Furniture*>(object_manager::ObjectById(GetObjectManager(), tableId));
        if (!table || !table->m_definition->m_hasPlates) return false;
        waiterId = table->m_WaiterID;
        return true;
    }
    bool HasEvents() const { return m_building.m_events != nullptr; }
    int ActiveEventId() const { return m_building.m_events->GetActiveEventId(); }
    void HideEventActors() const { m_building.m_events->HideActors(); }
    void CancelEvent() const { m_building.m_events->CancelCurrent(); }
    void ShowEventActors() const { m_building.m_events->ShowActors(); }

private:
    Building& m_building;
};

void HideStaff(Building& building)
{
    StaffEnv env(building);
    HideStaffAndEvents(env, ListsOf(building));
}

void ShowStaff(Building& building)
{
    StaffEnv env(building);
    ShowStaffAndEvents(env, ListsOf(building));
}

OpenStateStep UpdateOpenState(Building& building)
{
    World* world = GetWorld();
    OpenStateInput in;
    in.closedForPower = building.m_closedForPower != 0;
    in.userClosed = building.m_userClose != 0;
    in.repairState = building.m_RepairState;
    in.openStatus = building.m_OpenStatus;
    in.power = building.m_definition->m_Power;
    in.powerDemand = world->m_powerDemand;
    in.currentPower = world->m_currentPower;
    // The hour and the opening hours are read only on the paths that need them; they are cheap and have no side effects.
    if (!in.closedForPower && !in.userClosed && in.repairState != 3) {
        in.hour = static_cast<uint32_t>(WholeHour(world->m_gameDayTime));
        in.hours = HoursValues(building);
    }
    const OpenStateStep step = DecideOpenState(in);
    switch (step) {
    case OpenStateStep::None: break;
    case OpenStateStep::ClearPowerClosed:
        building.m_closedForPower = 0;
        world::UpdateBuildingTotals(*world);
        break;
    case OpenStateStep::Open:
        building.m_OpenStatus = 0;
        kShowStaff(&building);
        break;
    case OpenStateStep::Close:
        building.m_OpenStatus = 1;
        kClearQueueForClosing(building.m_queue);
        kHideStaff(&building);
        PostBuildingClosing(building);
        break;
    }
    return step;
}

// ---- handlers -------------------------------------------------------------------------------------------------------------

// Call counters (`call-report objects.building-<method>`).
test::CallCounter g_closed("objects.building-is-user-or-power-closed-during-hours");
test::CallCounter g_power("objects.building-get-power-output");
test::CallCounter g_updateOpen("objects.building-update-open-state");
test::CallCounter g_open("objects.building-open");
test::CallCounter g_close("objects.building-close");
test::CallCounter g_shed("objects.building-shed-power");
test::CallCounter g_setBroken("objects.building-set-broken");
test::CallCounter g_setVariable("objects.building-set-variable");
test::CallCounter g_lightning("objects.building-apply-lightning-damage");

void ApplyLightningStepOf(Building& building)
{
    ApplyLightningStep(building);
}

// ---- SetVariable (group L2) ---------------------------------------------------------------------------------------------

// The game's side of the Env of building_set_variable.h. The tower's beach mode moves the grid and posts to the world (the keyword is covered by
// the unit test).
class SetVariableEnv {
public:
    const char* CommodityDisplayName(int id) const { return world::world_params::GetCommodityDisplayName(GetWorldParams(), id); }
    const tables::BeerDef* Beer(int id) const { return world::world_params::GetBeerDefinition(GetWorldParams(), id); }
    void Open(Building& building) const { OpenBuilding(building); }
    void Close(Building& building) const { CloseBuilding(building); }
    bool EventSlotActive(Building& building, int slot) const { return building.m_events->IsSlotActive(slot); }
    void SetEventPrice(Building& building, int slot, uint32_t word) const
    {
        std::memcpy(&building.m_events->m_slots[slot].m_price, &word, sizeof(word));
    }
    void SetBeachOpenMode(Building& building, int mode) const { building.SetBeachOpenMode(mode); }
    void Fatal(const char* key) const { FatalError("A script set unrecognized building variable \"%s\"", key); }
};

void SetVariableOf(Building& building, const char* key, const void* value)
{
    SetVariableEnv env;
    building_set_variable::SetVariable(building, key, value, env);
}

void RunSetVariable(Building* building, const char* key, const void* value)
{
    g_setVariable.Count();
    SetVariableOf(*building, key, value);
}

// ---- staff visibility: handlers -------------------------------------------------------------------------------------------

test::CallCounter g_hideStaff("objects.building-hide-staff-and-events");
test::CallCounter g_showStaff("objects.building-show-staff-and-events");

// ---- Building::Update (group F) -----------------------------------------------------------------------------------

// The game's side of the Env of building_update.h: every call, in the order the pure code asks for it.
class UpdateEnv {
public:
    explicit UpdateEnv(Building& building) : m_building(building), m_world(GetWorld()), m_id(static_cast<int32_t>(building.m_id)) {}

    // the wear model
    int Rand() const { return ::obl::game::Rand(); }
    void RequestInspection() const { kRepairAddJob(m_world->m_repairManager, m_id, 0); }
    void ClearQueueForBreakdown() const { kClearQueueForBreakdown(m_building.m_queue); }
    void HideStaff() const { kHideStaff(&m_building); }
    void NotifyBroken() const
    {
        Char16 notice[256];
        BrokenNotice(m_building, notice);
        feedback::Feedback(m_world->m_feedbackManager).AddText(notice, m_id, 4);
    }
    void RequestRepair() const { kRepairAddJob(m_world->m_repairManager, m_id, 2); }

    // the working tail
    void CloseDialog() const { GetGuiManager()->CloseDialogByProvider(&m_building); }
    void UpdateEvents() const { m_building.m_events->Update(); }
    void UpdateOpenState() const { kUpdateOpenState(&m_building); }
    float IsBuilding() const { return construction::runtime::IsBuilding(m_world->m_constructionManager, m_id); }
    float IsDemolishing() const { return construction::runtime::IsDemolishing(m_world->m_constructionManager, m_id); }
    bool IsCleanerClaimed() const { return m_building.IsCleanerClaimed(); }
    void SendCleaningMessage() const
    {
        // SendMessage posts the record to the message handler; the three last words are not used by the receiver.
        Message message{};
        message.sender = m_id;
        message.receiver = m_id;
        message.type = messages::MT_LOOK_AROUND;
        messages::message_facade::Post(m_world->m_messageHandler, &message);
    }
    void AddCleaningJob() const { kCleaningAddJob(m_world->m_cleaningManager, m_id); }
    void SetMoving() const { kSetMoving(m_building, true); }
    void LoopSoundStart(const char* name) const { CurrentLoopSounds()->StartLoop(name, m_id, kLoopSoundVolume, true); }
    void LoopSoundStop() const { CurrentLoopSounds()->StopLoop(m_id); }
    void BuildingSoundStart() const { GetScriptedSound()->StartBuildingSound(m_id); }
    void BuildingSoundStop() const { audio::ScriptedSound::StopBuildingSound(m_id); }

    // the building site and the demolition
    bool IsStarted() const { return construction::runtime::IsStarted(m_world->m_constructionManager, m_id); }
    int ActiveEventId() const { return m_building.m_events->GetActiveEventId(); }
    void CancelCurrentEvent() const { m_building.m_events->CancelCurrent(); }
    void TickHoarding() const { construction::runtime::TickHoarding(m_world->m_constructionManager, m_id); }
    object::Base* GetObject(int32_t id) const { return object_manager::ObjectById(GetObjectManager(), id); }
    void Post(const Message& message) const
    {
        Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(m_world->m_messageHandler, &copy);
    }
    void SetStarted() const { construction::runtime::SetStarted(m_world->m_constructionManager, m_id, 1); }

private:
    Building& m_building;
    World* m_world;
    int32_t m_id;
};

// Building::Update as the game runs it: the static object's Update first (its failure ends the game), then the dispatcher.
UpdateReport UpdateOfBuilding(Building& building)
{
    if (!building.Prop::Update()) {
        FatalError("Update of building \"%s\" failed", building.m_Name);
    }
    World* world = GetWorld();
    UpdateEnv env(building);
    return UpdateBuilding(env, building, world->m_gameDay, world->m_gameDayTime, world->m_breakDownsAllowed != 0);
}

test::CallCounter g_update("objects.building-update");
// Branches the default path takes (`call-report objects.building-update-<branch>`), counted on every call: they are rare enough to need proof.
test::CallCounter g_updateWorking("objects.building-update-working");
test::CallCounter g_updateWearActive("objects.building-update-wear-active");
test::CallCounter g_updateInspection("objects.building-update-inspection");
test::CallCounter g_updateRolled("objects.building-update-rolled");
test::CallCounter g_updateWorn("objects.building-update-wear-step");
test::CallCounter g_updateBroke("objects.building-update-break");
test::CallCounter g_updateCleaning("objects.building-update-cleaning");
test::CallCounter g_updateFlies("objects.building-update-flies");
test::CallCounter g_updateHire("objects.building-update-hire-idle");
test::CallCounter g_updateStartup("objects.building-update-startup");
test::CallCounter g_updateDemolishWait("objects.building-update-demolish-wait");
test::CallCounter g_updateDemolished("objects.building-update-demolish-start");

void CountUpdate(const UpdateReport& report)
{
    switch (static_cast<uint32_t>(report.state)) {
    case kBuildingStateNull:
    case kBuildingStateOccupied:
        g_updateWorking.Count();
        if (report.wear.active) g_updateWearActive.Count();
        if (report.wear.inspection) g_updateInspection.Count();
        if (report.wear.rolled) g_updateRolled.Count();
        if (report.wear.step == LightningStep::Wear) g_updateWorn.Count();
        if (report.wear.step == LightningStep::Break) g_updateBroke.Count();
        if (report.tail.cleaning) g_updateCleaning.Count();
        if (report.tail.flies == LoopCall::Start) g_updateFlies.Count();
        if (report.tail.hire == LoopCall::Start) g_updateHire.Count();
        break;
    case kBuildingStateStartup: g_updateStartup.Count(); break;
    case kBuildingStateDemolish:
        if (report.demolish.waiting) g_updateDemolishWait.Count();
        if (report.demolish.emptied) g_updateDemolished.Count();
        break;
    default: break;
    }
}

char RunUpdate(Building* building)
{
    g_update.Count();
    CountUpdate(UpdateOfBuilding(*building));
    return 1;
}

// ---- OnEnterState, OnExitState, ReceiveMessage (group H) ---------------------------------------------------------------

LifecycleWorld LifecycleWorldOf(const World& world)
{
    LifecycleWorld view;
    view.startup = world.m_startup != 0;
    view.powerUse = world.m_powerUse;
    view.maxPower = world.m_maxPower;
    view.currentPower = world.m_currentPower;
    view.gameTime = world.m_gameTime;
    view.gameDay = world.m_gameDay;
    view.gameDayTime = world.m_gameDayTime;
    return view;
}

// The game's side of the Env of building_lifecycle.h: every call.
class LifecycleEnv {
public:
    explicit LifecycleEnv(Building& building) : m_building(building), m_world(GetWorld()), m_id(static_cast<int32_t>(building.m_id)) {}

    // OnEnterState
    LoungerTile BuildingTile() const
    {
        int tile[2];
        m_building.GetTilePosition(tile);
        return {static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1])};
    }
    LoungerTile LoungerNeighbour(LoungerTile tile, int direction) const
    {
        const grid_math::TileXY next = GetGrid()->GetNeighbour(tile.x, tile.y, direction);
        return {next.x, next.y};
    }
    uint32_t LoungerFlags(LoungerTile tile) const
    {
        auto* grid = GetGrid();
        const uint32_t index = grid->TileIndex(tile.x, tile.y);
        return grid->m_cells[index].m_flags;
    }
    void PlaceLounger(LoungerTile tile) const
    {
        object::SceneryCreateParams placement{};
        std::strcpy(placement.m_key, "Lounger");
        std::strcpy(placement.m_name, "Lounger");
        placement.m_tile[0] = tile.x;
        placement.m_tile[1] = tile.y;
        placement.m_ownerBuildId = m_id;
        Message message{};
        message.sender = -2;
        message.receiver = -2;
        message.data = 1;
        world::PostPlaceCommand(message, &placement);
    }
    void NotifyNoBuilders() const
    {
        Char16 notice[256];
        BuildingNotice(m_building, "FEEDBACK_NO_BUILDERS", notice);
        feedback::Feedback(m_world->m_feedbackManager).AddText(notice, m_id, 4);
    }
    int CountUsableBuilders() const { return construction::runtime::CountUsableBuilders(m_world->m_constructionManager); }
    void UpdateOpenState() const { kUpdateOpenState(&m_building); }
    void StartBuild() const { construction::runtime::StartBuild(m_world->m_constructionManager, m_id); }
    void StartDemolish() const { construction::runtime::StartDemolish(m_world->m_constructionManager, m_id); }
    void FinishDemolish() const { construction::runtime::FinishDemolish(m_world->m_constructionManager, m_id); }
    int CountSwimmingAreaKeepers(int32_t area) const
    {
        int same = 0;
        for (const object::Base* post : obl::object_manager::FindBuildings(*GetObjectManager(), BuildingBehavior::LifeguardPost)) {
            if (static_cast<const Building*>(post)->m_InfluenceArea == area) ++same;
        }
        return same;
    }
    void OpenSwimmingArea(int32_t area) const
    {
        GetGrid()->SetAreaOpen(0, area);
        GetGrid()->SetAreaOpen(1, area);
    }
    void PoolBallPosition(float& x, float& y) const
    {
        int tile[2];
        m_building.GetTilePosition(tile);
        const auto* sprite = static_cast<const ui::SpriteInstance*>(m_building.m_sprite);
        const grid_math::PathPoint point = GetGrid()->GetFootprintOffsetPoint(static_cast<uint32_t>(tile[0]),
                static_cast<uint32_t>(tile[1]), m_building.m_definition->m_GridWidth, m_building.m_definition->m_GridHeight,
                kPoolBallOffsetX, kPoolBallOffsetY, static_cast<uint32_t>(sprite->m_direction));
        x = point.x;
        y = point.y;
    }
    void BuildingSoundStart() const { GetScriptedSound()->StartBuildingSound(m_id); }

    // OnExitState
    int FindSwimmingArea() const
    {
        return GetGrid()->GetBeachAreaOfPoint(m_building.m_WorldXYZ[0], m_building.m_WorldXYZ[1], m_building.m_WorldXYZ[2]);
    }
    bool IsSwimmingAreaOpen(int32_t area) const { return GetGrid()->IsAreaOpen(1, area); }
    void SpawnInitialStaff() const { kSpawnInitialStaff(&m_building); }
    void SpawnVehicles() const { kSpawnVehicles(&m_building); }
    void SpawnTables() const { kSpawnTables(&m_building); }
    void CloseDialog() const { GetGuiManager()->CloseDialogByProvider(&m_building); }
    void Reselect() const { m_building.Building::OnSelect(1, 0); } // no class of building overrides it
    void FinishBuild() const { construction::runtime::FinishBuild(m_world->m_constructionManager, m_id); }
    void* CreateQueue() const
    {
        return kQueueConstruct(new object::BuildingQueue, static_cast<uint32_t>(m_building.m_definition->m_MaxQueue),
                m_building.m_serve_point[0], m_building.m_serve_point[1], m_building.m_queue_direction[0], m_building.m_queue_direction[1],
                static_cast<uint32_t>(m_id));
    }
    void HideStaff() const { kHideStaff(&m_building); }
    void ClearQueueForClosing() const { kClearQueueForClosing(m_building.m_queue); }
    void PostClosing() const { PostBuildingClosing(m_building); }
    object::BuildingEvents* CreateEvents() const
    {
        return static_cast<object::BuildingEvents*>(object::CreateBuildingEvents(new object::BuildingEvents, m_id));
    }
    void RequestInspection() const { kRepairAddJob(m_world->m_repairManager, m_id, 0); }

    // MT_STAFF_CREATED
    bool IsFemaleStaff(int32_t staffDefinitionId) const
    {
        return static_cast<const tables::StaffDef*>(world::world_params::GetStaffDefinition(GetWorldParams(), staffDefinitionId))
                       ->m_female != 0;
    }
    void HideServiceStaff(int32_t id) const
    {
        object::Base* object = object_manager::ObjectById(GetObjectManager(), id); // a created member is always there
        if (object && object->m_type == object::ObjectType::ServiceStaff) object->m_visible = 0;
    }

    // MT_VEHICLE_CREATED, MT_GUEST_LEFT_VEHICLE, MT_WAIT_FOR_BOAT
    void AddRouteStop(int32_t vehicleId) const
    {
        if (object::Base* vehicle = object_manager::ObjectById(GetObjectManager(), vehicleId)) // the sender of the message
            static_cast<object::Vehicle*>(vehicle)->AddRouteStop(vehicle->m_WorldXYZ[0], vehicle->m_WorldXYZ[1], vehicle->m_WorldXYZ[2]);
    }
    bool SeatGuest(int32_t tableId, int32_t guestId) const
    {
        if (object::Base* table = object_manager::ObjectById(GetObjectManager(), tableId)) // ditto
            return kSeatGuest(*static_cast<object::Furniture*>(table), guestId);
        return false;
    }
    void Enqueue(int32_t guestId, bool moveGuest, int32_t slot) const
    {
        if (m_building.m_queue) // a building that gets the message has its queue; a null one is skipped
            kQueueEnqueue(m_building.m_queue, static_cast<uint32_t>(guestId), slot, moveGuest);
    }

    // MT_REPAIR
    void Post(const Message& message) const
    {
        Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(m_world->m_messageHandler, &copy);
    }
    void PostDelayed(const Message& message, float delay) const
    {
        Message copy = message;
        messages::message_facade::PostDelayed(m_world->m_messageHandler, &copy, delay);
    }

    // MT_LOOK_AROUND, MT_COMMOD_QUALITY, MT_GUEST_SERVED
    uint32_t ObjectsNear(int32_t radius, object::Base* const*& list) const
    {
        int tile[2];
        m_building.GetTilePosition(tile);
        m_found = object_manager::GetObjectsNearTile(GetObjectManager(), tile[0], tile[1], radius);
        list = m_found.data();
        return static_cast<uint32_t>(m_found.size());
    }
    const char* CommodityName(int32_t commodityId) const { return world::world_params::GetCommodityName(GetWorldParams(), commodityId); }
    float EventPrice() const
    {
        const object::BuildingEvents* events = m_building.m_events; // a LIVEVENT building has its schedule
        return events ? events->m_slots[events->m_CurrentSlot].m_price : 0.0f;
    }
    uint32_t QueueSize() const
    {
        return m_building.m_queue ? static_cast<uint32_t>(static_cast<const object::BuildingQueue*>(m_building.m_queue)->m_qlQueue.size())
                                  : 0;
    } // no queue: nothing happens
    bool Dequeue(uint32_t& guest, int32_t& slot) const { return kQueueDequeue(m_building.m_queue, &guest, &slot); }
    void BoardQueuedGuests(int32_t sender) const
    {
        if (m_building.m_queue) // ditto
            kQueueBoardGuests(m_building.m_queue, static_cast<uint32_t>(sender));
    }
    object::Base* GetObject(int32_t id) const { return object_manager::ObjectById(GetObjectManager(), id); }
    // An unexpected sender ends the game: FatalError shows the sender's class and state and the building's state, name and open status.
    void MissingQueue(int32_t senderId) const
    {
        const object::Base* sender = object_manager::ObjectById(GetObjectManager(), senderId);
        FatalError("Building queue got a message it cannot handle\n sender type %d\n sender state %d\n building state %d\n building %s\n "
                   "open status %d",
                sender ? static_cast<int>(sender->m_type) : 0, sender ? static_cast<int>(sender->m_state) : 0,
                static_cast<int>(m_building.m_state), m_building.m_Name, static_cast<int>(m_building.m_OpenStatus));
    }
    void NotifyRepaired(bool noPower) const
    {
        Char16 notice[256];
        BuildingNotice(m_building, noPower ? "FEEDBACK_BUILDING_REPAIRED_NOPOWER" : "FEEDBACK_BUILDING_REPAIRED", notice);
        feedback::Feedback(m_world->m_feedbackManager).AddText(notice, m_id, 4);
    }

    // MT_IM_READY
    World& GameWorld() const { return *m_world; }
    float EventCost() const
    {
        const object::BuildingEvents* events = m_building.m_events; // as EventPrice
        return events ? events->m_slots[events->m_CurrentSlot].m_cost : 0.0f;
    }
    int32_t FamilyOf(int32_t commodityId) const { return world::world_params::GetCommodityFamilyOf(GetWorldParams(), commodityId); }
    int32_t FamilyId(const char* key) const { return world::world_params::GetCommodityFamilyId(GetWorldParams(), key); }
    int32_t CommodityId(const char* key) const { return world::world_params::GetCommodityId(GetWorldParams(), key); }
    object::Base* const* FindBuildings(BuildingBehavior behavior, uint32_t& count) const
    {
        m_found = obl::object_manager::FindBuildings(*GetObjectManager(), behavior);
        count = static_cast<uint32_t>(m_found.size());
        return m_found.data();
    }
    void AddFoodIll(int32_t guestId, float amount) const
    {
        if (object::Base* guest = object_manager::ObjectById(GetObjectManager(), guestId)) // the sender of the message is there
            GuestAddFoodIll(*static_cast<object::Guest*>(guest), amount);
    }
    void AddEventGuest(int32_t guestId) const { m_building.m_events->AddGuest(guestId); }
    const tables::BeerDef& Beer(int32_t id) const
    {
        return *world::world_params::GetBeerDefinition(GetWorldParams(), id);
    } // -1 reads the record before the table; the tests pin this
    int32_t& BeerCounter() const { return ServedBeerCounter(); }
    uint32_t ResortRating() const { return static_cast<uint32_t>(m_world->ResortRating()); }
    int Rand() const { return ::obl::game::Rand(); }

private:
    Building& m_building;
    World* m_world;
    mutable object_manager::ObjectList m_found; // the last list query's result, which the pure logic reads through a pointer
    int32_t m_id;
};

// Branches the default path takes (`call-report <name>`); like the Update's they are counted on every call. These methods change the world,
// so tests compare whole games; the BEACHHUT driver also compares its suppressed effects on a recorded rand tape.
test::CallCounter g_enter("objects.building-on-enter-state");
test::CallCounter g_enterBeachHut("objects.building-enter-state-beach-hut");
test::CallCounter g_enterWorking("objects.building-enter-state-working");
test::CallCounter g_enterPool("objects.building-enter-state-pool");
test::CallCounter g_enterStartup("objects.building-enter-state-startup");
test::CallCounter g_enterNoBuilders("objects.building-enter-state-no-builders");
test::CallCounter g_enterDemolish("objects.building-enter-state-demolish");
test::CallCounter g_enterDemolished("objects.building-enter-state-demolished");
test::CallCounter g_enterAreaOpened("objects.building-enter-state-area-opened");
test::CallCounter g_exit("objects.building-on-exit-state");
test::CallCounter g_exitStartup("objects.building-exit-state-startup");
test::CallCounter g_exitSwimmingArea("objects.building-exit-state-swimming-area");
test::CallCounter g_exitReselected("objects.building-exit-state-reselected");
test::CallCounter g_exitFinished("objects.building-exit-state-finished");
test::CallCounter g_exitShed("objects.building-exit-state-shed");
test::CallCounter g_exitStage("objects.building-exit-state-stage");
test::CallCounter g_exitInspection("objects.building-exit-state-inspection");
test::CallCounter g_receive("objects.building-receive-message");
test::CallCounter g_receiveStaticDefault("objects.building-receive-message-static-default");
test::CallCounter g_receiveRepairMore("objects.building-receive-message-repair-more");
test::CallCounter g_receiveRepairDone("objects.building-receive-message-repair-done");
test::CallCounter g_receiveRepairNotified("objects.building-receive-message-repair-notified");
test::CallCounter g_receiveRepairNoPower("objects.building-receive-message-repair-no-power");
test::CallCounter g_receiveEventFinished("objects.building-receive-message-event-finished");
test::CallCounter g_receiveStaffServing("objects.building-receive-message-staff-serving");
test::CallCounter g_receiveStaffOther("objects.building-receive-message-staff-other");
test::CallCounter g_receiveStaffHidden("objects.building-receive-message-staff-hidden");
test::CallCounter g_receiveNewDay("objects.building-receive-message-new-day");
test::CallCounter g_receiveFlagPolled("objects.building-receive-message-flag-polled");
test::CallCounter g_receiveFlagChanged("objects.building-receive-message-flag-changed");
test::CallCounter g_receiveTable("objects.building-receive-message-table-created");
test::CallCounter g_receiveLounger("objects.building-receive-message-lounger-created");
test::CallCounter g_receiveVehicleCreated("objects.building-receive-message-vehicle-created");
test::CallCounter g_receiveGuestLeft("objects.building-receive-message-guest-left");
test::CallCounter g_receiveGuestSeated("objects.building-receive-message-guest-seated");
test::CallCounter g_receiveBoard("objects.building-receive-message-board-vehicle");
test::CallCounter g_receiveWaitForBoat("objects.building-receive-message-wait-for-boat");
test::CallCounter g_receiveWaitMoved("objects.building-receive-message-wait-moved");
test::CallCounter g_receiveLookAround("objects.building-receive-message-look-around");
test::CallCounter g_receiveLookSpotted("objects.building-receive-message-look-spotted");
test::CallCounter g_receiveCommodQuality("objects.building-receive-message-commod-quality");
test::CallCounter g_receiveCommodRental("objects.building-receive-message-commod-rental");
test::CallCounter g_receiveCommodNext("objects.building-receive-message-commod-next");
test::CallCounter g_receiveServedBoarded("objects.building-receive-message-served-boarded");
test::CallCounter g_receiveServedIdle("objects.building-receive-message-served-idle");
test::CallCounter g_receiveServedFreedVehicle("objects.building-receive-message-served-freed-vehicle");
test::CallCounter g_receiveServedFreedStaff("objects.building-receive-message-served-freed-staff");
test::CallCounter g_receiveServedReady("objects.building-receive-message-served-ready");
test::CallCounter g_receiveServedDelayed("objects.building-receive-message-served-ready-delayed");
test::CallCounter g_receiveReady("objects.building-receive-message-im-ready");
test::CallCounter g_receiveReadyLive("objects.building-receive-message-ready-live");
test::CallCounter g_receiveReadyFoodIll("objects.building-receive-message-ready-food-ill");
test::CallCounter g_receiveReadyBeerDrawn("objects.building-receive-message-ready-beer-drawn");
test::CallCounter g_receiveReadySelfTables("objects.building-receive-message-ready-own-tables");
test::CallCounter g_receiveReadySelfLoungers("objects.building-receive-message-ready-own-loungers");
test::CallCounter g_receiveReadySelfBeer("objects.building-receive-message-ready-own-beer");
test::CallCounter g_receiveReadySelfEvent("objects.building-receive-message-ready-own-event");
test::CallCounter g_receiveReadySelfNow("objects.building-receive-message-ready-own-now");
test::CallCounter g_receiveReadySelfDelayed("objects.building-receive-message-ready-own-delayed");
test::CallCounter g_receiveReadyOtherTables("objects.building-receive-message-ready-other-tables");
test::CallCounter g_receiveReadyOtherPlain("objects.building-receive-message-ready-other-plain");
test::CallCounter g_receiveReadyOtherBeer("objects.building-receive-message-ready-other-beer");
test::CallCounter g_receiveRequest("objects.building-receive-message-request-commod");
test::CallCounter g_receiveRequestSnorkel("objects.building-receive-message-request-snorkel");
test::CallCounter g_receiveRequestLilo("objects.building-receive-message-request-lilo");
test::CallCounter g_receiveRequestBoard("objects.building-receive-message-request-board");
test::CallCounter g_receiveRequestLounger("objects.building-receive-message-request-lounger");
test::CallCounter g_receiveRequestLive("objects.building-receive-message-request-live");
test::CallCounter g_receiveRequestNoStock("objects.building-receive-message-request-no-stock");
test::CallCounter g_receivePriceOk("objects.building-receive-message-price-ok");
test::CallCounter g_receivePriceOkVehicle("objects.building-receive-message-price-ok-vehicle");
test::CallCounter g_receivePriceOkStaff("objects.building-receive-message-price-ok-staff");
test::CallCounter g_receivePriceOkSelf("objects.building-receive-message-price-ok-self");
test::CallCounter g_receivePriceOkQueued("objects.building-receive-message-price-ok-queued");
test::CallCounter g_receivePriceNotOk("objects.building-receive-message-price-not-ok");
test::CallCounter g_receivePriceNotOkRental("objects.building-receive-message-price-not-ok-rental");
test::CallCounter g_receivePriceNotOkFreed("objects.building-receive-message-price-not-ok-freed");
test::CallCounter g_receivePriceNotOkNext("objects.building-receive-message-price-not-ok-next");
test::CallCounter g_receivePriceNotOkSwallowed("objects.building-receive-message-price-not-ok-swallowed");
test::CallCounter g_receiveBuilt("objects.building-receive-message-builders-left-built");
test::CallCounter g_receiveRemoved("objects.building-receive-message-builders-left-removed");

void CountReadyRoute(ReadyRoute route)
{
    switch (route) {
    case ReadyRoute::SelfTables: g_receiveReadySelfTables.Count(); break;
    case ReadyRoute::SelfLoungers: g_receiveReadySelfLoungers.Count(); break;
    case ReadyRoute::SelfBeer: g_receiveReadySelfBeer.Count(); break;
    case ReadyRoute::SelfEvent: g_receiveReadySelfEvent.Count(); break;
    case ReadyRoute::SelfNow: g_receiveReadySelfNow.Count(); break;
    case ReadyRoute::SelfDelayed: g_receiveReadySelfDelayed.Count(); break;
    case ReadyRoute::OtherTables: g_receiveReadyOtherTables.Count(); break;
    case ReadyRoute::OtherPlain: g_receiveReadyOtherPlain.Count(); break;
    case ReadyRoute::OtherBeer: g_receiveReadyOtherBeer.Count(); break;
    }
}

void CountEnter(const EnterReport& report)
{
    if (report.beachHut) g_enterBeachHut.Count();
    if (report.working) g_enterWorking.Count();
    if (report.pool) g_enterPool.Count();
    if (report.startup) g_enterStartup.Count();
    if (report.noBuilders) g_enterNoBuilders.Count();
    if (report.demolish) g_enterDemolish.Count();
    if (report.demolished) g_enterDemolished.Count();
    if (report.areaOpened) g_enterAreaOpened.Count();
}

void CountExit(const ExitReport& report)
{
    if (!report.startupDone) return;
    g_exitStartup.Count();
    if (report.swimmingArea) g_exitSwimmingArea.Count();
    if (report.reselected) g_exitReselected.Count();
    if (report.finished) g_exitFinished.Count();
    if (report.shed) g_exitShed.Count();
    if (report.stage) g_exitStage.Count();
    if (report.inspection) g_exitInspection.Count();
}

char RunOnEnterState(Building* building, uint32_t state)
{
    LifecycleEnv env(*building);
    const EnterReport report = EnterState(env, *building, state, LifecycleWorldOf(*GetWorld()));
    g_enter.Count();
    CountEnter(report);
    return 1;
}

char RunOnExitState(Building* building)
{
    g_exit.Count();
    LifecycleEnv env(*building);
    CountExit(ExitState(env, *building, LifecycleWorldOf(*GetWorld())));
    return 1;
}

// The building handles 19 message types itself (docs/architecture/objects.md); every other type is the static object's, whose own ReceiveMessage the
// `default` case calls. Where no result is meaningful (MT_REPAIR, EVENT_FINISHED, NEW_DAY, RE_POLL_FLAG_STATE,
// GUEST_LEFT_VEHICLE, COMMOD_QUALITY, IM_READY, REQUEST_COMMOD, PRICE_OK, PRICE_NOT_OK, BUILDERS_LEFT_BASEPLATE and a BOARD_VEHICLE nobody matched; the callers do
// not read it) the answer is 0; a handled match returns 1.
char RunReceiveMessage(Building* building, Message* message)
{
    switch (message->type) {
    case messages::MT_REPAIR: {
        g_receive.Count();
        LifecycleEnv env(*building);
        const RepairReport report = RepairMessage(env, *building, *message, LifecycleWorldOf(*GetWorld()));
        (report.more ? g_receiveRepairMore : g_receiveRepairDone).Count();
        if (report.notified) g_receiveRepairNotified.Count();
        if (report.noPower) g_receiveRepairNoPower.Count();
        return 0;
    }
    case messages::MT_EVENT_FINISHED:
        g_receive.Count();
        g_receiveEventFinished.Count();
        if (building->m_events) // a building that gets this message always has its schedule
            building->m_events->AdvanceSlot();
        return 0;
    case messages::MT_STAFF_CREATED: {
        g_receive.Count();
        LifecycleEnv env(*building);
        const StaffCreatedReport report = StaffCreated(env, *building, *message);
        (report.serving ? g_receiveStaffServing : g_receiveStaffOther).Count();
        if (report.hidden) g_receiveStaffHidden.Count();
        return 1;
    }
    case messages::MT_NEW_DAY:
        g_receive.Count();
        if (NewDay(*building)) g_receiveNewDay.Count();
        return 0;
    case messages::MT_RE_POLL_FLAG_STATE: {
        g_receive.Count();
        LifecycleEnv env(*building);
        g_receiveFlagPolled.Count();
        if (RePollFlagState(env, *building)) g_receiveFlagChanged.Count();
        return 0;
    }
    case messages::MT_TABLE_CREATED:
        g_receive.Count();
        g_receiveTable.Count();
        TableCreated(*building, *message);
        return 1;
    case messages::MT_LOUNGER_CREATED:
        g_receive.Count();
        g_receiveLounger.Count();
        LoungerCreated(*building, *message);
        return 1;
    case messages::MT_VEHICLE_CREATED: {
        g_receive.Count();
        g_receiveVehicleCreated.Count();
        LifecycleEnv env(*building);
        VehicleCreated(env, *building, *message);
        return 1;
    }
    case messages::MT_GUEST_LEFT_VEHICLE: {
        g_receive.Count();
        LifecycleEnv env(*building);
        const GuestLeftReport report = VehicleLeft(env, *building, *message);
        if (report.matched) g_receiveGuestLeft.Count();
        if (report.seated) g_receiveGuestSeated.Count();
        return 0;
    }
    case messages::MT_BOARD_VEHICLE: {
        g_receive.Count();
        LifecycleEnv env(*building);
        const int matched = BoardVehicle(env, *building, *message);
        for (int i = 0; i < matched; ++i) g_receiveBoard.Count();
        return matched != 0; // 1 after a match; callers ignore the result otherwise
    }
    case messages::MT_WAIT_FOR_BOAT: {
        g_receive.Count();
        g_receiveWaitForBoat.Count();
        LifecycleEnv env(*building);
        if (WaitForBoat(env, *building, *message)) g_receiveWaitMoved.Count();
        return 1;
    }
    case messages::MT_LOOK_AROUND: {
        g_receive.Count();
        g_receiveLookAround.Count();
        LifecycleEnv env(*building);
        if (LookAround(env, *building).guestsNotified != 0) g_receiveLookSpotted.Count();
        return 1;
    }
    case messages::MT_COMMOD_QUALITY: {
        g_receive.Count();
        g_receiveCommodQuality.Count();
        LifecycleEnv env(*building);
        const CommodQualityReport report = CommodQuality(env, *building, *message);
        if (report.rental != RentalGood::None) g_receiveCommodRental.Count();
        if (report.next) g_receiveCommodNext.Count();
        return 0; // the result is not read here either
    }
    case messages::MT_REQUEST_COMMOD: {
        g_receive.Count();
        g_receiveRequest.Count();
        LifecycleEnv env(*building);
        const RequestCommodReport report = RequestCommod(env, *building, *message);
        switch (report.rental) {
        case RentalGood::Snorkel: g_receiveRequestSnorkel.Count(); break;
        case RentalGood::Lilo: g_receiveRequestLilo.Count(); break;
        case RentalGood::Board: g_receiveRequestBoard.Count(); break;
        default: break;
        }
        if (report.lounger) g_receiveRequestLounger.Count();
        if (report.live) g_receiveRequestLive.Count();
        if (!report.available) g_receiveRequestNoStock.Count();
        return 0;
    }
    case messages::MT_IM_READY: {
        g_receive.Count();
        LifecycleEnv env(*building);
        const ImReadyReport report = ImReady(env, *building, *message);
        g_receiveReady.Count();
        if (report.live) g_receiveReadyLive.Count();
        if (report.foodIll) g_receiveReadyFoodIll.Count();
        if (report.beerDrawn) g_receiveReadyBeerDrawn.Count();
        CountReadyRoute(report.route);
        return 0; // the callers do not read the result
    }
    case messages::MT_GUEST_SERVED: {
        g_receive.Count();
        LifecycleEnv env(*building);
        const GuestServedReport report = GuestServed(env, *building, *message);
        if (report.boarded) g_receiveServedBoarded.Count();
        if (report.idle) g_receiveServedIdle.Count();
        if (report.freedVehicle != 0) g_receiveServedFreedVehicle.Count();
        if (report.freedStaff != 0) g_receiveServedFreedStaff.Count();
        if (report.ready != 0) g_receiveServedReady.Count();
        if (report.readyDelayed != 0) g_receiveServedDelayed.Count();
        return 1;
    }
    case messages::MT_PRICE_OK: {
        g_receive.Count();
        g_receivePriceOk.Count();
        LifecycleEnv env(*building);
        switch (PriceOk(env, *building, *message).route) {
        case PriceOkRoute::Vehicle: g_receivePriceOkVehicle.Count(); break;
        case PriceOkRoute::Staff: g_receivePriceOkStaff.Count(); break;
        case PriceOkRoute::SelfServed: g_receivePriceOkSelf.Count(); break;
        case PriceOkRoute::Queued: g_receivePriceOkQueued.Count(); break;
        }
        return 0; // the callers do not read the result
    }
    case messages::MT_PRICE_NOT_OK: {
        g_receive.Count();
        g_receivePriceNotOk.Count();
        LifecycleEnv env(*building);
        const PriceNotOkReport report = PriceNotOk(env, *building, *message);
        if (report.rental != RentalGood::None) g_receivePriceNotOkRental.Count();
        if (report.freedVehicle + report.freedStaff != 0) g_receivePriceNotOkFreed.Count();
        if (report.ready) g_receivePriceNotOkNext.Count();
        if (report.swallowed) g_receivePriceNotOkSwallowed.Count();
        return 0; // ditto
    }
    case messages::MT_BUILDERS_LEFT_BASEPLATE: {
        g_receive.Count();
        LifecycleEnv env(*building);
        (BuildersLeftBaseplate(env, *building, *message) ? g_receiveBuilt : g_receiveRemoved).Count();
        return 0; // ditto
    }
    default:
        // Every type that is not one of the building's 19: the static object's own ReceiveMessage.
        g_receiveStaticDefault.Count();
        return building->Prop::ReceiveMessage(message);
    }
}

// ---- group J: vehicles, tables and the staff list ---------------------------------------------------------------------------------
// SpawnVehicles/SpawnTables (src/game/objects/building_spawn.h) only read the building and post one place command per vehicle/table.
// DismissStaff writes the serving list and its two counters and tells the staff object to leave.

class SpawnEnv {
public:
    explicit SpawnEnv(Building& building) : m_building(building) {}

    Tile BuildingTile() const
    {
        int tile[2];
        m_building.GetTilePosition(tile);
        return {static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1])};
    }
    Tile Neighbour(Tile tile, int direction) const
    {
        const grid_math::TileXY next = GetGrid()->GetNeighbour(tile.x, tile.y, direction);
        return {next.x, next.y};
    }
    Point TileCenter(Tile tile) const
    {
        const grid_math::WorldPoint center = GetGrid()->TileToWorldCenter(tile.x, tile.y);
        return {center.x, center.y};
    }
    Point FootprintPoint(Tile tile, int width, int height, float offsetX, float offsetY, uint32_t rotation) const
    {
        const grid_math::PathPoint point = GetGrid()->GetFootprintOffsetPoint(tile.x, tile.y, width, height, offsetX, offsetY, rotation);
        return {point.x, point.y};
    }
    // The vehicle and table records are C++: the key is a std::string member.
    const char* VehicleKey(int id) const { return world::world_params::GetVehicleDefinition(GetWorldParams(), id)->m_key.c_str(); }
    const char* TableKey(int id) const { return world::world_params::GetTableDefinition(GetWorldParams(), id)->m_key.c_str(); }
    // HireStaff / SpawnInitialStaff
    StaffInfo StaffDefinition(int id) const
    {
        const auto* definition = static_cast<const tables::StaffDef*>(world::world_params::GetStaffDefinition(GetWorldParams(), id));
        return {definition->m_ObjectType.c_str(), definition->m_key.c_str()};
    }
    int BuilderSlot(int32_t id) const
    {
        return static_cast<const object::Builder*>(object_manager::ObjectById(GetObjectManager(), id))->m_BuilderNumber;
    }
    int Rand() const { return ::obl::game::Rand(); }
    void Place(const PlaceCommand& command) const
    {
        object::TableCreateParams payload{};
        std::memcpy(payload.m_key, command.key, sizeof(payload.m_key));
        payload.m_pos[0] = command.x;
        payload.m_pos[1] = command.y;
        payload.m_pos[2] = command.z;
        payload.m_buildingId = command.owner;
        payload.m_orientation = command.rotation;
        Message message{};
        message.sender = -2;
        message.receiver = -2;
        message.data = command.kind;
        world::PostPlaceCommand(message, &payload);
    }

private:
    Building& m_building;
};

SpawnSite SiteOf(const Building& building)
{
    const tables::BuildingDef& def = *building.m_definition;
    const auto* sprite = static_cast<const ui::SpriteInstance*>(building.m_sprite);
    SpawnSite site;
    site.owner = static_cast<int32_t>(building.m_id);
    site.rotation = static_cast<uint32_t>(sprite->m_direction);
    site.width = def.m_GridWidth;
    site.height = def.m_GridHeight;
    site.numVehicles = static_cast<uint32_t>(def.m_NumVehicles);
    site.numTables = static_cast<uint32_t>(def.m_NumTables);
    for (int i = 0; i < 5; ++i) {
        site.vehicle[i] = def.m_Vehicle[i];
        site.vehicleOffset[i][0] = def.m_VehicleGridOffset[i][0];
        site.vehicleOffset[i][1] = def.m_VehicleGridOffset[i][1];
        site.vehicleDir[i] = def.m_VehicleDir[i];
    }
    for (int i = 0; i < 10; ++i) {
        site.table[i] = def.m_Table[i];
        site.tableOffset[i][0] = def.m_TablePos[i][0];
        site.tableOffset[i][1] = def.m_TablePos[i][1];
    }
    return site;
}

void SpawnVehiclesOf(Building& building)
{
    SpawnEnv env(building);
    SpawnVehicles(env, SiteOf(building));
}

void SpawnTablesOf(Building& building)
{
    SpawnEnv env(building);
    SpawnTables(env, SiteOf(building));
}

test::CallCounter g_spawnVehicles("objects.building-spawn-vehicles");
test::CallCounter g_spawnTables("objects.building-spawn-tables");

class DismissEnv {
public:
    bool StaffIsFemale(int32_t id) const
    {
        const auto* staff = static_cast<const object::Staff*>(object_manager::ObjectById(GetObjectManager(), id));
        return staff->m_staffDefinition->m_female != 0;
    }
    void Leave(int32_t id) const
    {
        object::Base* object = object_manager::ObjectById(GetObjectManager(), id);
        static_cast<object::Staff*>(object)->Sack(); // Sack dismisses this staff member.
    }
};

test::CallCounter g_dismiss("objects.building-dismiss-staff");
test::CallCounter g_dismissFound("objects.building-dismiss-staff-found");
test::CallCounter g_dismissFemale("objects.building-dismiss-staff-female");

StaffSite StaffSiteOf(const Building& building)
{
    const tables::BuildingDef& def = *building.m_definition;
    const auto* sprite = static_cast<const ui::SpriteInstance*>(building.m_sprite);
    StaffSite site;
    site.owner = static_cast<int32_t>(building.m_id);
    site.rotation = sprite ? static_cast<uint32_t>(sprite->m_direction) : 0;
    site.width = def.m_GridWidth;
    site.height = def.m_GridHeight;
    std::memcpy(site.servePoint, building.m_serve_point, sizeof(site.servePoint));
    site.males = building.m_numMalestaff;
    site.females = building.m_numFemalestaff;
    std::memcpy(site.staff, building.m_staff, sizeof(site.staff));
    site.maxStaff = def.m_MaxStaff;
    site.numInitial = static_cast<uint32_t>(def.m_NumStaff);
    std::memcpy(site.initial, def.m_Staff, sizeof(site.initial));
    site.staffOffset[0] = def.m_StaffOffset[0];
    site.staffOffset[1] = def.m_StaffOffset[1];
    std::snprintf(site.maleKey, sizeof(site.maleKey), "%s", def.m_MaleFileName.c_str());
    std::snprintf(site.femaleKey, sizeof(site.femaleKey), "%s", def.m_FemaleFileName.c_str());
    return site;
}

bool HireStaffOf(Building& building)
{
    SpawnEnv env(building);
    return HireStaff(env, StaffSiteOf(building));
}

bool SpawnInitialStaffOf(Building& building)
{
    SpawnEnv env(building);
    SpawnInitialStaff(env, StaffSiteOf(building));
    return true;
}

test::CallCounter g_hire("objects.building-hire-staff");
test::CallCounter g_initialStaff("objects.building-spawn-initial-staff");

} // namespace

// The building's own steps (building_methods.h).
void BuildingSpawnVehicles(Building* building)
{
    g_spawnVehicles.Count();
    SpawnVehiclesOf(*building);
}
void BuildingSpawnTables(Building* building)
{
    g_spawnTables.Count();
    SpawnTablesOf(*building);
}
bool BuildingHireStaff(Building* building)
{
    g_hire.Count();
    return HireStaffOf(*building);
}
void BuildingSpawnInitialStaff(Building* building)
{
    g_initialStaff.Count();
    SpawnInitialStaffOf(*building);
}
void BuildingDismissStaff(Building* building, uint32_t staffId)
{
    g_dismiss.Count();
    DismissEnv env;
    const DismissResult result =
            DismissStaff(env, building->m_staff, building->m_numMalestaff, building->m_numFemalestaff, static_cast<int32_t>(staffId));
    if (result.found) g_dismissFound.Count();
    if (result.female) g_dismissFemale.Count();
}
void BuildingHideStaffAndEvents(Building* building)
{
    g_hideStaff.Count();
    HideStaff(*building);
}
void BuildingShowStaffAndEvents(Building* building)
{
    g_showStaff.Count();
    ShowStaff(*building);
}
bool BuildingIsUserOrPowerClosedDuringHours(const Building* building)
{
    g_closed.Count();
    return ClosedDuringHours(*building);
}
void BuildingUpdateOpenState(Building* building)
{
    g_updateOpen.Count();
    UpdateOpenState(*building);
}
void BuildingOpen(Building* building)
{
    g_open.Count();
    OpenBuilding(*building);
}
void BuildingClose(Building* building)
{
    g_close.Count();
    CloseBuilding(*building);
}
int BuildingGetPowerOutput(Building* building)
{
    g_power.Count();
    return PowerOutputOf(*building);
}
void BuildingShedPower(Building* building)
{
    g_shed.Count();
    ShedPowerOf(*building);
}
void BuildingSetBroken(Building* building)
{
    g_setBroken.Count();
    SetBrokenOf(*building);
}
void BuildingApplyLightningDamage(Building* building)
{
    g_lightning.Count();
    ApplyLightningStepOf(*building);
}

bool BuildingOnExitState(Building* building)
{
    return RunOnExitState(building) != 0;
}

void SetBuildingVisitsSinceCleaned(Building& building, int visits)
{
    building.m_VisitsSinceCleaned = visits;
    building.m_calledCleaning = 0;
}

void SetBuildingAge(Building& building, int age)
{
    building.m_BuildingAge = age;
}

void SetBuildingCommodityPrices(Building& building, float price)
{
    for (float& slot : building.m_Commod_Price) slot = price;
}

bool PollBuildingClosedFlag(Building& building)
{
    if (building.m_InfluenceArea == -1 || !building.m_animSprites[0]) return false;
    building.m_animSprites[0]->m_animIndex = 1;
    building.m_animSprites[0]->m_frameIndex = 3;
    Message message{};
    message.sender = static_cast<int>(building.m_id);
    message.receiver = static_cast<int>(building.m_id);
    message.type = messages::MT_RE_POLL_FLAG_STATE;
    messages::message_facade::Post(GetWorld()->m_messageHandler, &message);
    return true;
}

void CompleteBuildingStartup(Building& building)
{
    building.m_state = 1;
    BuildingOnExitState(&building);
    building.m_state = 0;
}

void ArmBuildingFailure(Building& building, int hours, int interval)
{
    building.m_LastFailTime = hours - 100;
    if (interval) {
        building.m_UserRepairInterval = building.m_RepairInterval = interval;
        building.m_LastRepairCall = hours - 100;
    }
}

void ResetBuildingWear(Building& building)
{
    building.m_RepairState = 0;
    building.m_wasBroken = 0;
    building.m_calledForRepair = 0;
    building.m_OpenStatus = 0;
}
} // namespace obl::object

namespace obl::object {
bool Building::OnEnterState(uint32_t state)
{
    return object::RunOnEnterState(this, state);
}
bool Building::Update()
{
    return object::RunUpdate(this);
}
bool Building::OnExitState()
{
    return object::RunOnExitState(this);
}
bool Building::ReceiveMessage(messages::Message* message)
{
    return object::RunReceiveMessage(this, message);
}
void Building::SetVariable(const char* key, const void* value)
{
    object::RunSetVariable(this, key, value);
}
} // namespace obl::object
