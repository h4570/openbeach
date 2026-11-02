#include "importer/src/readers/save/save_writer.h"
#include "importer/src/writers/content_key.h"

#include "importer/src/writers/catalog_writer.h"
#include "core/code_pages.h"
#include "formats/map_pack.h"
#include "formats/save_records.h"
#include "formats/schemas.h"
#include "core/sha256.h"

#include "core/code_page.h"
#include "core/json_float.h"
#include "core/pack_path.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <utility>

namespace obl::convert {

using obl::code_pages::CodePoint;
using obl::code_pages::kUndefinedCodePoint;

using nlohmann::ordered_json;

namespace {

// How an old section record becomes an obl.save field (docs: converter/save-conversion.md). UInts collects the records of an
// indexed label ("m_iObjectiveStatus[2]", matched as "m_iObjectiveStatus[]") into one array.
// A key "list[].field" places the value of record "Label[i]..." at list[i].field; List marks the list's size record
// and makes the (possibly empty) list. Text is narrow text; Name is narrow text
// too, an engine name kept verbatim (its content key follows by the key rule, save-conversion.md).
// ByteFlag retains a byte flag whose meaning is zero versus nonzero. Group maps every label starting
// with the entry's label (a prefix such as "sprite." or "sprites[].") through `group`, under the entry's key. Building,
// Beer and Event are positions in the catalog's `buildings`, `beers` and `events` lists (the WorldParams ids) as keys,
// -1 as null.
enum class Map : uint8_t {
    Float,
    Int,
    UInt,
    Bool,
    ByteFlag,
    Ints,
    Floats,
    UInts,
    WideText,
    Text,
    Name,
    Building,
    Beer,
    Event,
    State,
    List,
    Group,
    Drop,
    Special
};

struct SectionField {
    const char* label; // the emitter's label (save_body.cpp field tables)
    const char* name; // obl.save key; null for dropped and special records
    Map map;
    std::span<const struct SectionField> group = {};
    formats::SaveStateSet states = formats::SaveStateSet::None; // the names of a Map::State record
};

// In the emitter's order. `m_LastGameDayTime` is the hour of the previous frame (World update), `m_lastGameDayTime` the
// hour the day cycle last checked; `m_averageTotty` is the guests' average attractiveness.
constexpr SectionField kWorldFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"m_MapName", nullptr, Map::Special}, // the pack's map id: the stem of the old path
        {"m_fGameStartTime", "game_start_time", Map::Float},
        {"m_fGameTime", "game_time", Map::Float},
        {"m_GameDay", "game_day", Map::Int},
        {"m_GameDayTime", "day_time", Map::Float},
        {"m_LastGameDayTime", "last_day_time", Map::Float},
        {"m_fLastGameDayTime", "previous_day_time", Map::Float},
        {"m_fGameDeltaTime", "delta_time", Map::Float},
        {"m_fGameSpeed", "speed", Map::Float},
        {"m_GameSpeedLevel", "speed_level", Map::Int},
        {"m_fNextStaffFeedbackTime", "next_staff_feedback_time", Map::Float},
        {"m_bStartup", "startup", Map::Bool},
        {"m_NumValidBuildings", nullptr, Map::Special}, // with m_ValidBuildingIDs: available_buildings
        {"m_ValidBuildingIDs", nullptr, Map::Special},
        {"m_bBreakDownsAllowed", "breakdowns_allowed", Map::Bool},
        {"m_bAllowLitter", "litter_allowed", Map::Bool},
        {"m_BoatList", nullptr, Map::Drop}, // raw pointers; the boats are rebuilt from the objects
        {"m_NumBoats", nullptr, Map::Drop},
        {"m_fPowerUse", "power_use", Map::Float},
        {"m_fMaxPower", "max_power", Map::Float},
        {"m_fCurrentPower", "current_power", Map::Float},
        {"m_fPowerDemand", "power_demand", Map::Float},
        {"m_ScriptFlags", "script_flags", Map::Int},
        {"m_ScriptVars", "script_variables", Map::Ints},
        {"m_bPaused", "paused", Map::Bool},
        {"m_bForcedPaused", "forced_paused", Map::Bool},
        {"m_NumGuests", "guests", Map::Int},
        {"m_NumLitter", "litter", Map::Int},
        {"m_NumBuildings", "buildings", Map::Int},
        {"m_fScenicVal", "scenic_value", Map::Float},
        {"m_fWaterContam", "water_contamination", Map::Float},
        {"m_iObjectiveStatus[]", "objective_status", Map::UInts},
        {"m_TotalGuestArrive", "total_guest_arrivals", Map::Ints},
        {"m_TotalGuestDepart", "total_guest_departures", Map::Ints},
        {"m_TodayGuestArrive", "today_guest_arrivals", Map::Ints},
        {"m_TodayGuestDepart", "today_guest_departures", Map::Ints},
        {"m_TotalGuestUnhappyDepart", "total_unhappy_departures", Map::Ints},
        {"m_TodayGuestUnhappyDepart", "today_unhappy_departures", Map::Ints},
        {"m_fIncomeToday", "income_today", Map::Float},
        {"m_fDemolishIncomeToday", "demolition_income_today", Map::Float},
        {"m_fFineIncomeToday", "fine_income_today", Map::Float},
        {"m_fAccomIncomeToday", "accommodation_income_today", Map::Float},
        {"m_fAttractIncomeToday", "attraction_income_today", Map::Float},
        {"m_fRetailIncomeToday", "retail_income_today", Map::Float},
        {"m_fTotalFines", "total_fines", Map::Float},
        {"m_fCostsToday", "costs_today", Map::Float},
        {"m_fConstructCostToday", "construction_cost_today", Map::Float},
        {"m_fSceneryCostToday", "scenery_cost_today", Map::Float},
        {"m_fWagesCostToday", "wages_cost_today", Map::Float},
        {"m_fStockCostToday", "stock_cost_today", Map::Float},
        {"m_fAverageProfit", "average_profit", Map::Float},
        {"m_fAverageCost", "average_cost", Map::Float},
        {"m_fAverageIncome", "average_income", Map::Float},
        {"m_fProfitRecord", "profit_history", Map::Floats},
        {"m_fIncomeRecord", "income_history", Map::Floats},
        {"m_fCostRecord", "cost_history", Map::Floats},
        {"m_BuildingsConstructed", "buildings_constructed", Map::Int},
        {"m_DiffBuildingsConstructed", "different_buildings_constructed", Map::Int},
        {"m_BarsConstructed", "bars_constructed", Map::Int},
        {"m_BuildingsLeftToBuild", "buildings_left_to_build", Map::Int},
        {"m_fAverageTotty", "average_attractiveness", Map::Float},
        {"m_fAverageBeauty", "average_beauty", Map::Float},
        {"m_fAverageSafety", "average_safety", Map::Float},
        {"m_PartyCount", "party_count", Map::Int},
        {"m_ShagCount", "romance_count", Map::Int},
        {"m_PissedCount", "drunk_count", Map::Int},
        {"m_GuestsDrowned", "guests_drowned", Map::Int},
        {"m_GuestsEaten", "guests_eaten", Map::Int},
        {"m_UnbuiltAfterStartup", "unbuilt_after_startup", Map::Int},
        {"Worldscript state", "script_state", Map::Int},
};

// The object manager save routine. The slot counts and the id per slot follow from the objects and their order (counts checked by
// CheckOldSaveBody, the id list here), so the loader rebuilds them.
constexpr SectionField kObjectManagerFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"m_dwNumActiveObjects", nullptr, Map::Drop},
        {"m_dwNumActiveBuildingObjects", nullptr, Map::Drop},
        {"m_dwNumActiveGuestObjects", nullptr, Map::Drop},
        {"m_dwNumActiveOtherObjects", nullptr, Map::Drop},
        {"m_dwObjectIDList", nullptr, Map::Special}, // checked against the objects, then dropped
        {"m_dwNextFreeObjectID", "next_id", Map::UInt},
        {"m_bGlobalKeyline", "keyline", Map::Bool},
};

constexpr SectionField kPlayerFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"Name", "name", Map::WideText},
        {"Cash", "cash", Map::Float},
};

// ZEngine (the map view save routine): the camera's top-left corner in map pixels, its float position and velocity, the scripted
// camera move and the queue of map tiles waiting to be re-burned.
constexpr SectionField kMapViewFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"m_MapX", nullptr, Map::Special}, // with m_MapY: camera
        {"m_MapY", nullptr, Map::Special},
        {"m_MapPosition.x", nullptr, Map::Special}, // position, velocity: pairs
        {"m_MapPosition.y", nullptr, Map::Special},
        {"m_MapVelocity.x", nullptr, Map::Special},
        {"m_MapVelocity.y", nullptr, Map::Special},
        {"burnqueue size", nullptr, Map::Drop},
        {"burnqueue entry[]", "burn_queue", Map::UInts},
        {"m_bMapLocked", "locked", Map::Bool},
        {"m_bMovingMap", "moving", Map::Bool},
        {"m_fMoveMapDuration", "move_duration", Map::Float},
        {"m_fMoveMapStartTime", "move_start_time", Map::Float},
        {"m_iMoveMapStartX", nullptr, Map::Special}, // move_start, move_destination: pairs
        {"m_iMoveMapStartY", nullptr, Map::Special},
        {"m_iMoveMapDestX", nullptr, Map::Special},
        {"m_iMoveMapDestY", nullptr, Map::Special},
        {"m_fLastUnBurnTime", "last_unburn_time", Map::Float},
        {"m_GuestObjectsDrawn", "guests_drawn", Map::Int},
};

// A label with its indices replaced by "[]" ("BuildingJob[3].RepairID" -> "BuildingJob[].RepairID") and its indices.
std::string GenericLabel(std::string_view label, std::vector<uint32_t>* indices = nullptr)
{
    std::string generic;
    for (size_t i = 0; i < label.size(); ++i) {
        generic += label[i];
        if (label[i] != '[') continue;
        size_t end = i + 1;
        uint32_t value = 0;
        while (end < label.size() && label[end] >= '0' && label[end] <= '9') value = value * 10 + static_cast<uint32_t>(label[end++] - '0');
        if (end == i + 1 || end >= label.size() || label[end] != ']') continue;
        if (indices) indices->push_back(value);
        i = end - 1;
    }
    return generic;
}

struct FoundField {
    const SectionField* spec = nullptr;
    std::string key;
};
FoundField FindField(std::span<const SectionField> table, std::string_view label, uint32_t occurrence = 0)
{
    const std::string generic = GenericLabel(label);
    for (const SectionField& f : table) {
        if (f.map == Map::Group) {
            const std::string_view prefix = f.label;
            if (!generic.starts_with(prefix)) continue;
            // The rest of the label after the prefix: walk both, skipping the label's index digits where the prefix has "[]".
            size_t cut = 0;
            for (size_t i = 0; i < prefix.size(); ++i, ++cut)
                if (prefix[i] == '[')
                    while (cut + 1 < label.size() && label[cut + 1] >= '0' && label[cut + 1] <= '9') ++cut;
            FoundField inner = FindField(f.group, label.substr(cut), occurrence);
            if (inner.spec && inner.spec->name && *f.name) inner.key = std::string(f.name) + "." + inner.key;
            return inner;
        }
        if ((label == f.label || generic == f.label) && occurrence-- == 0) return {&f, f.name ? f.name : ""};
    }
    return {};
}

// repair_manager::Save (the "RepairManager" block): the building jobs (inspections, breakdowns), the scenery jobs, the jobs a mechanic has taken.
// `object` is the object id of the building or scenery; `type` the job kind (0 inspection, 2 breakdown, others scenery).
constexpr SectionField kRepairFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"BuildingJob list size", "building_jobs", Map::List},
        {"BuildingJob[].RepairID", "building_jobs[].object", Map::Int},
        {"BuildingJob[].iCalls", "building_jobs[].calls", Map::UInt},
        {"BuildingJob[].fDistance", "building_jobs[].distance", Map::Float},
        {"BuildingJob[].fTimeCalled", "building_jobs[].time_called", Map::Float},
        {"BuildingJob[].bUrgent", "building_jobs[].urgent", Map::Bool},
        {"BuildingJob[].Type", "building_jobs[].type", Map::State, {}, formats::SaveStateSet::RepairJob},
        {"SceneryJob list size", "scenery_jobs", Map::List},
        {"SceneryJob[].RepairID", "scenery_jobs[].object", Map::Int},
        {"SceneryJob[].iCalls", "scenery_jobs[].calls", Map::UInt},
        {"SceneryJob[].fDistance", "scenery_jobs[].distance", Map::Float},
        {"SceneryJob[].fTimeCalled", "scenery_jobs[].time_called", Map::Float},
        {"SceneryJob[].bUrgent", "scenery_jobs[].urgent", Map::Bool},
        {"SceneryJob[].Type", "scenery_jobs[].type", Map::State, {}, formats::SaveStateSet::RepairJob},
        {"ActiveJob list size", "active_jobs", Map::List},
        {"ActiveJob[].RepairID", "active_jobs[].object", Map::Int},
        {"ActiveJob[].bRepairing", "active_jobs[].repairing", Map::Bool},
        {"ActiveJob[].bUrgent", "active_jobs[].urgent", Map::Bool},
        {"ActiveJob[].Type", "active_jobs[].type", Map::State, {}, formats::SaveStateSet::RepairJob},
        {"m_BreakdownCount", "breakdowns", Map::Int},
        {"m_VandalismCount", "vandalisms", Map::Int},
        {"m_ActiveCount", "active_count", Map::Int},
};

// cleaning_manager::Save (the "CleaningManager" block): the waiting jobs and the jobs a cleaner has taken; `object` is the object id to clean.
constexpr SectionField kCleaningFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"Job list size", "jobs", Map::List},
        {"Job[].CleaningID", "jobs[].object", Map::Int},
        {"Job[].iCalls", "jobs[].calls", Map::UInt},
        {"Job[].fTimeCalled", "jobs[].time_called", Map::Float},
        {"Job[].bUrgent", "jobs[].urgent", Map::Bool},
        {"ActiveJob list size", "active_jobs", Map::List},
        {"ActiveJob[].CleaningID", "active_jobs[].object", Map::Int},
        {"ActiveJob[].bCleaning", "active_jobs[].cleaning", Map::Bool},
        {"ActiveJob[].bUrgent", "active_jobs[].urgent", Map::Bool},
        {"m_CleaningCount", "cleanings", Map::Int},
        {"m_ActiveCount", "active_count", Map::Int},
};

// The event manager save routine (no class record): the disaster schedule's cursor and size and the running events by schedule index.
constexpr SectionField kEventFields[] = {
        {"m_dwNextEvent", "next_event", Map::Int},
        {"m_dwNumEvents", "event_count", Map::Int},
        {"m_dwNumCurEvents", "current_event_count", Map::Int},
        {"m_CurEvents.size()", "current_events", Map::List},
        {"EventID[]", "current_events[]", Map::Int},
        {"m_dwDayOffset", "day_offset", Map::Int},
        {"m_dwCurDayOffset", "current_day_offset", Map::Int},
};

// The weather save routine (no class record). `desire_modifiers` is indexed like a guest's 26 desire slots; `state` and
// `previous_state` are weather kinds (0..5, heat wave to thunderstorm); `strike_building` is an object id.
constexpr SectionField kWeatherFields[] = {
        {"m_LightIndex", "light_index", Map::UInt},
        {"m_LightPrevIndex", "light_previous_index", Map::UInt},
        {"m_WeatherTime", "time", Map::Float},
        {"m_DesireModifiers", "desire_modifiers", Map::Floats},
        {"m_dwNextEvent", "next_event", Map::Int},
        {"m_dwNumEvents", "event_count", Map::Int},
        {"m_bLightFlash", "light_flash", Map::Bool},
        {"m_dwLightCol", "light_colour", Map::Int},
        {"m_fFlashTime", "flash_time", Map::Float},
        {"m_bManual", "manual", Map::Bool},
        {"m_ManualCol", "manual_colour", Map::UInt},
        {"m_fLightningStrikeTime", "strike_time", Map::Float},
        {"m_dwStrikeLevel", "strike_level", Map::Int},
        {"m_dwStrikeBuilding", "strike_building", Map::Int},
        {"m_bStrikeBuilding", "striking_building", Map::Bool},
        {"m_rStrikeArea.top", "strike_area.top", Map::UInt},
        {"m_rStrikeArea.bottom", "strike_area.bottom", Map::UInt},
        {"m_rStrikeArea.left", "strike_area.left", Map::UInt},
        {"m_rStrikeArea.right", "strike_area.right", Map::UInt},
        {"m_StrikePoint.x", "strike_point.x", Map::Float},
        {"m_StrikePoint.y", "strike_point.y", Map::Float},
        {"m_StrikePoint.z", "strike_point.z", Map::Float},
        {"m_bStrikes", "strikes", Map::Bool},
        {"m_LightningLightmap", "lightning_lightmap", Map::Bool},
        {"PrevState", "previous_state", Map::State, {}, formats::SaveStateSet::Weather},
        {"CurState", "state", Map::State, {}, formats::SaveStateSet::Weather},
        {"m_LastCol", "last_colour", Map::UInt},
        {"m_dwDayOffset", "day_offset", Map::Int},
        {"m_dwCurDayOffset", "current_day_offset", Map::Int},
};

// The ferry manager save routine: the boat schedule, its cursor (the index of the next boat, -1 past the end), the pending
// performance boats and the departure statistics per guest type slot. A boat's `guest_info` is its arrival mode (0
// random count, 1 fixed count, 2 per keyword, 3 performance); per keyword slot `guest_keywords` names a scenario guest
// type (content key, "" unused), `guest_counts` its count and `guest_random` whether the count is a random bound.
// `time_flags` is the schedule mode (0..3, game/ferry/ferry_logic.h).
constexpr SectionField kFerryFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"List size", "boats", Map::List},
        {"Boat[].GuestInfo", "boats[].guest_info", Map::Int},
        {"Boat[].GuestType", "boats[].guest_random", Map::Ints},
        {"Boat[].GuestKeyword[]", "boats[].guest_keywords[]", Map::Name},
        {"Boat[].GuestKeywordNumbers", "boats[].guest_counts", Map::Ints},
        {"Boat[].NumGuests", "boats[].guests", Map::Int},
        {"Boat[].Time", "boats[].time", Map::Float},
        {"Boat[].Day", "boats[].day", Map::Int},
        {"Boat[].TimeFlags", "boats[].time_flags", Map::Int},
        {"Current It", "next_boat", Map::Int},
        {"Now list size", "performances", Map::List},
        {"NowBoat[].GuestInfo", "performances[].guest_info", Map::Int},
        {"NowBoat[].GuestType", "performances[].guest_random", Map::Ints},
        {"NowBoat[].GuestKeyword[]", "performances[].guest_keywords[]", Map::Name},
        {"NowBoat[].GuestKeywordNumbers", "performances[].guest_counts", Map::Ints},
        {"NowBoat[].NumGuests", "performances[].guests", Map::Int},
        {"NowBoat[].Time", "performances[].time", Map::Float},
        {"NowBoat[].Day", "performances[].day", Map::Int},
        {"NowBoat[].TimeFlags", "performances[].time_flags", Map::Int},
        {"m_GuestsLeft.fAvgHapp", "departures.average_happiness", Map::Floats},
        {"m_GuestsLeft.iHappyLeavers", "departures.happy", Map::UInt},
        {"m_GuestsLeft.iUnhappyLeavers", "departures.unhappy", Map::UInt},
};

constexpr SectionField kFeedbackFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"List size", "pending", Map::List},
        {"Pending[].ID", "pending[].object", Map::Int},
        {"Pending[].pwText set", "pending[].wide", Map::Bool},
        {"Pending[].pwText", "pending[].text", Map::WideText},
        {"Pending[].pText", "pending[].text", Map::Text},
        {"Buffer size", "history", Map::List},
        {"History[].ID", "history[].object", Map::Int},
        {"History[].day", "history[].day", Map::Int},
        {"History[].time", "history[].hour", Map::Float},
        {"History[].pwText set", "history[].wide", Map::Bool},
        {"History[].pwText", "history[].text", Map::WideText},
        {"History[].pText", "history[].text", Map::Text},
        {"m_CleanerMessages", "cleaner_messages", Map::Int},
        {"m_LifeGuardMessages", "lifeguard_messages", Map::Int},
        {"m_SecurityGuardMessages", "security_guard_messages", Map::Int},
        {"m_MechanicMessages", "mechanic_messages", Map::Int},
        {"m_RepMessages", "rep_messages", Map::Int},
        {"m_BuilderMessages", "builder_messages", Map::Int},
        {"m_StaffMessages", "staff_messages", Map::Int},
        {"m_NumOfSuppressedMessages", "suppressed_count", Map::Int},
        {"m_SuppressWorldMess", "suppress_world", Map::Bool},
        {"m_SuppressObjectMess", "suppress_objects", Map::Bool},
        {"m_SuppressedMessages", "suppressed", Map::Ints},
        {"m_fLastCheckedMissedDesires", "last_missed_desires_check", Map::Float},
};

// The message handler save routine: the delayed messages. Sender and receiver are object ids or the engine's fixed receivers, so
// they stay numbers; `type` is the message type (messages::MT_*), `data`/`extra` its payload (message_state.h), `time` the
// delivery time in game seconds.
constexpr SectionField kMessageFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"Delayed list size", "delayed", Map::List},
        {"DelayedMessage[].dwSender", "delayed[].sender", Map::Int},
        {"DelayedMessage[].dwReciever", "delayed[].receiver", Map::Int},
        {"DelayedMessage[].dwType", "delayed[].type", Map::Int},
        {"DelayedMessage[].dwFlags", "delayed[].flags", Map::Int},
        {"DelayedMessage[].dwData", "delayed[].data", Map::Int},
        {"DelayedMessage[].fData", "delayed[].float_data", Map::Float},
        {"DelayedMessage[].fTime", "delayed[].time", Map::Float},
        {"DelayedMessage[].member@0x14", "delayed[].extra", Map::Int},
};

// The construction manager save routine: the jobs by building object id with their hoardings (tile and facing), times, progress and
// the ten builder work points, then the builders (staff object id, status, location state, job building id). The second
// m_fConstDone/m_fDemolishDone pair is the progress and the two m_BuildingReady flags are started/finished
// (construction::Job). The builder count is written as is and at most 50 entries follow; a larger count fails.
constexpr SectionField kConstructionFields[] = {
        {"sizeof", nullptr, Map::Drop},
        {"List size", "jobs", Map::List},
        {"Job[].BuildID", "jobs[].building", Map::Int},
        {"Job[].BuildersOnJob", "jobs[].builders", Map::Int},
        {"Job[].m_NumHoardings", "jobs[].hoardings", Map::List},
        {"Job[].Hoarding[].Direct", "jobs[].hoardings[].dir", Map::Int},
        {"Job[].Hoarding[].x", "jobs[].hoardings[].x", Map::Int},
        {"Job[].Hoarding[].y", "jobs[].hoardings[].y", Map::Int},
        {"Job[].Hoarding[].member@0xc", "jobs[].hoardings[].extra", Map::Int},
        {"Job[].m_fConstStart", "jobs[].construction_start", Map::Float},
        {"Job[].m_fDemolishStart", "jobs[].demolition_start", Map::Float},
        {"Job[].m_fConstDone", "jobs[].construction_done", Map::Float},
        {"Job[].m_fDemolDone", "jobs[].demolition_done", Map::Float},
        {"Job[].m_fConstDone", "jobs[].construction_progress", Map::Float},
        {"Job[].m_fDemolishDone", "jobs[].demolition_progress", Map::Float},
        {"Job[].m_bUnderConstruction", "jobs[].under_construction", Map::Bool},
        {"Job[].m_bUnderDemolishion", "jobs[].under_demolition", Map::Bool},
        {"Job[].m_BuildingReady", "jobs[].started", Map::Bool},
        {"Job[].m_BuildingReady", "jobs[].finished", Map::Bool},
        {"Job[].BuilderConstPoint[].X", "jobs[].work_points[].x", Map::UInt},
        {"Job[].BuilderConstPoint[].Y", "jobs[].work_points[].y", Map::UInt},
        {"Job[].BuilderConstPoint[].Dir", "jobs[].work_points[].dir", Map::Int},
        {"m_ConstructionCount", "constructions", Map::Int},
        {"m_NumberOfBuilders", "builders", Map::Special}, // the list's size record, refused above 50
        {"Builder[].ID", "builders[].staff", Map::Int},
        {"Builder[].Status", "builders[].status", Map::State, {}, formats::SaveStateSet::BuilderStatus},
        {"Builder[].Location", "builders[].location", Map::UInt},
        {"Builder[].BuildID", "builders[].building", Map::Int},
        {"Builder[].ManuallyAssigned", "builders[].manual", Map::Bool},
};

constexpr std::pair<std::string_view, std::span<const SectionField>> kSectionTables[] = {
        {"World", kWorldFields},
        {"ObjectManager", kObjectManagerFields},
        {"Player", kPlayerFields},
        {"ZEngine", kMapViewFields},
        {"RepairManager", kRepairFields},
        {"CleaningManager", kCleaningFields},
        {"EventManager", kEventFields},
        {"Weather", kWeatherFields},
        {"FerryManager", kFerryFields},
        {"FeedbackManager", kFeedbackFields},
        {"MessageHandler", kMessageFields},
        {"ConstructionManager", kConstructionFields},
};

// ---- Object state (converter/save-conversion.md, "Object state") ----
// An object's records are its class fields, then the shared Prop or Mover layer, then Entity with
// its sprite. Identity records (name, id, position) are the object's `type`, `id` and `pos`, so they are dropped here.

// The sprite save routine: animation state; `dir` is the facing (a building's rotation is the object's `dir` instead).
constexpr SectionField kSpriteFields[] = {
        {"m_fStartTime", "start_time", Map::Float},
        {"m_eAnimType", "anim_type", Map::UInt},
        {"m_iFPS", "fps", Map::UInt},
        {"m_iCurrAnim", "anim", Map::UInt},
        {"m_iCurrFrame", "frame", Map::UInt},
        {"m_iCurrDir", "dir", Map::UInt},
        {"m_iCurrPal", "palette", Map::UInt},
        {"m_bLit", "lit", Map::Bool},
        {"m_bKeyline", "keyline", Map::Bool},
        {"m_bTriggeredSound", "triggered_sound", Map::Bool},
};

// The base object save routine. `sprite` is the sprite's name verbatim; m_type repeats the object type byte.
constexpr SectionField kBaseFields[] = {
        {"m_bWaitingForDeath", "base.waiting_for_death", Map::Bool},
        {"m_bRequiresAttention", "base.requires_attention", Map::Bool},
        {"m_Name", nullptr, Map::Drop},
        {"m_dwID", nullptr, Map::Drop},
        {"m_bType", nullptr, Map::Special}, // checked against the object type byte
        {"m_bOwner", "base.owner", Map::UInt},
        {"m_WorldXYZ.x", nullptr, Map::Drop},
        {"m_WorldXYZ.y", nullptr, Map::Drop},
        {"m_WorldXYZ.z", nullptr, Map::Drop}, // placement keeps all three coordinates
        {"m_dwState", "base.state", Map::State, {}, formats::SaveStateSet::ObjectState},
        {"m_bVisible", "base.visible", Map::Bool},
        {"m_XDrawOffset", "base.draw_offset_x", Map::UInt},
        {"m_YDrawOffset", "base.draw_offset_y", Map::UInt},
        {"Sprite name", "base.sprite", Map::Name},
        {"sprite.", "sprite", Map::Group, kSpriteFields},
        {"m_bDebugBreak", "base.debug_break", Map::Bool},
        {"m_SelectLevel", "base.select_level", Map::UInt},
};

constexpr SectionField kStaticFields[] = {
        {"m_bGridW", "static.grid_w", Map::UInt},
        {"m_bGridH", "static.grid_h", Map::UInt},
        {"m_bCleaning", "static.cleaner_claimed", Map::Bool},
        {"m_bRepairing", "static.mechanic_claimed", Map::Bool},
        {"m_bCalledCleaning", "static.called_cleaning", Map::Bool},
        {"m_bCalledRepairing", "static.called_repairing", Map::Bool},
        {"m_bCalledCleaning", "static.urgent_cleaning", Map::Bool},
        {"m_bCalledRepairing", "static.urgent_repairing", Map::Bool},
        {"", "", Map::Group, kBaseFields},
};

// The path data save routine: the steps as tile offsets, the end tiles and the cursor.
constexpr SectionField kPathFields[] = {
        {"m_fTimeStamp", "time_stamp", Map::Float},
        {"m_iRefCount", "ref_count", Map::UInt},
        {"m_iNumSteps", "steps", Map::List},
        {"m_pDirections[].x", "steps[].x", Map::UInt},
        {"m_pDirections[].y", "steps[].y", Map::UInt},
        {"m_Start.x", "start.x", Map::UInt},
        {"m_Start.y", "start.y", Map::UInt},
        {"m_Stop.x", "stop.x", Map::UInt},
        {"m_Stop.y", "stop.y", Map::UInt},
        {"m_ePathType", "type", Map::State, {}, formats::SaveStateSet::RouteType},
        {"m_bValid", "valid", Map::Bool},
        {"m_bReverse", "reverse", Map::Bool},
        {"m_iCurrStep", "current_step", Map::UInt},
};

// The moving object save routine, then Entity. `path` is present while the object walks a path ("on path"). Object ids stay ids;
// `group` and `building` are left uninitialized by the constructor (mover_lifecycle.h): Objects() writes `group` as 0 and
// a `building` that names no object as -1.
// The traversal flag is a byte: nonzero means backwards, including uninitialized values in old saves.
constexpr SectionField kMovingFields[] = {
        {"m_fLastTimeRemaining", "moving.last_time_remaining", Map::Float},
        {"m_bMoving", "moving.moving", Map::Bool},
        {"m_fMass", "moving.mass", Map::Float},
        {"m_vPosition.x", "moving.position.x", Map::Float},
        {"m_vPosition.y", "moving.position.y", Map::Float},
        {"m_vVelocity.x", "moving.velocity.x", Map::Float},
        {"m_vVelocity.y", "moving.velocity.y", Map::Float},
        {"m_vSteerForce.x", "moving.steer_force.x", Map::Float},
        {"m_vSteerForce.y", "moving.steer_force.y", Map::Float},
        {"m_fMaxForce", "moving.max_force", Map::Float},
        {"m_fMaxVel", "moving.max_velocity", Map::Float},
        {"m_vTarget.x", "moving.target.x", Map::Float},
        {"m_vTarget.y", "moving.target.y", Map::Float},
        {"m_DestXYZ.x", "moving.destination.x", Map::Float},
        {"m_DestXYZ.y", "moving.destination.y", Map::Float},
        {"m_DestXYZ.z", "moving.destination.z", Map::Float},
        {"m_DestObjectID", "moving.destination_object", Map::Int},
        {"m_BuildingLeavingID", "moving.leaving_building", Map::Int},
        {"m_eBehaviourState", "moving.behaviour", Map::UInt},
        {"m_GroupID", "moving.group", Map::Int},
        {"m_bFollowObject", "moving.following", Map::Bool},
        {"m_dwFollowID", "moving.follow_object", Map::Int},
        {"m_iFollowDistance", "moving.follow_distance", Map::UInt},
        {"m_bReachedFollowDistance", "moving.reached_follow_distance", Map::Bool},
        {"on path", nullptr, Map::Drop}, // `path` present or not
        {"path.", "moving.path", Map::Group, kPathFields},
        {"m_bWalkInBuilding", "moving.walk_in_building", Map::Bool},
        {"m_dwBuildingID", "moving.building", Map::Int},
        {"m_bBuildingTraversal", "moving.building_traversal", Map::ByteFlag},
        {"m_iNumWalkPts", "moving.walk_points", Map::UInt},
        {"m_iCurrWalkPt", "moving.current_walk_point", Map::UInt},
        {"m_bEnteredTheSea", "moving.entered_sea", Map::Bool},
        {"m_bInsideBuilding", "moving.inside_building", Map::Bool},
        {"m_bMoveToEntrance", "moving.move_to_entrance", Map::Bool},
        {"m_bControlDirection", "moving.control_direction", Map::Bool},
        {"m_bVehiclePath", "moving.vehicle_path", Map::Bool},
        {"m_iPosOffset", "moving.position_offset", Map::UInt},
        {"m_iLastDir", "moving.last_dir", Map::UInt},
        {"m_fAnimTimeDiv2", "moving.anim_half_time", Map::Float},
        {"m_bWaitingForMoveImHere", "moving.waiting_for_arrival", Map::Bool},
        {"m_fMessageReceived", "moving.message_received", Map::Float},
        {"m_eTurnType", "moving.turn_type", Map::UInt},
        {"m_fTargetTolerance", "moving.target_tolerance", Map::Float},
        {"", "", Map::Group, kBaseFields},
};

// The records the object manager save routine writes before each object: the type byte (the object's `kind`) and the name the load
// creates the object by, which for map props often differs from m_Name (`LOWLIGHT02` for `lowlight`).
constexpr SectionField kObjectHeaderFields[] = {
        {"Object type", nullptr, Map::Drop},
        {"Object Name", nullptr, Map::Special}, // `created_as` when it differs from `type`
};

// The scenery object save routine. `attached_guests` are guest object ids (-1 none); `owner_building` the building that placed it,
// left uninitialized for map props: Objects() writes a value that names no object as -1.
constexpr SectionField kSceneryFields[] = {
        {"m_AttachedGuest", "scenery.attached_guests", Map::Ints},
        {"m_NumItems", "scenery.items", Map::Int},
        {"m_NumAttachedGuests", "scenery.attached_count", Map::Int},
        {"m_MaxNumAttachedGuests", "scenery.max_attached", Map::Int},
        {"m_FreeSeat", "scenery.free_seat", Map::Int},
        {"m_bVandalised", "scenery.vandalised", Map::Bool},
        {"m_RepairState", "scenery.repair_state", Map::UInt},
        {"m_bFlashLight", "scenery.flash_light", Map::Bool},
        {"m_PreVandlismAnim", "scenery.pre_vandalism_anim", Map::UInt},
        {"m_ReburnRect.left", "scenery.reburn_rect.left", Map::UInt},
        {"m_ReburnRect.top", "scenery.reburn_rect.top", Map::UInt},
        {"m_ReburnRect.right", "scenery.reburn_rect.right", Map::UInt},
        {"m_ReburnRect.bottom", "scenery.reburn_rect.bottom", Map::UInt},
        {"m_ScrXYRect.left", "scenery.screen_rect.left", Map::UInt},
        {"m_ScrXYRect.top", "scenery.screen_rect.top", Map::UInt},
        {"m_ScrXYRect.right", "scenery.screen_rect.right", Map::UInt},
        {"m_ScrXYRect.bottom", "scenery.screen_rect.bottom", Map::UInt},
        {"m_fLightsOn", "scenery.lights_on", Map::Float},
        {"m_fLightsOff", "scenery.lights_off", Map::Float},
        {"m_bSoundPlaying", "scenery.sound_playing", Map::Bool},
        {"m_OwnerBuildID", "scenery.owner_building", Map::Int},
        {"m_bCanSelect", "scenery.can_select", Map::Bool},
};

// The staff object save routine, written by every staff class after its own fields. `home` is the home building and
// `goto_object` the directed target (object ids); the three thoughts are the shown texts.
constexpr SectionField kStaffFields[] = {
        {"m_HomeID", "staff.home", Map::Int},
        {"m_Wage", "staff.wage", Map::Int},
        {"m_walkpoint", "staff.walk_point", Map::Int},
        {"m_happiness", "staff.happiness", Map::Float},
        {"m_SalaryExpectation", "staff.salary_expectation", Map::Float},
        {"m_StartTime", "staff.start_time", Map::Int},
        {"m_EndTime", "staff.end_time", Map::Int},
        {"m_hoursWorkedInRow", "staff.hours_in_row", Map::UInt},
        {"m_OverWorkReduct", "staff.overwork_reduction", Map::UInt},
        {"m_IdleTimePerHour", "staff.idle_time_per_hour", Map::Int},
        {"m_HiredOn", "staff.hired_on", Map::Int},
        {"m_DirectStaff", "staff.directed", Map::Bool},
        {"m_GotoObject", "staff.goto_object", Map::Int},
        {"m_LastAction", "staff.last_action", Map::Int},
        {"m_DirectPos.x", "staff.direct_pos.x", Map::Float},
        {"m_DirectPos.y", "staff.direct_pos.y", Map::Float},
        {"m_bFoundDirecting", "staff.found_directing", Map::Bool},
        {"m_NumFeedBackMesslog", "staff.feedback_messages", Map::Int},
        {"m_bCantDoJob", "staff.cant_do_job", Map::Bool},
        {"m_WanderPos.x", "staff.wander_pos.x", Map::Float},
        {"m_WanderPos.y", "staff.wander_pos.y", Map::Float},
        {"m_WanderPos.z", "staff.wander_pos.z", Map::Float},
        {"m_DebugInfo", "staff.debug_info", Map::Int},
        {"m_Thoughts[]", "staff.thoughts[]", Map::WideText},
        {"m_bGotTheSack", "staff.sacked", Map::Bool},
        {"m_bEndedShift", "staff.ended_shift", Map::Bool},
        {"m_bGonePastShift", "staff.gone_past_shift", Map::Bool},
};

// A sprite attached by the script: its name verbatim, then the sprite state.
constexpr SectionField kAttachedSpriteFields[] = {
        {"Sprite name", "sprite", Map::Name},
        {"", "", Map::Group, kSpriteFields},
};

// The service staff save routine (and the restaurant waiter), then the shared staff fields.
constexpr SectionField kServiceStaffFields[] = {
        {"m_dwActionState", "service.action_state", Map::Int},
        {"", "", Map::Group, kStaffFields},
};

// The life guard save routine: `saving` the guest being rescued (object id), `beach_area` the position in the beach area chain,
// `influence_area` the swimming area it keeps (-1 none).
constexpr SectionField kLifeguardFields[] = {
        {"m_saving", "lifeguard.saving", Map::Bool},
        {"m_RescueThenDeath", "lifeguard.rescue_then_death", Map::Bool},
        {"m_saving_id", "lifeguard.saving_guest", Map::Int},
        {"m_BeachArea", "lifeguard.beach_area", Map::Int},
        {"m_ExitPoint.x", "lifeguard.exit_point.x", Map::Float},
        {"m_ExitPoint.y", "lifeguard.exit_point.y", Map::Float},
        {"m_ExitPoint.z", "lifeguard.exit_point.z", Map::Float},
        {"m_ReturnAnimation", "lifeguard.return_animation", Map::Text},
        {"m_CurrAnimation", "lifeguard.current_animation", Map::Text},
        {"m_LeaveState", "lifeguard.leave_state", Map::Int},
        {"m_StopAction", "lifeguard.stop_action", Map::Bool},
        {"m_ActionState", "lifeguard.action_state", Map::Int},
        {"attached sprite", nullptr, Map::Drop}, // `attached` present or not
        {"attached.", "lifeguard.attached", Map::Group, kAttachedSpriteFields},
        {"m_AttachedOffsetX", "lifeguard.attached_offset_x", Map::Int},
        {"m_AttachedOffsetY", "lifeguard.attached_offset_y", Map::Int},
        {"m_FeedbackStatus", "lifeguard.feedback_status", Map::Int},
        {"m_RequiredFeedback", "lifeguard.required_feedback", Map::Int},
        {"m_CurrentFeedback", "lifeguard.current_feedback", Map::Int},
        {"m_NumSaves", "lifeguard.saves", Map::Int},
        {"m_NumDrowned", "lifeguard.drowned", Map::Int},
        {"m_GuestDrowned", "lifeguard.guest_drowned", Map::Bool},
        {"m_timesCalledOut", "lifeguard.times_called_out", Map::Int},
        {"m_dwTimesPathBlocked", "lifeguard.times_path_blocked", Map::Int},
        {"m_InfluenceArea", "lifeguard.influence_area", Map::Int},
        {"m_bInMyArea", "lifeguard.in_my_area", Map::Bool},
        {"m_bInSea", "lifeguard.in_sea", Map::Bool},
        {"m_bEnteringSea", "lifeguard.entering_sea", Map::Bool},
        {"m_AtHotel", "lifeguard.at_hotel", Map::Bool},
        {"m_DrawGuest", "lifeguard.draw_guest", Map::Bool},
        {"", "", Map::Group, kStaffFields},
};

// The mechanic save routine; `class_count` is the class-wide mechanic count every mechanic writes.
constexpr SectionField kMechanicFields[] = {
        {"m_sMechanicCount", "mechanic.class_count", Map::Int},
        {"vandalism", "mechanic.vandalism", Map::Bool},
        {"breakdown", "mechanic.breakdown", Map::Bool},
        {"watering", "mechanic.watering", Map::Bool},
        {"m_bFoundRepair", "mechanic.found_repair", Map::Bool},
        {"m_bFoundBetterJob", "mechanic.found_better_job", Map::Bool},
        {"m_fLastTaskTime", "mechanic.last_task_time", Map::Float},
        {"m_FeedbackStatus", "mechanic.feedback_status", Map::Int},
        {"m_RequiredFeedback", "mechanic.required_feedback", Map::Int},
        {"m_CurrentFeedback", "mechanic.current_feedback", Map::Int},
        {"m_dwRepairType", "mechanic.repair_type", Map::Int},
        {"m_WateringPos.x", "mechanic.watering_pos.x", Map::Float},
        {"m_WateringPos.y", "mechanic.watering_pos.y", Map::Float},
        {"m_WateringPos.z", "mechanic.watering_pos.z", Map::Float},
        {"m_dwInspectionsToday", "mechanic.inspections_today", Map::Int},
        {"m_dwRepairsToday", "mechanic.repairs_today", Map::Int},
        {"m_dwVandalsToday", "mechanic.vandals_today", Map::Int},
        {"m_dwWateredToday", "mechanic.watered_today", Map::Int},
        {"m_ActionState", "mechanic.action_state", Map::Int},
        {"", "", Map::Group, kStaffFields},
};

// The rep save routine: `talking_guest` the guest being talked to (object id); `counters` seven rows of five counters.
constexpr SectionField kRepFields[] = {
        {"m_sRepCount", "rep.class_count", Map::Int},
        {"m_talking_id", "rep.talking_guest", Map::Int},
        {"counters[]", "rep.counters[]", Map::Ints},
        {"m_ReturnAnimation", "rep.return_animation", Map::Text},
        {"m_CurrAnimation", "rep.current_animation", Map::Text},
        {"m_LeaveState", "rep.leave_state", Map::Int},
        {"m_StopAction", "rep.stop_action", Map::Bool},
        {"m_ActionState", "rep.action_state", Map::Int},
        {"attached sprite", nullptr, Map::Drop}, // `attached` present or not
        {"attached.", "rep.attached", Map::Group, kAttachedSpriteFields},
        {"m_AttachedOffsetX", "rep.attached_offset_x", Map::Int},
        {"m_AttachedOffsetY", "rep.attached_offset_y", Map::Int},
        {"m_FeedbackStatus", "rep.feedback_status", Map::Int},
        {"m_RequiredFeedback", "rep.required_feedback", Map::Int},
        {"m_CurrentFeedback", "rep.current_feedback", Map::Int},
        {"m_GuestsTalkedToToday", "rep.guests_talked_to_today", Map::Int},
        {"m_MinTalkTime", "rep.min_talk_time", Map::Int},
        {"m_MaxTalkTime", "rep.max_talk_time", Map::Int},
        {"m_fTimeWithGuests", "rep.time_with_guests", Map::Float},
        {"m_bSentToGuest", "rep.sent_to_guest", Map::Bool},
        {"m_GuestOffSetPosY", "rep.guest_offset_y", Map::Float},
        {"m_GuestOffSetPosX", "rep.guest_offset_x", Map::Float},
        {"m_GuestOpinionValue", "rep.guest_opinion_value", Map::Int},
        {"", "", Map::Group, kStaffFields},
};

// The builder save routine; `class_count` is the class-wide builder count every builder writes.
constexpr SectionField kBuilderFields[] = {
        {"m_sBuilderCount", "builder.class_count", Map::Int},
        {"m_NearestValidTileX", "builder.nearest_valid_tile_x", Map::Int},
        {"m_NearestValidTileY", "builder.nearest_valid_tile_y", Map::Int},
        {"m_NumBuildersWhistling", "builder.whistling", Map::Int},
        {"m_NumTimesPlayedAnimInRow", "builder.anim_repeats", Map::Int},
        {"m_fLastTaskTime", "builder.last_task_time", Map::Float},
        {"m_BuilderNumber", "builder.number", Map::Int},
        {"m_HomePoint.x", "builder.home_point.x", Map::Float},
        {"m_HomePoint.y", "builder.home_point.y", Map::Float},
        {"m_HomePoint.z", "builder.home_point.z", Map::Float},
        {"m_ActionState", "builder.action_state", Map::Int},
        {"m_Night", "builder.night", Map::Int},
        {"m_InHut", "builder.in_hut", Map::Int},
        {"m_CurrentFeedback", "builder.current_feedback", Map::Int},
        {"m_FeedbackStatus", "builder.feedback_status", Map::Int},
        {"m_RequiredFeedback", "builder.required_feedback", Map::Int},
        {"m_BuilderLocation", "builder.location", Map::Int},
        {"m_JobType", "builder.job_type", Map::Int},
        {"m_LastStartTime", "builder.last_start_time", Map::Int},
        {"m_bCheckForJobs", "builder.check_for_jobs", Map::Bool},
        {"m_WaitTillMorning", "builder.wait_till_morning", Map::Int},
        {"", "", Map::Group, kStaffFields},
};

// The cleaner save routine; `busy_toilet` the toilet being cleaned (object id, 0 none).
constexpr SectionField kCleanerFields[] = {
        {"m_sCleanerCount", "cleaner.class_count", Map::Int},
        {"m_bLitterFound", "cleaner.litter_found", Map::Bool},
        {"m_bToiletFound", "cleaner.toilet_found", Map::Bool},
        {"m_bBinFound", "cleaner.bin_found", Map::Bool},
        {"m_bCleanToilets", "cleaner.clean_toilets", Map::Bool},
        {"m_bCleanLitter", "cleaner.clean_litter", Map::Bool},
        {"m_bCleanBins", "cleaner.clean_bins", Map::Bool},
        {"m_fLastTaskTime", "cleaner.last_task_time", Map::Float},
        {"m_LitterDistance", "cleaner.litter_distance", Map::Int},
        {"m_ToiletsToday", "cleaner.toilets_today", Map::Int},
        {"m_BinsToday", "cleaner.bins_today", Map::Int},
        {"m_LitterToday", "cleaner.litter_today", Map::Int},
        {"m_BusyToilet", "cleaner.busy_toilet", Map::Int},
        {"m_FeedbackStatus", "cleaner.feedback_status", Map::Int},
        {"m_RequiredFeedback", "cleaner.required_feedback", Map::Int},
        {"m_CurrentFeedback", "cleaner.current_feedback", Map::Int},
        {"m_BinPos.x", "cleaner.bin_pos.x", Map::Float},
        {"m_BinPos.y", "cleaner.bin_pos.y", Map::Float},
        {"m_BinPos.z", "cleaner.bin_pos.z", Map::Float},
        {"m_ActionState", "cleaner.action_state", Map::Int},
        {"m_UrgentType", "cleaner.urgent_type", Map::Int},
        {"m_bUrgentClean", "cleaner.urgent_clean", Map::Bool},
        {"", "", Map::Group, kStaffFields},
};

// The security guard save routine: offender and fight partner are object ids; the second m_FineOffsetY is the fine offset's z.
constexpr SectionField kSecurityGuardFields[] = {
        {"m_sGuardCount", "security.class_count", Map::Int},
        {"m_dwOffenderID", "security.offender", Map::Int},
        {"m_dwFightWithID", "security.fight_with", Map::Int},
        {"m_fTolerance", "security.tolerance", Map::Float},
        {"m_eTolerance", "security.tolerance_level", Map::UInt},
        {"m_eSeverity", "security.severity", Map::UInt},
        {"m_eAction", "security.action", Map::UInt},
        {"m_bDoubleFine", "security.double_fine", Map::Bool},
        {"m_bMisdemeanourFound", "security.misdemeanour_found", Map::Bool},
        {"m_bShouldReprimand", "security.should_reprimand", Map::Bool},
        {"m_bShouldDetain", "security.should_detain", Map::Bool},
        {"m_NotEnoughBins", "security.not_enough_bins", Map::Bool},
        {"m_MisdemeanourPos.x", "security.misdemeanour_pos.x", Map::Float},
        {"m_MisdemeanourPos.y", "security.misdemeanour_pos.y", Map::Float},
        {"m_MisdemeanourPos.z", "security.misdemeanour_pos.z", Map::Float},
        {"m_iNumMinorFinesToday", "security.minor_fines_today", Map::UInt},
        {"m_iNumSeriousFinesToday", "security.serious_fines_today", Map::UInt},
        {"m_iNumVandals", "security.vandals", Map::UInt},
        {"m_iNumFights", "security.fights", Map::UInt},
        {"m_iNumJailed", "security.jailed", Map::UInt},
        {"m_FeedbackStatus", "security.feedback_status", Map::Int},
        {"m_RequiredFeedback", "security.required_feedback", Map::Int},
        {"m_CurrentFeedback", "security.current_feedback", Map::Int},
        {"m_ActionState", "security.action_state", Map::Int},
        {"m_fMinorFineAmount", "security.minor_fine_amount", Map::Float},
        {"m_fSeriousFineAmount", "security.serious_fine_amount", Map::Float},
        {"m_fFineTotalToday", "security.fine_total_today", Map::Float},
        {"m_FineOffsetX", "security.fine_offset.x", Map::Float},
        {"m_FineOffsetY", "security.fine_offset.y", Map::Float},
        {"m_FineOffsetY", "security.fine_offset.z", Map::Float},
        {"m_FineLevel", "security.fine_level", Map::Int},
        {"m_iNumCautioned", "security.cautioned", Map::UInt},
        {"", "", Map::Group, kStaffFields},
};

// The shark save routine: `victim` the guest being chased and `owner` the object the script set as owner (object ids; a staff member in
// the local saves).
constexpr SectionField kSharkFields[] = {
        {"m_dwAlertRange", "shark.alert_range", Map::Int},
        {"m_dwSniffRange", "shark.sniff_range", Map::Int},
        {"m_SwimCircleRect[]", "shark.swim_rect[]", Map::UInt},
        {"m_dwActionState", "shark.action_state", Map::Int},
        {"m_dwMyVictim", "shark.victim", Map::Int},
        {"m_dwGuestsEaten", "shark.guests_eaten", Map::Int},
        {"m_dwOwner", "shark.owner", Map::Int},
};

// The vehicle object save routine: `guests` the 300 passenger slots and `owner` the building (object ids, numbers: slots past the
// passengers and most boat owners hold stale values in the local saves); `vehicle_type` the
// vehicle enum (fish boat, speed boat, ferry, ...); `ride_time` may be uninitialized (kept bit for bit); the 64 way points;
// the sprites present among five (vehicle, door, three auxiliary).
constexpr SectionField kVehicleFields[] = {
        {"m_Guests", "vehicle.guests", Map::Ints},
        {"m_dwGuestsToWaitFor", "vehicle.guests_to_wait_for", Map::Int},
        {"m_dwCurrCap", "vehicle.current_capacity", Map::Int},
        {"m_fRideTime", "vehicle.ride_time", Map::Float},
        {"m_dwOwner", "vehicle.owner", Map::Int},
        {"m_DestroyPoint", "vehicle.destroy_point", Map::UInt},
        {"m_UnloadPoint", "vehicle.unload_point", Map::UInt},
        {"m_iUnloadDir", "vehicle.unload_dir", Map::UInt},
        {"m_TurnArea.x", "vehicle.turn_area.x", Map::UInt},
        {"m_TurnArea.y", "vehicle.turn_area.y", Map::UInt},
        {"m_WayPoints[].x", "vehicle.way_points[].x", Map::Float},
        {"m_WayPoints[].y", "vehicle.way_points[].y", Map::Float},
        {"m_WayPoints[].z", "vehicle.way_points[].z", Map::Float},
        {"m_iNumWayPts", "vehicle.way_point_count", Map::UInt},
        {"m_iCurrentWayPoint", "vehicle.current_way_point", Map::UInt},
        {"m_CurrentRoamPoint.x", "vehicle.roam_point.x", Map::Float},
        {"m_CurrentRoamPoint.y", "vehicle.roam_point.y", Map::Float},
        {"m_CurrentRoamPoint.z", "vehicle.roam_point.z", Map::Float},
        {"m_CurrentFishPoint.x", "vehicle.fish_point.x", Map::Float},
        {"m_CurrentFishPoint.y", "vehicle.fish_point.y", Map::Float},
        {"m_CurrentFishPoint.z", "vehicle.fish_point.z", Map::Float},
        {"m_bMoveToTurnPoint", "vehicle.move_to_turn_point", Map::Bool},
        {"m_bFishing", "vehicle.fishing", Map::Bool},
        {"m_bTurnAnim", "vehicle.turn_anim", Map::Bool},
        {"m_bTurnCW", "vehicle.turn_cw", Map::Bool},
        {"m_iNumTurns", "vehicle.turns", Map::UInt},
        {"m_fTurnAnimStartTime", "vehicle.turn_anim_start_time", Map::Float},
        {"m_fTurnAnimLength", "vehicle.turn_anim_length", Map::Float},
        {"m_iStartCircuitPoint", "vehicle.start_circuit_point", Map::UInt},
        {"m_iStopCircuitPoint", "vehicle.stop_circuit_point", Map::UInt},
        {"m_iCurrCircuitPoint", "vehicle.current_circuit_point", Map::UInt},
        {"sprites[].", "vehicle.sprites[]", Map::Group, kSpriteFields},
        {"m_eVehicleType", "vehicle.vehicle_type", Map::State, {}, formats::SaveStateSet::VehicleType},
        {"m_bWaitForGuestToBoard", "vehicle.wait_for_guest_to_board", Map::Bool},
        {"m_bPlayingDoorAnim", "vehicle.playing_door_anim", Map::Bool},
        {"m_FerryEntryPoint.x", "vehicle.ferry_entry_point.x", Map::Float},
        {"m_FerryEntryPoint.y", "vehicle.ferry_entry_point.y", Map::Float},
        {"m_FerryEntryPoint.z", "vehicle.ferry_entry_point.z", Map::Float},
        {"m_FerryExitPoint.x", "vehicle.ferry_exit_point.x", Map::Float},
        {"m_FerryExitPoint.y", "vehicle.ferry_exit_point.y", Map::Float},
        {"m_FerryExitPoint.z", "vehicle.ferry_exit_point.z", Map::Float},
        {"m_FerryEntryOffset.x", "vehicle.ferry_entry_offset.x", Map::Float},
        {"m_FerryEntryOffset.y", "vehicle.ferry_entry_offset.y", Map::Float},
        {"m_FerryExitOffset.x", "vehicle.ferry_exit_offset.x", Map::Float},
        {"m_FerryExitOffset.y", "vehicle.ferry_exit_offset.y", Map::Float},
        {"m_iFerryEntryDir", "vehicle.ferry_entry_dir", Map::UInt},
        {"m_iFerryExitDir", "vehicle.ferry_exit_dir", Map::UInt},
        {"m_bGotGrid", "vehicle.got_grid", Map::Bool},
        {"m_CurrGridXY.x", "vehicle.grid.x", Map::UInt},
        {"m_CurrGridXY.y", "vehicle.grid.y", Map::UInt},
};

// The table object save routine: one chair per seat (occupier a guest object id) with its sprite when the table has chair sprites,
// the waiter fields (`waiter` its object id, `owner` the restaurant), then a plate sprite per chair when present.
constexpr SectionField kTableFields[] = {
        {"Chair[].ChairSeated", "table.chairs[].seated", Map::Bool},
        {"Chair[].ChairOccupier", "table.chairs[].occupier", Map::Int},
        {"Chair[].ChairDir", "table.chairs[].dir", Map::UInt},
        {"Chair[].iPosition", "table.chairs[].position", Map::UInt},
        {"Chair[].bWaitingForWaiter", "table.chairs[].waiting_for_waiter", Map::Bool},
        {"Chair[].sprite.", "table.chairs[].sprite", Map::Group, kSpriteFields},
        {"m_bDrawWaiter", "table.draw_waiter", Map::Bool},
        {"m_WaiterServing", "table.waiter_serving", Map::Int},
        {"m_WaiterPos", "table.waiter_pos", Map::UInt},
        {"m_WaiterID", "table.waiter", Map::Int},
        {"m_bWaiterReady", "table.waiter_ready", Map::Bool},
        {"m_iRotation", "table.rotation", Map::UInt},
        {"m_dwOwner", "table.owner", Map::Int},
        {"m_bServeTable", "table.serve_table", Map::Bool},
        {"m_GuestsRemoved", "table.guests_removed", Map::UInt},
        {"m_GuestsAdded", "table.guests_added", Map::UInt},
        {"Plate[].sprite.", "table.plates[].sprite", Map::Group, kSpriteFields},
        {"Plate[].m_bDrawPlate", "table.plates[].draw", Map::Bool},
};

// The guest object save routine. The arrays stay engine slots (`desires` like the 26 desire slots, `person`, `opinion`, `tolerance`);
// `rides.last`, `vehicle`, `staying_at`, `best`, `current`, the fight and activity partners and `last_ids` are object ids
// (numbers: stale values occur); `desired_commodity_slot` indexes the destination building's commodities; `thoughts`
// are thought text ids; `best_type`, `worst_type` and `current_type` are catalog buildings. m_DayArrived is written twice.
constexpr SectionField kGuestFields[] = {
        {"m_Tolerance", "guest.tolerance", Map::Floats},
        {"m_person", "guest.person", Map::Floats},
        {"m_desires", "guest.desires", Map::Floats},
        {"m_opinion", "guest.opinion", Map::Floats},
        {"m_rides.last", "guest.rides.last", Map::Ints},
        {"m_rides.start", "guest.rides.start", Map::Int},
        {"m_rides.end", "guest.rides.end", Map::Int},
        {"m_invent.slot", "guest.inventory", Map::Ints},
        {"m_desired_commod", "guest.desired_commodity_slot", Map::Int},
        {"m_VehicleID", "guest.vehicle", Map::Int},
        {"m_activity", "guest.activity", Map::Bool},
        {"m_StopAction", "guest.stop_action", Map::Bool},
        {"m_walkpoints[].x", "guest.walk_points[].x", Map::Float},
        {"m_walkpoints[].y", "guest.walk_points[].y", Map::Float},
        {"m_walkpoints[].z", "guest.walk_points[].z", Map::Float},
        {"m_walkpoint", "guest.walk_point", Map::Int},
        {"m_drunk", "guest.drunk", Map::Float},
        {"m_oxygen", "guest.oxygen", Map::Int},
        {"m_cash", "guest.cash", Map::Float},
        {"m_name", "guest.name", Map::WideText},
        {"m_happiness", "guest.happiness", Map::Float},
        {"m_WalkAnimation", "guest.walk_animation", Map::Text},
        {"m_StandAnimation", "guest.stand_animation", Map::Text},
        {"m_Fidget", "guest.fidget", Map::Bool},
        {"m_CurrAnimation", "guest.current_animation", Map::Text},
        {"m_ReturnAnimation", "guest.return_animation", Map::Text},
        {"m_LeaveState", "guest.leave_state", Map::Int},
        {"attached sprite", nullptr, Map::Drop}, // `attached` present or not
        {"attached.", "guest.attached", Map::Group, kAttachedSpriteFields},
        {"m_EventID", "guest.event", Map::Int},
        {"m_ActionState", "guest.action_state", Map::Int},
        {"m_RepeatAction", "guest.repeat_action", Map::Int},
        {"m_AttachedOffsetX", "guest.attached_offset_x", Map::Int},
        {"m_AttachedOffsetY", "guest.attached_offset_y", Map::Int},
        {"m_FightPerson", "guest.fight_person", Map::Int},
        {"m_ActivityPerson", "guest.activity_person", Map::Int},
        {"m_bJustLittered", "guest.just_littered", Map::Bool},
        {"m_fTimeLittered", "guest.time_littered", Map::Float},
        {"m_StayingAt", "guest.staying_at", Map::Int},
        {"m_WakeTime", "guest.wake_time", Map::Float},
        {"m_BedTime", "guest.bed_time", Map::Float},
        {"m_NightsToStay", "guest.nights_to_stay", Map::Int},
        {"m_DayArrived", "guest.day_arrived", Map::Int},
        {"m_fTimeArrived", "guest.time_arrived", Map::Float},
        {"m_TimeToLeave", "guest.time_to_leave", Map::Float},
        {"m_LastID", "guest.last_ids", Map::Ints},
        {"m_bLastActivity", "guest.last_activity", Map::Ints},
        {"m_ArrayPos", "guest.array_pos", Map::Int},
        {"m_fSunBurnLevel", "guest.sunburn", Map::Float},
        {"m_AmountLoition", "guest.lotion", Map::Int},
        {"m_DayArrived", nullptr, Map::Special}, // the same field again: checked equal, then dropped
        {"m_AppliedLotion", "guest.applied_lotion", Map::Bool},
        {"m_BestSpotOpinion", "guest.best_spot_opinion", Map::Bool},
        {"m_Best", "guest.best", Map::Int},
        {"m_BestType", "guest.best_type", Map::Building},
        {"m_fBestScore", "guest.best_score", Map::Float},
        {"m_WorstSpotOpinion", "guest.worst_spot_opinion", Map::Bool},
        {"m_WorstType", "guest.worst_type", Map::Building},
        {"m_fWorstScore", "guest.worst_score", Map::Float},
        {"m_CurID", "guest.current", Map::Int},
        {"m_CurTypeID", "guest.current_type", Map::Building},
        {"m_fCurScore", "guest.current_score", Map::Float},
        {"m_fPriceScore", "guest.price_score", Map::Float},
        {"m_fQueueScore", "guest.queue_score", Map::Float},
        {"m_fSafetyScore", "guest.safety_score", Map::Float},
        {"m_fCleanScore", "guest.clean_score", Map::Float},
        {"m_bMissedPurchase", "guest.missed_purchase", Map::Bool},
        {"m_fFoodIll", "guest.food_ill", Map::Float},
        {"m_bImIll", "guest.ill", Map::Bool},
        {"m_bLastGood", "guest.last_good", Map::Bool},
        {"m_bLastBad", "guest.last_bad", Map::Bool},
        {"m_HasSatDown", "guest.has_sat_down", Map::Bool},
        {"m_bLeavingEarly", "guest.leaving_early", Map::Bool},
        {"m_Thoughts", "guest.thoughts", Map::Ints},
        {"m_OffenceState", "guest.offence_state", Map::UInt},
        {"m_fTimeOfOffence", "guest.time_of_offence", Map::Float},
        {"m_fLastUpdateHappened", "guest.last_update", Map::Float},
};

// The building object save routine. Staff, vehicles, tables, loungers, queue guests and event actors/guests are object ids (numbers);
// the five commodity arrays are the definition's commodity slots, as are the queue's commodities; `beer` and the
// event slots' `event` are catalog beers and events; `sprites` the animation sprites (as many as the definition names
// plus an optional extra); `events` (the show schedule) and `queue` only when the building has them.
constexpr SectionField kBuildingFields[] = {
        {"m_staff", "building.staff", Map::Ints},
        {"m_numMalestaff", "building.male_staff", Map::Int},
        {"m_numFemalestaff", "building.female_staff", Map::Int},
        {"m_NonServeStaff", "building.non_serve_staff", Map::Ints},
        {"m_NumNonServestaff", "building.non_serve_staff_count", Map::Int},
        {"m_staff_busy", "building.staff_busy", Map::Ints},
        {"m_burn", "building.burn", Map::Bool},
        {"m_Entrance", "building.entrance", Map::Bool},
        {"m_queue_point.x", "building.queue_point.x", Map::Float},
        {"m_queue_point.y", "building.queue_point.y", Map::Float},
        {"m_queue_point.z", "building.queue_point.z", Map::Float},
        {"m_serve_point.x", "building.serve_point.x", Map::Float},
        {"m_serve_point.y", "building.serve_point.y", Map::Float},
        {"m_serve_point.z", "building.serve_point.z", Map::Float},
        {"m_EntrancePoint.x", "building.entrance_point.x", Map::Float},
        {"m_EntrancePoint.y", "building.entrance_point.y", Map::Float},
        {"m_EntrancePoint.z", "building.entrance_point.z", Map::Float},
        {"m_queue_direction.x", "building.queue_direction.x", Map::Float},
        {"m_queue_direction.y", "building.queue_direction.y", Map::Float},
        {"m_queue_direction.z", "building.queue_direction.z", Map::Float},
        {"m_TopWorldPos.x", "building.top_world_pos.x", Map::Float},
        {"m_TopWorldPos.y", "building.top_world_pos.y", Map::Float},
        {"m_TopWorldPos.z", "building.top_world_pos.z", Map::Float},
        {"m_Placed", "building.placed", Map::Float},
        {"m_NumLilos", "building.lilos", Map::Int},
        {"m_NumBoards", "building.boards", Map::Int},
        {"m_NumSnorkels", "building.snorkels", Map::Int},
        {"m_CurCapacity", "building.current_capacity", Map::Int},
        {"m_NumVehicles", "building.vehicle_count", Map::Int},
        {"m_Vehicles", "building.vehicles", Map::Ints},
        {"m_bVehicleInUse", "building.vehicle_in_use", Map::Ints},
        {"m_LastMsgTime", "building.last_message_time", Map::Float},
        {"m_fLightsOn", "building.lights_on", Map::Float},
        {"m_fLightsOff", "building.lights_off", Map::Float},
        {"m_bHasBall", "building.has_ball", Map::Bool},
        {"m_BallPosition.x", "building.ball_position.x", Map::Float},
        {"m_BallPosition.y", "building.ball_position.y", Map::Float},
        {"m_BallPosition.z", "building.ball_position.z", Map::Float},
        {"m_OpenStatus", "building.open_status", Map::State, {}, formats::SaveStateSet::BuildingOpen},
        {"m_NumGuests", "building.guests", Map::Int},
        {"m_SatisfactionPercentage", "building.satisfaction", Map::Float},
        {"m_Commod_IntensityM", "building.commodity_intensity_male", Map::Floats},
        {"m_Commod_IntensityF", "building.commodity_intensity_female", Map::Floats},
        {"m_Commod_Price", "building.commodity_price", Map::Floats},
        {"m_Commod_PriceFactor", "building.commodity_price_factor", Map::Floats},
        {"m_Commod_Cost", "building.commodity_cost", Map::Floats},
        {"m_BeerStrength", "building.beer_strength", Map::Float},
        {"m_BeerType", "building.beer", Map::Beer},
        {"m_fIncomeToday", "building.income_today", Map::Float},
        {"m_fCostToday", "building.cost_today", Map::Float},
        {"m_fProfitToday", "building.profit_today", Map::Float},
        {"m_fAverageProfit", "building.average_profit", Map::Float},
        {"m_BuildingAge", "building.age", Map::Int},
        {"m_fProfitRecord", "building.profit_history", Map::Floats},
        {"m_fLastFaultDayTime", "building.last_fault_day_time", Map::Float},
        {"m_LastFaultDay", "building.last_fault_day", Map::Int},
        {"m_NumFaults", "building.faults", Map::Int},
        {"m_UserRepairInterval", "building.user_repair_interval", Map::Int},
        {"m_RepairInterval", "building.repair_interval", Map::Int},
        {"m_RepairState", "building.repair_state", Map::State, {}, formats::SaveStateSet::BuildingWear},
        {"m_bWasBroken", "building.was_broken", Map::Bool},
        {"m_LastRepairCall", "building.last_repair_call", Map::Int},
        {"m_fLastRepairDayTime", "building.last_repair_day_time", Map::Float},
        {"m_LastRepairDay", "building.last_repair_day", Map::Int},
        {"m_NumVisitsToday", "building.visits_today", Map::Int},
        {"m_VisitsSinceCleaned", "building.visits_since_cleaned", Map::Int},
        {"m_LastFailTime", "building.last_fail_time", Map::Int},
        {"m_PowerConsumption", "building.power_consumption", Map::UInt},
        {"m_NumTimesCleanedToday", "building.cleaned_today", Map::Int},
        {"m_bClosedForPower", "building.closed_for_power", Map::Bool},
        {"m_bUserClose", "building.user_closed", Map::Bool},
        {"m_bCalledForInspection", "building.called_for_inspection", Map::Bool},
        {"m_bCalledForRepair", "building.called_for_repair", Map::Bool},
        {"m_bBuiltByUser", "building.built_by_user", Map::Bool},
        {"m_iNumTables", "building.table_count", Map::UInt},
        {"m_Tables", "building.tables", Map::Ints},
        {"m_bBurnLimbo", "building.burn_limbo", Map::Bool},
        {"m_rScrExtents.left", "building.screen_extents.left", Map::UInt},
        {"m_rScrExtents.top", "building.screen_extents.top", Map::UInt},
        {"m_rScrExtents.right", "building.screen_extents.right", Map::UInt},
        {"m_rScrExtents.bottom", "building.screen_extents.bottom", Map::UInt},
        {"m_rScrRect.left", "building.screen_rect.left", Map::UInt},
        {"m_rScrRect.top", "building.screen_rect.top", Map::UInt},
        {"m_rScrRect.right", "building.screen_rect.right", Map::UInt},
        {"m_rScrRect.bottom", "building.screen_rect.bottom", Map::UInt},
        {"m_fTimeInLimbo", "building.time_in_limbo", Map::Float},
        {"m_iShadowOffset", "building.shadow_offset", Map::UInt},
        {"m_iPickOffset", "building.pick_offset", Map::UInt},
        {"m_iLightMapOffset", "building.light_map_offset", Map::UInt},
        {"m_bSoundPlaying", "building.sound_playing", Map::Bool},
        {"m_SoundID", "building.sound_id", Map::UInt},
        {"sprites[].", "building.sprites[]", Map::Group, kSpriteFields},
        {"bTempUnderConstruction", "building.under_construction", Map::Bool},
        {"bTempUnderDemolishion", "building.under_demolition", Map::Bool},
        {"Events", nullptr, Map::Drop}, // `events` present or not
        {"events.m_OwnerID", "building.events.owner", Map::Int},
        {"events.m_bEventActive", "building.events.active", Map::Bool},
        {"events.Slot[].Type", "building.events.slots[].event", Map::Event},
        {"events.Slot[].NumActors", "building.events.slots[].actor_count", Map::UInt},
        {"events.Slot[].ActorSize", "building.events.slots[].actors", Map::List},
        {"events.Slot[].ActorID[]", "building.events.slots[].actors[]", Map::Int},
        {"events.Slot[].NumGuests", "building.events.slots[].guest_count", Map::UInt},
        {"events.Slot[].GuestSize", "building.events.slots[].guests", Map::List},
        {"events.Slot[].GuestID[]", "building.events.slots[].guests[]", Map::Int},
        {"events.Slot[].Start", "building.events.slots[].start", Map::Float},
        {"events.Slot[].Duration", "building.events.slots[].duration", Map::Float},
        {"events.Slot[].Price", "building.events.slots[].price", Map::Float},
        {"m_ThoughtList.size()", "building.thoughts", Map::List},
        {"m_ThoughtList[].ID", "building.thoughts[].object", Map::Int},
        {"m_ThoughtList[].TextLength", nullptr, Map::Special}, // checked against the text, then dropped
        {"m_ThoughtList[].pText", "building.thoughts[].text", Map::WideText},
        {"m_QueueValue", nullptr, Map::Drop}, // `queue` present or not
        {"queue.m_dwOwner", "building.queue.owner", Map::Int},
        {"queue.m_dwMaxLength", "building.queue.max_length", Map::Int},
        {"queue.m_ServePoint.x", "building.queue.serve_point.x", Map::Float},
        {"queue.m_ServePoint.y", "building.queue.serve_point.y", Map::Float},
        {"queue.m_QueueDirection.x", "building.queue.direction.x", Map::Float},
        {"queue.m_QueueDirection.y", "building.queue.direction.y", Map::Float},
        {"queue.m_qlQueue.size()", "building.queue.guests", Map::List},
        {"queue.m_qlQueue[]", "building.queue.guests[]", Map::Int},
        {"queue.m_qlCommmod.size()", "building.queue.commodities", Map::List},
        {"queue.m_qlCommmod[]", "building.queue.commodities[]", Map::Int},
        {"m_iNumLoungers", "building.lounger_count", Map::UInt},
        {"m_dwLoungers", "building.loungers", Map::Ints},
        {"m_InfluenceArea", "building.influence_area", Map::Int},
        {"m_fBuildingAnimTime", "building.anim_time", Map::Float},
};

// The performers' Save (cabaret, singer, weight lifter, catwalk, wet T-shirt, aerobic; save_body.cpp ReadPerformer): the
// action state, the performer kind and the show building (`owner`, object id), the finished flag (labelled m_finish or
// m_bFinished by class), the wet T-shirt contestant (object id), the action point (not for the cabaret) and the singer's
// and weight lifter's attached sprite.
constexpr SectionField kPerformerFields[] = {
        {"m_dwActionState", "performer.action_state", Map::Int},
        {"m_iType", "performer.performer_type", Map::UInt},
        {"m_OwnerID", "performer.owner", Map::Int},
        {"m_bFinished", "performer.finished", Map::Bool},
        {"m_bFinish", "performer.finished", Map::Bool},
        {"m_Contestent", "performer.contestant", Map::Int},
        {"m_ActionPoint.x", "performer.action_point.x", Map::Float},
        {"m_ActionPoint.y", "performer.action_point.y", Map::Float},
        {"m_ActionPoint.z", "performer.action_point.z", Map::Float},
        {"m_pAttachedSprite", nullptr, Map::Drop}, // `attached` present or not
        {"attached.", "performer.attached", Map::Group, kAttachedSpriteFields},
};

// The class fields per object type byte (kObjectKinds order); type 0 is never created (the reader refuses it).
constexpr std::span<const SectionField> kClassFields[] = {
        {},
        kSceneryFields, // scenery
        kBuildingFields, // building
        kGuestFields, // guest
        kVehicleFields, // vehicle
        kTableFields, // table
        kServiceStaffFields, // service staff
        kLifeguardFields, // lifeguard
        kMechanicFields, // mechanic
        kRepFields, // rep
        kBuilderFields, // builder
        kCleanerFields, // cleaner
        kSecurityGuardFields, // security guard
        kSharkFields, // shark
        kPerformerFields, // cabaret
        kPerformerFields, // singer
        kPerformerFields, // weight lifter
        kPerformerFields, // catwalk
        kPerformerFields, // wet T-shirt
        kServiceStaffFields, // snooty staff (the restaurant waiter)
        kPerformerFields, // aerobic
};

// Object type byte (ObjectManager::CreateObject) -> obl.save kind and the catalog list its name resolves in. Null: the
// name is a key of its own for the loader to check. Guest types live in the scenario's guest set; scenery is a catalog
// entry, a prop of the scenario's map (`palm`, `shrub05`) or code-made (`litter`, `lowlight`); sharks and performers
// have no catalog entry.
struct ObjectKind {
    const char* kind;
    const char* catalog;
};
constexpr ObjectKind kObjectKinds[] = {
        {nullptr, nullptr},
        {"scenery", nullptr},
        {"building", "buildings"},
        {"guest", nullptr},
        {"vehicle", "vehicles"},
        {"table", "tables"},
        {"staff", "staff"}, // service staff
        {"staff", "staff"}, // lifeguard
        {"staff", "staff"}, // mechanic
        {"staff", "staff"}, // rep
        {"staff", "staff"}, // builder
        {"staff", "staff"}, // cleaner
        {"staff", "staff"}, // security guard
        {"animal", nullptr}, // shark
        {"performer", nullptr}, // cabaret
        {"performer", nullptr}, // singer
        {"performer", nullptr}, // weight lifter
        {"performer", nullptr}, // catwalk
        {"performer", nullptr}, // wet T-shirt
        {"staff", "staff"}, // snooty staff (the restaurant "Waiter")
        {"performer", nullptr}, // aerobic
};

static_assert(std::size(kClassFields) == std::size(kObjectKinds));

// The restaurant waiter is created by name and has no staff definition (staff_construction.h).
constexpr std::string_view kBuiltinStaff = "waiter";

constexpr const char* kDirections[] = {"n", "ne", "e", "se", "s", "sw", "w", "nw"};

// The old save's type bytes of the staff and performer classes; `class` names them in obl.save.
constexpr std::pair<std::string_view, uint8_t> kObjectClasses[] = {{"service", 6}, {"lifeguard", 7}, {"mechanic", 8}, {"rep", 9},
        {"builder", 10}, {"cleaner", 11}, {"security", 12}, {"cabaret", 14}, {"singer", 15}, {"weightlifter", 16}, {"catwalk", 17},
        {"wettshirt", 18}, {"waiter", 19}, {"aerobic", 20}};

// The `class` of a type byte; empty for the kinds that need none.
std::string_view ObjectClassOf(uint8_t type)
{
    for (const auto& [name, byte] : kObjectClasses)
        if (byte == type) return name;
    return {};
}

class DocumentWriter {
public:
    DocumentWriter(const OldSaveBody& body, const nlohmann::json& index, const SaveOrigin& origin, std::string& error)
        : m_body(body), m_index(index), m_origin(origin), m_error(error)
    {}

    bool Run(ordered_json& d)
    {
        d = ordered_json::object();
        d["format"] = "obl.save";
        d["version"] = 2;
        ordered_json header;
        if (!Header(header)) return false;
        d["header"] = std::move(header);
        ordered_json world, player, map, objects = ordered_json::array(), objectManager, view, repair, cleaning, events, weather;
        ordered_json ferry, feedback, messages, construction;
        const auto none = [](const OldField&, const SectionField&) { return true; };
        if (!World(world) || !Player(player) || !Grid(map) || !ObjectManager(objectManager) || !Objects(objects) || !View(view) ||
                !MapSection("RepairManager", kRepairFields, repair, none) ||
                !MapSection("CleaningManager", kCleaningFields, cleaning, none) ||
                !MapSection("EventManager", kEventFields, events, none) || !MapSection("Weather", kWeatherFields, weather, none) ||
                !MapSection("FerryManager", kFerryFields, ferry, none) || !MapSection("FeedbackManager", kFeedbackFields, feedback, none) ||
                !MapSection("MessageHandler", kMessageFields, messages, none) || !Construction(construction))
            return false;
        d["world"] = std::move(world);
        d["player"] = std::move(player);
        d["map"] = std::move(map);
        d["objects"] = std::move(objects);
        d["managers"] =
                ordered_json{{"objects", std::move(objectManager)}, {"repair", std::move(repair)}, {"cleaning", std::move(cleaning)},
                        {"weather", std::move(weather)}, {"events", std::move(events)}, {"ferry", std::move(ferry)},
                        {"feedback", std::move(feedback)}, {"messages", std::move(messages)}, {"construction", std::move(construction)}};
        d["view"] = std::move(view);
        return true;
    }

private:
    bool Fail(std::string message)
    {
        m_error = std::move(message);
        return false;
    }

    // C-string fields carry their terminator; the text ends at the first NUL.
    static std::string_view CText(const SaveRecord& r)
    {
        std::string_view text = r.Text();
        return text.substr(0, std::min(text.find('\0'), text.size()));
    }

    // Old narrow text is in the code page of the pack's language, read like the text tables (save-conversion.md).
    std::string Utf8(std::string_view text) const
    {
        const bool polish = m_origin.language == "pl";
        std::u16string wide;
        wide.reserve(text.size());
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            unsigned code = CodePoint(polish, byte);
            if (code == kUndefinedCodePoint) code = byte; // keeps its C1 code point, as text_convert.cpp does
            wide.push_back(static_cast<char16_t>(code));
        }
        return code_page::Utf16ToUtf8(wide);
    }

    bool Header(ordered_json& h)
    {
        const OldSaveHeader& old = m_body.header;
        char scenario[32];
        std::snprintf(scenario, sizeof(scenario), "%s%d", old.sandbox ? "sandbox" : "s", old.scenario + 1);
        h["saved_at"] = old.savedAt;
        h["scenario"] = scenario;
        h["sandbox"] = old.sandbox;
        // The game stores the scenario title's text key; `title_text` is its text in the pack's language, so the slot
        // list reads without the text table. A title that is no key, or a key the table lacks, keeps the stored text.
        const std::string_view title = CText(old.title);
        const bool isKey = !title.empty() && std::all_of(title.begin(), title.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        });
        if (isKey) h["title"] = std::string(title);
        const auto text = isKey ? m_origin.texts.find(title) : m_origin.texts.end();
        h["title_text"] = text != m_origin.texts.end() ? text->second : Utf8(title);
        h["day"] = old.day;
        h["day_time"] = SaveFloat(old.dayTimeBits);
        h["cash"] = SaveFloat(old.cashBits);
        if (old.guests < 0) return Fail("header: negative guest count " + std::to_string(old.guests));
        h["guests"] = old.guests;
        h["packs"] = ordered_json::array(
                {ordered_json{{"id", m_origin.packId}, {"content_version", m_origin.contentVersion}, {"pack_hash", m_origin.packHash}}});
        h["converted_from"] = ordered_json{{"format", "beachlife-1.6"}, {"file", m_origin.file}};
        return true;
    }

    const OldBlock* Section(std::string_view kind) const
    {
        for (const OldBlock& b : m_body.sections)
            if (b.kind == kind) return &b;
        return nullptr;
    }

    // The catalog key at `id` of a kind, or empty when the index has no such entry.
    std::string CatalogKey(const char* kind, int64_t id) const
    {
        const auto list = m_index.find(kind);
        if (list == m_index.end() || !list->is_array() || id < 0 || id >= static_cast<int64_t>(list->size())) return {};
        return (*list)[static_cast<size_t>(id)].get<std::string>();
    }
    bool CatalogHas(const char* kind, const std::string& key) const
    {
        const auto list = m_index.find(kind);
        return list != m_index.end() && list->is_array() && std::find(list->begin(), list->end(), key) != list->end();
    }

    // Maps every record of a section through its table into `out`; a Special record goes to `special(field, spec)`.
    template<class Special> bool MapSection(const char* kind, std::span<const SectionField> table, ordered_json& out, Special&& special)
    {
        const OldBlock* block = Section(kind);
        if (!block) return Fail(std::string("no ") + kind + " section");
        return MapFields(kind, block->fields, table, out, special);
    }

    // Maps `fields` (a section or a range of an object) through `table` into `out`; `where` prefixes errors.
    template<class Special>
    bool MapFields(const std::string& where, std::span<const OldField> fields, std::span<const SectionField> table, ordered_json& out,
            Special&& special)
    {
        std::map<std::string_view, uint32_t> seen; // occurrences of each label so far
        for (const OldField& f : fields) {
            const uint32_t occurrence = seen[f.name]++;
            const FoundField found = FindField(table, f.name, occurrence);
            const SectionField* spec = found.spec;
            const std::string at = where + "." + f.name;
            if (!spec && occurrence > 0) return Fail(at + ": written twice");
            if (!spec) return Fail(at + ": no obl.save field and not dropped");
            const SaveRecord& r = f.record;
            ordered_json value;
            switch (spec->map) {
            case Map::Drop:
            case Map::Group: continue;
            case Map::List: value = ordered_json::array(); break;
            case Map::Special:
                if (!special(f, *spec)) return false;
                continue;
            case Map::UInts:
                if (!out.contains(found.key)) out[found.key] = ordered_json::array();
                out[found.key].push_back(r.Bits());
                continue;
            case Map::Float: value = SaveFloat(r.Bits()); break;
            case Map::Int: value = static_cast<int32_t>(r.Bits()); break;
            case Map::UInt: value = r.Bits(); break;
            case Map::State: value = formats::SaveStateValue(spec->states, static_cast<int32_t>(r.Bits()), &m_origin.objectStates); break;
            case Map::Bool:
                if (r.Bits() > 1) return Fail(at + ": bool value " + std::to_string(r.Bits()));
                value = r.Bits() != 0;
                break;
            case Map::ByteFlag: value = r.Bits(); break;
            case Map::Ints:
            case Map::Floats:
                value = ordered_json::array();
                for (uint32_t i = 0; i < r.count; ++i)
                    value.push_back(spec->map == Map::Ints ? ordered_json(static_cast<int32_t>(r.Bits(i))) : SaveFloat(r.Bits(i)));
                break;
            case Map::WideText: value = WideUtf8(r); break;
            case Map::Text: value = Utf8(CText(r)); break;
            case Map::Name: value = Utf8(CText(r)); break;
            case Map::Building:
            case Map::Beer:
            case Map::Event: {
                const char* list = spec->map == Map::Building ? "buildings" : spec->map == Map::Beer ? "beers" : "events";
                const auto id = static_cast<int32_t>(r.Bits());
                if (id == -1) break; // null
                value = CatalogKey(list, id);
                if (value.get_ref<const std::string&>().empty())
                    return Fail(at + ": no catalog " + std::string(list) + " entry " + std::to_string(id));
                break;
            }
            }
            std::vector<uint32_t> indices;
            GenericLabel(f.name, &indices);
            if (!PlaceSaveValue(out, found.key, indices, std::move(value)))
                return Fail(at + ": written twice or list element out of order");
        }
        for (const SectionField& f : table) // an indexed label without records is an empty list
            if (f.map == Map::UInts && !out.contains(f.name)) out[f.name] = ordered_json::array();
        return true;
    }

    // UTF-16LE text of a wide-string record, up to the first NUL.
    static std::string WideUtf8(const SaveRecord& r)
    {
        std::u16string text;
        for (uint32_t i = 0; i < r.count && 2 * i + 1 < r.bytes.size(); ++i) {
            const auto c = static_cast<char16_t>(r.bytes[2 * i] | (r.bytes[2 * i + 1] << 8));
            if (!c) break;
            text.push_back(c);
        }
        return code_page::Utf16ToUtf8(text);
    }

    bool World(ordered_json& w)
    {
        int32_t validCount = -1;
        return MapSection("World", kWorldFields, w, [&](const OldField& f, const SectionField&) {
            const SaveRecord& r = f.record;
            if (f.name == "m_MapName") {
                const std::string_view path = CText(r);
                if (path.empty()) return true; // optional: no map
                const std::string id = map_pack::BizMapId(path);
                if (id.empty()) return Fail("World.m_MapName: not a map path: " + Utf8(path));
                w["map_id"] = id;
                return true;
            }
            if (f.name == "m_NumValidBuildings") {
                validCount = static_cast<int32_t>(r.Bits());
                return true;
            }
            // m_ValidBuildingIDs
            if (validCount < 0 || static_cast<uint32_t>(validCount) > r.count)
                return Fail("World.m_NumValidBuildings: " + std::to_string(validCount) + " of " + std::to_string(r.count));
            ordered_json list = ordered_json::array();
            std::set<std::string> seen;
            for (int32_t i = 0; i < validCount; ++i) {
                const auto id = static_cast<int32_t>(r.Bits(static_cast<size_t>(i)));
                const std::string key = CatalogKey("buildings", id);
                if (key.empty())
                    return Fail("World.m_ValidBuildingIDs[" + std::to_string(i) + "]: no catalog building " + std::to_string(id));
                if (!seen.insert(key).second) return Fail("World.m_ValidBuildingIDs: building " + key + " listed twice");
                list.push_back(key);
            }
            w["available_buildings"] = std::move(list);
            return true;
        });
    }

    bool Construction(ordered_json& c)
    {
        return MapSection("ConstructionManager", kConstructionFields, c, [&](const OldField& f, const SectionField&) {
            if (f.record.Bits() > 50)
                return Fail("ConstructionManager.m_NumberOfBuilders: " + std::to_string(f.record.Bits()) + " over 50");
            c["builders"] = ordered_json::array();
            return true;
        });
    }

    bool Player(ordered_json& p)
    {
        return MapSection("Player", kPlayerFields, p, [](const OldField&, const SectionField&) { return true; });
    }

    // The id list is dropped: the loader rebuilds it from the objects in order, so the used slots of each group
    // (buildings from 0, guests from 500, the others from 2000) must hold the ids of the objects in file order.
    bool ObjectManager(ordered_json& m)
    {
        return MapSection("ObjectManager", kObjectManagerFields, m, [&](const OldField& f, const SectionField&) {
            const OldBlock* manager = Section("ObjectManager");
            const OldField* counts[] = {manager->Find("m_dwNumActiveBuildingObjects"), manager->Find("m_dwNumActiveGuestObjects"),
                    manager->Find("m_dwNumActiveOtherObjects")};
            constexpr uint32_t kBases[] = {0, 500, 2000};
            size_t object = 0;
            for (size_t group = 0; group < 3; ++group) {
                if (!counts[group]) return Fail("ObjectManager: no object counts");
                const uint32_t count = counts[group]->record.Bits();
                for (uint32_t i = 0; i < count; ++i, ++object) {
                    const size_t slot = kBases[group] + i;
                    const OldField* id = object < m_body.objects.size() ? m_body.objects[object].Find("m_dwID") : nullptr;
                    if (slot >= f.record.count || !id || f.record.Bits(slot) != id->record.Bits())
                        return Fail("ObjectManager.m_dwObjectIDList[" + std::to_string(slot) + "]: not the id of object " +
                                std::to_string(object));
                }
            }
            return true;
        });
    }

    // The coordinate pairs of the map view: each `.x`/`X` record opens a pair under its key, the `.y`/`Y` record closes it.
    bool View(ordered_json& v)
    {
        uint32_t first = 0;
        return MapSection("ZEngine", kMapViewFields, v, [&](const OldField& f, const SectionField&) {
            const char last = f.name.back();
            if (last == 'x' || last == 'X') {
                first = f.record.Bits();
                return true;
            }
            const uint32_t second = f.record.Bits();
            if (f.name == "m_MapY")
                v["camera"] = ordered_json::array({static_cast<int32_t>(first), static_cast<int32_t>(second)});
            else if (f.name == "m_MapPosition.y")
                v["position"] = ordered_json::array({SaveFloat(first), SaveFloat(second)});
            else if (f.name == "m_MapVelocity.y")
                v["velocity"] = ordered_json::array({SaveFloat(first), SaveFloat(second)});
            else if (f.name == "m_iMoveMapStartY")
                v["move_start"] = ordered_json::array({static_cast<int32_t>(first), static_cast<int32_t>(second)});
            else
                v["move_destination"] = ordered_json::array({static_cast<int32_t>(first), static_cast<int32_t>(second)});
            return true;
        });
    }

    bool Grid(ordered_json& m)
    {
        const OldBlock* grid = Section("WorldMap");
        if (!grid) return Fail("no WorldMap section");
        ordered_json flags = ordered_json::array(), heights = ordered_json::array(), areas = ordered_json::array();
        int32_t islandSize = 0;
        for (const OldField& f : grid->fields) {
            if (f.name == "m_uiFlags")
                flags.push_back(f.record.Bits());
            else if (f.name == "m_usZOffset")
                heights.push_back(f.record.Bits());
            else if (f.name.starts_with("m_Points->Closed["))
                areas.push_back(f.record.Bits() != 0); // The grid load routine opens the area for any value but 0
            else if (f.name == "m_IslandSize")
                islandSize = static_cast<int32_t>(f.record.Bits());
        }
        const size_t cells = flags.size();
        const auto side = static_cast<size_t>(std::lround(std::sqrt(static_cast<double>(cells))));
        if (cells == 0 || side * side != cells || heights.size() != cells)
            return Fail("WorldMap: " + std::to_string(cells) + " cells are not a square grid");
        m["cells"] = ordered_json::array({side, side});
        m["island_size"] = islandSize;
        m["areas_open"] = std::move(areas);
        m["flags"] = std::move(flags);
        m["heights"] = std::move(heights);
        return true;
    }

    // Fields the old game left uninitialized carry no meaning: a word that names no object becomes -1 (0 is the first
    // building's id). Both readers take that as no object: the walk point lookup keeps the destination and a post to it is dropped.
    static void ClearObjectRef(ordered_json& state, const char* key, const std::set<uint32_t>& live)
    {
        const auto it = state.find(key);
        if (it != state.end() && !live.contains(static_cast<uint32_t>(it->get<int32_t>()))) *it = -1;
    }

    bool Objects(ordered_json& list)
    {
        std::set<uint32_t> live;
        for (const OldBlock& o : m_body.objects)
            if (const OldField* id = o.Find("m_dwID")) live.insert(id->record.Bits());
        for (const OldBlock& o : m_body.objects) {
            const std::string where = o.kind + " at " + std::to_string(o.offset);
            if (o.type == 0 || o.type >= std::size(kObjectKinds)) return Fail(where + ": object type " + std::to_string(o.type));
            const ObjectKind& kind = kObjectKinds[o.type];
            const OldField* name = o.Find("m_Name");
            const OldField* id = o.Find("m_dwID");
            const OldField* x = o.Find("m_WorldXYZ.x");
            const OldField* y = o.Find("m_WorldXYZ.y");
            const OldField* z = o.Find("m_WorldXYZ.z");
            if (!name || !id || !x || !y || !z) return Fail(where + ": no Entity fields");

            // Catalog names are keys already; the other names (map props such as "Palm 4s") get the shared key rule:
            // lowercase, every run of other characters one '_' (save-conversion.md).
            bool clean = true;
            const std::string key = ContentKey(CText(name->record), &clean);
            if (!clean && kind.catalog) return Fail(where + ": name \"" + std::string(CText(name->record)) + "\" is not a content key");
            if (key.empty()) return Fail(where + ": empty name");
            if (kind.catalog && !CatalogHas(kind.catalog, key) && !(o.type == 19 && key == kBuiltinStaff))
                return Fail(where + ": " + key + " is not in catalog " + kind.catalog);

            ordered_json object;
            object["id"] = id->record.Bits();
            object["kind"] = kind.kind;
            object["type"] = key;
            if (const std::string_view objectClass = ObjectClassOf(o.type); !objectClass.empty())
                object["class"] = std::string(objectClass);
            object["pos"] = ordered_json::array({SaveFloat(x->record.Bits()), SaveFloat(y->record.Bits()), SaveFloat(z->record.Bits())});
            if (!ObjectState(o, key, object)) return false;
            if (object.contains("moving")) {
                ClearObjectRef(object["moving"], "building", live);
                if (object["moving"].contains("group")) object["moving"]["group"] = 0; // nothing reads it
            }
            if (object.contains("scenery")) ClearObjectRef(object["scenery"], "owner_building", live);
            // A building's rotation is its sprite direction (0..3, a quarter turn each; place-building `dir`).
            if (o.type == 2) {
                const OldField* dir = o.Find("sprite.m_iCurrDir");
                if (!dir || dir->record.Bits() > 3) return Fail(where + ": building rotation out of range");
                object["dir"] = kDirections[2 * dir->record.Bits()];
                object["sprite"].erase("dir");
            }
            list.push_back(std::move(object));
        }
        return true;
    }

    bool ObjectState(const OldBlock& o, const std::string& key, ordered_json& object)
    {
        const std::string where = o.kind + " at " + std::to_string(o.offset);
        const auto special = [&](const OldField& f, const SectionField&) {
            if (f.name == "m_bType") {
                if (f.record.Bits() != o.type)
                    return Fail(where + ".m_bType: " + std::to_string(f.record.Bits()) + " is not the object type");
            } else if (f.name.ends_with(".TextLength")) { // a building thought's text length with its terminator
                const OldField* text = &f + 1;
                if (text == o.fields.data() + o.fields.size() || !text->name.ends_with(".pText") ||
                        text->record.count + 1 != f.record.Bits())
                    return Fail(where + "." + f.name + ": not the length of the text with its terminator");
            } else if (f.name == "m_DayArrived") { // The guest object save routine writes the field twice
                if (f.record.Bits() != o.Find("m_DayArrived")->record.Bits()) return Fail(where + ".m_DayArrived: differs from the first");
            } else if (std::string created = ContentKey(CText(f.record)); created != key) { // Object Name: the key it is created by
                object["created_as"] = std::move(created);
            }
            return true;
        };
        const std::span<const OldField> fields = o.fields;
        constexpr size_t kHeader = std::size(kObjectHeaderFields);
        if (o.sharedFrom < kHeader || o.sharedFrom >= fields.size()) return Fail(where + ": no shared object layers");
        if (!MapFields(where, fields.first(kHeader), kObjectHeaderFields, object, special)) return false;
        if (!MapFields(where, fields.subspan(kHeader, o.sharedFrom - kHeader), kClassFields[o.type], object, special)) return false;
        const bool isStatic = fields[o.sharedFrom].name == "m_bGridW"; // the layer the reader found (scenery, building, table)
        return MapFields(where, fields.subspan(o.sharedFrom),
                isStatic ? std::span<const SectionField>(kStaticFields) : std::span<const SectionField>(kMovingFields), object, special);
    }

    const OldSaveBody& m_body;
    const nlohmann::json& m_index;
    const SaveOrigin& m_origin;
    std::string& m_error;
};

// ---- obl.save back to old records (converter/save-load-bridge.md) ----

// The value at `key` of `scope`, the inverse of PlaceSaveValue: "a.b" nests objects, each "[]" takes the next of
// `indices`. Null when the document has no such value.
const nlohmann::json* FindSaveValue(const nlohmann::json& scope, std::string_view key, std::span<const uint32_t> indices)
{
    const nlohmann::json* target = &scope;
    for (size_t dot = key.find('.'); dot < key.find("[]"); dot = key.find('.')) {
        const auto it = target->find(std::string(key.substr(0, dot)));
        if (it == target->end()) return nullptr;
        target = &*it;
        key.remove_prefix(dot + 1);
    }
    const size_t at = key.find("[]");
    const auto it = target->find(std::string(key.substr(0, at)));
    if (it == target->end()) return nullptr;
    if (at == std::string_view::npos) return &*it;
    if (indices.empty() || !it->is_array() || indices.front() >= it->size()) return nullptr;
    const nlohmann::json& element = (*it)[indices.front()];
    if (key.size() == at + 2) return &element;
    return FindSaveValue(element, key.substr(at + 3), indices.subspan(1));
}

constexpr std::pair<std::string_view, uint32_t> kSectionSizes[] = {{"World", 0xde4}, {"WorldMap", 340}, {"Path", 4},
        {"ObjectManager", 214024}, {"Player", 132}, {"FerryManager", 0xde4}, {"RepairManager", 24}, {"FeedbackManager", 536},
        {"MessageHandler", 8}, {"ZEngine", 356}, {"CleaningManager", 16}, {"ConstructionManager", 1012}};

// Section kind -> where its fields live in the document.
constexpr std::pair<std::string_view, std::string_view> kSectionScopes[] = {{"World", "world"}, {"ObjectManager", "managers.objects"},
        {"Player", "player"}, {"ZEngine", "view"}, {"RepairManager", "managers.repair"}, {"CleaningManager", "managers.cleaning"},
        {"EventManager", "managers.events"}, {"Weather", "managers.weather"}, {"FerryManager", "managers.ferry"},
        {"FeedbackManager", "managers.feedback"}, {"MessageHandler", "managers.messages"},
        {"ConstructionManager", "managers.construction"}};

// The object type byte of an obl.save object: its kind, for staff and performers its class.
uint8_t ObjectTypeOf(const nlohmann::json& o)
{
    const std::string kind = o.value("kind", "");
    if (kind == "scenery") return 1;
    if (kind == "building") return 2;
    if (kind == "guest") return 3;
    if (kind == "vehicle") return 4;
    if (kind == "table") return 5;
    if (kind == "animal") return 13;
    const std::string objectClass = o.value("class", "");
    for (const auto& [name, byte] : kObjectClasses)
        if (name == objectClass) return byte;
    return 0;
}

class JsonSource final : public RecordSource {
public:
    JsonSource(const nlohmann::json& d, const nlohmann::json& index, const SaveOrigin& origin, std::deque<std::vector<uint8_t>>& storage,
            std::string& error)
        : m_d(d), m_index(index), m_origin(origin), m_storage(storage), m_error(error)
    {
        const bool polish = origin.language == "pl";
        for (unsigned b = 0; b < 256; ++b) {
            const unsigned code = CodePoint(polish, b);
            m_narrow.emplace(code == kUndefinedCodePoint ? b : code, static_cast<uint8_t>(b));
        }
    }

    bool Take(OldBlock& block, std::string name, SaveTag tag, uint32_t count, SaveRecord* out) override
    {
        std::vector<uint8_t> bytes;
        uint32_t items = count ? count : 1;
        if (!Value(block, name, tag, count, bytes, items)) return false;
        m_storage.push_back(std::move(bytes));
        const SaveRecord record{0, tag, items, m_storage.back()};
        block.fields.push_back({std::move(name), record});
        if (out) *out = record;
        return true;
    }

    bool Has(const OldBlock& block, const std::string& label, std::span<const SaveTag>) override
    {
        if (block.kind == "WorldMap") {
            const nlohmann::json& map = m_d["map"];
            if (label == "m_uiFlags") return Occurrences(block, label) < map["flags"].size();
            const size_t i = std::stoul(label.substr(label.find('[') + 1));
            return map.contains("areas_open") && i < map["areas_open"].size(); // formats::CheckOblSaveDocument refuses it missing
        }
        Field f;
        return Locate(block, label, f, false) && f.value;
    }

    bool Class(OldBlock& block, std::string_view name) override
    {
        block.kind = std::string(name);
        return true;
    }
    bool Fail(size_t, const OldBlock& block, std::string_view field, std::string_view why) override
    {
        m_error = block.kind + "." + std::string(field) + ": " + std::string(why);
        return false;
    }
    size_t Offset() const override { return 0; }
    size_t Remaining() const override { return SIZE_MAX / 4; }

private:
    struct Field {
        const SectionField* spec = nullptr;
        std::string key;
        std::vector<uint32_t> indices;
        const nlohmann::json* scope = nullptr;
        const nlohmann::json* value = nullptr;
    };

    // Sections have fixed kinds; every other block is an object (its kind is the class the reader named).
    static bool IsObject(const OldBlock& block)
    {
        if (block.kind == "EventManager" || block.kind == "Weather") return false;
        for (const auto& [kind, size] : kSectionSizes)
            if (kind == block.kind) return false;
        return true;
    }

    // How often `label` occurs in `block` so far. Counted incrementally: the WorldMap block repeats two labels per cell, and
    // a full scan per record made a document load quadratic (about 20 s of a Debug load for a 128 x 128 map).
    uint32_t Occurrences(const OldBlock& block, std::string_view label)
    {
        if (m_counted.block != &block || block.fields.size() < m_counted.scanned) m_counted = {&block, 0, {}};
        for (; m_counted.scanned < block.fields.size(); ++m_counted.scanned) ++m_counted.counts[block.fields[m_counted.scanned].name];
        const auto it = m_counted.counts.find(label);
        return it == m_counted.counts.end() ? 0 : it->second;
    }

    const nlohmann::json& Object() const { return m_d["objects"][m_object]; }

    // The table entry, key and document value of `label` in `block`; `taking` counts the label's earlier records.
    bool Locate(const OldBlock& block, const std::string& label, Field& f, bool taking)
    {
        std::span<const SectionField> table;
        if (IsObject(block)) {
            if (m_object >= m_d["objects"].size()) return false;
            f.scope = &Object();
            const uint8_t type = ObjectTypeOf(*f.scope);
            if (label == "Object type" || label == "Object Name")
                table = kObjectHeaderFields;
            else if (block.sharedFrom && block.fields.size() >= block.sharedFrom)
                table = type == 1 || type == 2 || type == 5 ? std::span<const SectionField>(kStaticFields)
                                                            : std::span<const SectionField>(kMovingFields);
            else
                table = kClassFields[type];
        } else {
            for (const auto& [kind, fields] : kSectionTables)
                if (kind == block.kind) table = fields;
            for (const auto& [kind, path] : kSectionScopes)
                if (kind == block.kind) {
                    f.scope = &m_d;
                    for (std::string_view rest = path; !rest.empty();) {
                        const size_t dot = std::min(rest.find('.'), rest.size());
                        const auto it = f.scope->find(std::string(rest.substr(0, dot)));
                        if (it == f.scope->end()) { // an optional section: every field takes its default
                            static const nlohmann::json kAbsent = nlohmann::json::object();
                            f.scope = &kAbsent;
                            break;
                        }
                        f.scope = &*it;
                        rest.remove_prefix(std::min(dot + 1, rest.size()));
                    }
                }
            if (!f.scope) return false;
        }
        const FoundField found = FindField(table, label, taking ? Occurrences(block, label) : 0);
        if (!found.spec) return false;
        f.spec = found.spec;
        f.key = found.key;
        GenericLabel(label, &f.indices);
        if (!f.key.empty()) f.value = FindSaveValue(*f.scope, f.key, f.indices);
        return true;
    }

    // The value of a section field the document leaves out (save-load-bridge.md, "Defaults"): zero, false, empty text or
    // no catalog entry; the ObjectManager's next free id is one past the largest object id, the ferry's cursor none (-1).
    bool Default(const OldBlock& block, const std::string& label, const Field& f, SaveTag tag, uint32_t count, std::vector<uint8_t>& bytes,
            uint32_t& items)
    {
        const size_t width = Width(tag);
        switch (f.spec->map) {
        case Map::Text:
        case Map::Name: return NarrowText("", bytes, items);
        case Map::WideText: WideText("", bytes, items); return true;
        case Map::Building:
        case Map::Beer:
        case Map::Event: Put(bytes, 0xFFFFFFFFu, width); return true;
        case Map::UInt:
            if (block.kind == "ObjectManager" && label == "m_dwNextFreeObjectID") {
                uint32_t next = 0;
                for (const auto& o : m_d["objects"]) next = std::max(next, o["id"].get<uint32_t>() + 1);
                Put(bytes, next, width);
                return true;
            }
            [[fallthrough]];
        case Map::Int:
            if (block.kind == "FerryManager" && label == "Current It") { // an empty schedule has the sentinel cursor
                Put(bytes, 0xFFFFFFFFu, width);
                return true;
            }
            [[fallthrough]];
        case Map::Float:
        case Map::State:
        case Map::Bool:
        case Map::Ints:
        case Map::Floats:
        case Map::UInts:
        case Map::ByteFlag:
            for (uint32_t i = 0; i < (count ? count : 1); ++i) Put(bytes, 0, width);
            return true;
        default: return Missing(block, label, "no value at " + f.key);
        }
    }

    bool Missing(const OldBlock& block, const std::string& label, const std::string& what)
    {
        m_error = block.kind + "." + label + ": " + what;
        return false;
    }

    static void Put(std::vector<uint8_t>& bytes, uint32_t value, size_t width)
    {
        for (size_t i = 0; i < width; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
    static size_t Width(SaveTag tag)
    {
        switch (tag) {
        case SaveTag::Class:
        case SaveTag::String:
        case SaveTag::Bool:
        case SaveTag::Byte:
        case SaveTag::ByteArray: return 1;
        case SaveTag::Short:
        case SaveTag::WideString:
        case SaveTag::Word: return 2;
        default: return 4;
        }
    }

    bool NarrowText(const std::string& utf8, std::vector<uint8_t>& bytes, uint32_t& items)
    {
        for (const char16_t unit : code_page::Utf8ToUtf16(utf8)) {
            const auto it = m_narrow.find(unit);
            if (it == m_narrow.end()) return false;
            bytes.push_back(it->second);
        }
        items = static_cast<uint32_t>(bytes.size());
        return true;
    }
    static void WideText(const std::string& utf8, std::vector<uint8_t>& bytes, uint32_t& items)
    {
        const std::u16string text = code_page::Utf8ToUtf16(utf8);
        for (const char16_t unit : text) Put(bytes, unit, 2);
        items = static_cast<uint32_t>(text.size());
    }

    // The value of a scalar or array record from a number, an f32 or a list of them.
    bool Numbers(const nlohmann::json& v, SaveTag tag, uint32_t count, std::vector<uint8_t>& bytes)
    {
        const size_t width = Width(tag);
        const auto one = [&](const nlohmann::json& item) {
            if (tag == SaveTag::Float || tag == SaveTag::FloatArray) {
                const auto bits = SaveFloatBits(item);
                if (!bits) return false;
                Put(bytes, *bits, width);
                return true;
            }
            if (item.is_boolean()) {
                Put(bytes, item.get<bool>() ? 1 : 0, width);
                return true;
            }
            if (!item.is_number_integer()) return false;
            Put(bytes, static_cast<uint32_t>(item.get<int64_t>()), width);
            return true;
        };
        if (!v.is_array()) return count <= 1 && one(v);
        if (v.size() != count) return false;
        for (const auto& item : v)
            if (!one(item)) return false;
        return true;
    }

    int32_t CatalogPosition(const char* list, const std::string& key) const
    {
        const auto it = m_index.find(list);
        if (it == m_index.end() || !it->is_array()) return -2;
        for (size_t i = 0; i < it->size(); ++i)
            if ((*it)[i] == key) return static_cast<int32_t>(i);
        return -2;
    }

    // The record `label` of `block` from the document.
    bool Value(OldBlock& block, const std::string& label, SaveTag tag, uint32_t count, std::vector<uint8_t>& bytes, uint32_t& items)
    {
        const size_t width = Width(tag);
        if (label == "sizeof") {
            for (const auto& [kind, size] : kSectionSizes)
                if (kind == block.kind) {
                    Put(bytes, size, width);
                    return true;
                }
            return Missing(block, label, "no section size");
        }
        if (label == "Object type") ++m_object; // a new object starts
        if (block.kind == "WorldMap") return Grid(block, label, bytes, width);

        Field f;
        if (!Locate(block, label, f, true)) return Missing(block, label, "no obl.save field for this label");
        switch (f.spec->map) {
        case Map::Drop:
        case Map::Special: return Rebuilt(block, label, f, tag, count, bytes, items);
        case Map::Group: return Missing(block, label, "group label");
        case Map::List: Put(bytes, f.value && f.value->is_array() ? static_cast<uint32_t>(f.value->size()) : 0, width); return true;
        case Map::UInts: {
            const nlohmann::json* list = FindSaveValue(*f.scope, f.key, {});
            const uint32_t i = f.indices.empty() ? 0 : f.indices.back();
            if (!list && !IsObject(block)) return Default(block, label, f, tag, count, bytes, items);
            if (!list || !list->is_array() || i >= list->size()) return Missing(block, label, "no element " + std::to_string(i));
            Put(bytes, (*list)[i].get<uint32_t>(), width);
            return true;
        }
        default: break;
        }
        if (!f.value) {
            // A building's sprite direction is the object's `dir`.
            if (f.key == "sprite.dir" && f.scope->contains("dir")) {
                const std::string dir = (*f.scope)["dir"];
                for (uint32_t i = 0; i < 4; ++i)
                    if (dir == kDirections[2 * i]) Put(bytes, i, width);
                return bytes.size() == width || Missing(block, label, "bad dir");
            }
            if (!IsObject(block)) return Default(block, label, f, tag, count, bytes, items);
            return Missing(block, label, "no value at " + f.key);
        }
        const nlohmann::json& v = *f.value;
        switch (f.spec->map) {
        case Map::Float:
        case Map::Int:
        case Map::UInt:
        case Map::Bool:
        case Map::Ints:
        case Map::Floats:
            if (!Numbers(v, tag, count ? count : 1, bytes)) return Missing(block, label, "not a value of " + f.key);
            return true;
        case Map::State: {
            const auto number = formats::SaveStateNumber(f.spec->states, v, &m_origin.objectStates);
            if (!number) return Missing(block, label, "no state at " + f.key);
            Put(bytes, static_cast<uint32_t>(*number), width);
            return true;
        }
        case Map::ByteFlag:
            if (!v.is_number_integer() || v.get<int64_t>() < 0 || v.get<int64_t>() > 255)
                return Missing(block, label, "not a byte flag at " + f.key);
            Put(bytes, v.get<uint32_t>(), width);
            return true;
        case Map::WideText: WideText(v.get<std::string>(), bytes, items); return true;
        case Map::Text:
        case Map::Name: return NarrowText(v.get<std::string>(), bytes, items) || Missing(block, label, "text outside the pack's code page");
        case Map::Building:
        case Map::Beer:
        case Map::Event: {
            const char* list = f.spec->map == Map::Building ? "buildings" : f.spec->map == Map::Beer ? "beers" : "events";
            const int32_t id = v.is_null() ? -1 : CatalogPosition(list, v.get<std::string>());
            if (id == -2) return Missing(block, label, "not in catalog " + std::string(list));
            Put(bytes, static_cast<uint32_t>(id), width);
            return true;
        }
        default: return Missing(block, label, "unmapped");
        }
    }

    bool Grid(const OldBlock& block, const std::string& label, std::vector<uint8_t>& bytes, size_t width)
    {
        const nlohmann::json& map = m_d["map"];
        if (label == "m_IslandSize") {
            Put(bytes, static_cast<uint32_t>(map.value("island_size", 0)), width); // optional, default 0
        } else if (label == "m_uiFlags" || label == "m_usZOffset") {
            const nlohmann::json& list = map[label == "m_uiFlags" ? "flags" : "heights"];
            Put(bytes, list[Occurrences(block, label)].get<uint32_t>(), width);
        } else {
            const size_t i = std::stoul(label.substr(label.find('[') + 1));
            Put(bytes, map["areas_open"][i].get<bool>() ? 1 : 0, width);
        }
        return true;
    }

    // Records the conversion drops or checks: rebuilt from the rest of the document.
    bool Rebuilt(const OldBlock& block, const std::string& label, const Field& f, SaveTag tag, uint32_t count, std::vector<uint8_t>& bytes,
            uint32_t& items)
    {
        const size_t width = Width(tag);
        const nlohmann::json& s = *f.scope;
        if (block.kind == "World") {
            static const nlohmann::json kNone = nlohmann::json::array();
            const auto it = s.find("available_buildings"); // optional: none
            const nlohmann::json& available = it == s.end() ? kNone : *it;
            if (label == "m_MapName") {
                const std::string id = s.value("map_id", "");
                const std::string path = id.empty() ? "" : "data/Scenario/" + id + "/" + id + ".biz";
                return NarrowText(path, bytes, items) || Missing(block, label, "text outside the pack's code page");
            }
            if (label == "m_NumValidBuildings") {
                Put(bytes, static_cast<uint32_t>(available.size()), width);
            } else if (label == "m_ValidBuildingIDs") {
                for (uint32_t i = 0; i < count; ++i) {
                    int32_t id = 0;
                    if (i < available.size()) id = CatalogPosition("buildings", available[i].get<std::string>());
                    if (id < 0) return Missing(block, label, "building not in the catalog");
                    Put(bytes, static_cast<uint32_t>(id), width);
                }
            } else {
                for (uint32_t i = 0; i < (count ? count : 1); ++i) Put(bytes, 0, width); // m_BoatList, m_NumBoats: never used
            }
            return true;
        }
        if (block.kind == "ObjectManager") {
            uint32_t groups[3] = {};
            for (const auto& o : m_d["objects"]) {
                const uint8_t type = ObjectTypeOf(o);
                ++groups[type == 2 ? 0 : type == 3 ? 1 : 2];
            }
            if (label == "m_dwNumActiveObjects") Put(bytes, groups[0] + groups[1] + groups[2], width);
            if (label == "m_dwNumActiveBuildingObjects") Put(bytes, groups[0], width);
            if (label == "m_dwNumActiveGuestObjects") Put(bytes, groups[1], width);
            if (label == "m_dwNumActiveOtherObjects") Put(bytes, groups[2], width);
            if (label == "m_dwObjectIDList") {
                std::vector<uint32_t> slots(count, 0xFFFFFFFFu);
                constexpr uint32_t kBases[] = {0, 500, 2000};
                uint32_t next[3] = {};
                for (const auto& o : m_d["objects"]) {
                    const uint8_t type = ObjectTypeOf(o);
                    const int group = type == 2 ? 0 : type == 3 ? 1 : 2;
                    const uint32_t slot = kBases[group] + next[group]++;
                    if (slot < count) slots[slot] = o["id"].get<uint32_t>();
                }
                for (const uint32_t id : slots) Put(bytes, id, width);
            }
            return !bytes.empty() || Missing(block, label, "not rebuilt");
        }
        if (block.kind == "ZEngine") {
            if (label == "burnqueue size") {
                Put(bytes, static_cast<uint32_t>(s.contains("burn_queue") ? s["burn_queue"].size() : 0), width);
                return true;
            }
            constexpr std::pair<std::string_view, std::pair<const char*, int>> kPairs[] = {{"m_MapX", {"camera", 0}},
                    {"m_MapY", {"camera", 1}}, {"m_MapPosition.x", {"position", 0}}, {"m_MapPosition.y", {"position", 1}},
                    {"m_MapVelocity.x", {"velocity", 0}}, {"m_MapVelocity.y", {"velocity", 1}}, {"m_iMoveMapStartX", {"move_start", 0}},
                    {"m_iMoveMapStartY", {"move_start", 1}}, {"m_iMoveMapDestX", {"move_destination", 0}},
                    {"m_iMoveMapDestY", {"move_destination", 1}}};
            for (const auto& [name, at] : kPairs) {
                if (name != label) continue;
                if (!s.contains(at.first)) { // an optional pair: 0
                    Put(bytes, 0, width);
                    return true;
                }
                return Numbers(s[at.first][at.second], tag, 1, bytes) || Missing(block, label, "bad pair");
            }
            return Missing(block, label, "not rebuilt");
        }
        if (block.kind == "ConstructionManager") { // m_NumberOfBuilders: the builder list's size
            Put(bytes, static_cast<uint32_t>(s.contains("builders") ? s["builders"].size() : 0), width);
            return true;
        }
        // Objects.
        const nlohmann::json& o = Object();
        const std::string name = o.value("type", "");
        if (label == "Object type" || label == "m_bType") {
            const uint8_t type = ObjectTypeOf(o);
            if (!type) return Missing(block, label, "no object type for kind " + o.value("kind", ""));
            Put(bytes, type, width);
        } else if (label == "Object Name") {
            return NarrowText(o.value("created_as", name), bytes, items) || Missing(block, label, "bad name");
        } else if (label == "m_Name") {
            return NarrowText(name, bytes, items) || Missing(block, label, "bad name");
        } else if (label == "m_dwID") {
            Put(bytes, o["id"].get<uint32_t>(), width);
        } else if (label == "m_WorldXYZ.x" || label == "m_WorldXYZ.y") {
            Put(bytes, *SaveFloatBits(o["pos"][label.back() == 'x' ? 0 : 1]), width);
        } else if (label == "m_WorldXYZ.z") {
            Put(bytes, *SaveFloatBits(o["pos"][2]), width);
        } else if (label == "on path") {
            Put(bytes, o.contains("moving") && o["moving"].contains("path") ? 1 : 0, width);
        } else if (label == "attached sprite" || label == "m_pAttachedSprite") {
            bool attached = false;
            for (const auto& [key, value] : o.items())
                if (value.is_object() && value.contains("attached")) attached = true;
            Put(bytes, attached ? 1 : 0, width);
        } else if (label == "Events") {
            Put(bytes, o.contains("building") && o["building"].contains("events") ? 1 : 0, width);
        } else if (label == "m_QueueValue") {
            Put(bytes, o.contains("building") && o["building"].contains("queue") ? 1 : 0, width);
        } else if (label == "m_DayArrived") { // written twice: the second is the first again
            const OldField* first = block.Find("m_DayArrived");
            if (!first) return Missing(block, label, "no first record");
            bytes.assign(first->record.bytes.begin(), first->record.bytes.end());
        } else if (label.ends_with(".TextLength")) { // the thought's text length with its terminator
            const std::string textLabel = label.substr(0, label.size() - 10) + "pText";
            Field text;
            if (!Locate(block, textLabel, text, false) || !text.value) return Missing(block, label, "no thought text");
            Put(bytes, static_cast<uint32_t>(code_page::Utf8ToUtf16(text.value->get<std::string>()).size() + 1), width);
        } else {
            return Missing(block, label, "not rebuilt");
        }
        return true;
    }

    const nlohmann::json& m_d;
    const nlohmann::json& m_index;
    const SaveOrigin& m_origin;
    std::deque<std::vector<uint8_t>>& m_storage;
    std::string& m_error;
    std::map<unsigned, uint8_t> m_narrow; // code point -> byte of the pack's code page
    size_t m_object = SIZE_MAX; // the object being read (advanced by its "Object type" record)
    struct {
        const OldBlock* block = nullptr;
        size_t scanned = 0;
        std::map<std::string, uint32_t, std::less<>> counts;
    } m_counted; // Occurrences of the block being read
};

} // namespace

bool OldSaveBodyFromDocument(
        const nlohmann::json& document, const nlohmann::json& catalogIndex, const SaveOrigin& origin, OldSaveBody& body, std::string& error)
{
    body = {};
    nlohmann::json d = document;
    formats::RecordsToSlots(d);
    auto storage = std::make_shared<std::deque<std::vector<uint8_t>>>();
    if (!d.is_object() || d.value("format", "") != "obl.save" || !d.contains("header") || !d.contains("objects") || !d.contains("map")) {
        error = "not an obl.save document";
        return false;
    }
    const nlohmann::json& h = d["header"];
    OldSaveHeader header;
    try {
        const std::string scenario = h["scenario"];
        const bool sandbox = scenario.starts_with("sandbox");
        header.scenario = std::stoi(scenario.substr(sandbox ? 7 : 1)) - 1;
        header.sandbox = h["sandbox"].get<bool>();
        header.savedAt = h["saved_at"].get<uint32_t>();
        header.day = h["day"].get<int32_t>();
        header.dayTimeBits = SaveFloatBits(h["day_time"]).value_or(0);
        header.cashBits = SaveFloatBits(h["cash"]).value_or(0);
        header.guests = h["guests"].get<int32_t>();
        // The slot title: the text key the game stored, else the stored text (in the pack's code page).
        std::string title = h.contains("title") ? h["title"].get<std::string>() : std::string();
        std::vector<uint8_t> bytes;
        if (!title.empty()) {
            bytes.assign(title.begin(), title.end());
        } else {
            const bool polish = origin.language == "pl";
            for (const char16_t unit : code_page::Utf8ToUtf16(h["title_text"].get<std::string>())) {
                uint8_t byte = '?';
                for (unsigned b = 0; b < 256; ++b) {
                    const unsigned code = CodePoint(polish, b);
                    if ((code == kUndefinedCodePoint ? b : code) == unit) byte = static_cast<uint8_t>(b);
                }
                bytes.push_back(byte);
            }
        }
        storage->push_back(std::move(bytes));
        header.title = SaveRecord{0, SaveTag::String, static_cast<uint32_t>(storage->back().size()), storage->back()};
    } catch (const nlohmann::json::exception& e) {
        error = std::string("header: ") + e.what();
        return false;
    }
    body.storage = storage;
    try {
        JsonSource source(d, catalogIndex, origin, *storage, error);
        if (!ReadOldSaveBody(source, header, body)) return false;
    } catch (const std::exception& e) {
        error = std::string("obl.save: ") + e.what();
        return false;
    }
    return true;
}

bool PlaceSaveValue(ordered_json& out, std::string_view key, std::span<const uint32_t> indices, ordered_json value)
{
    ordered_json* target = &out;
    for (size_t dot = key.find('.'); dot < key.find("[]"); dot = key.find('.')) { // the objects before the first list
        target = &(*target)[std::string(key.substr(0, dot))];
        key.remove_prefix(dot + 1);
    }
    const size_t at = key.find("[]");
    if (at == std::string_view::npos) {
        if (target->contains(std::string(key))) return false; // a repeated label needs its own entry, never an overwrite
        (*target)[std::string(key)] = std::move(value);
        return true;
    }
    if (indices.empty()) return false;
    const uint32_t index = indices.front();
    ordered_json& list = (*target)[std::string(key.substr(0, at))];
    if (list.is_null() && index == 0) list = ordered_json::array();
    if (!list.is_array() || index > list.size()) return false;
    if (key.size() == at + 2) {
        if (index < list.size()) return false;
        list.push_back(std::move(value));
        return true;
    }
    if (index == list.size()) list.push_back(ordered_json::object());
    return PlaceSaveValue(list[index], key.substr(at + 3), indices.subspan(1), std::move(value));
}

bool OldSectionLabelKnown(std::string_view section, std::string_view label)
{
    for (const auto& [name, table] : kSectionTables)
        if (name == section) return FindField(table, label).spec != nullptr;
    return false;
}

std::string OldObjectFieldKey(uint8_t type, std::string_view label)
{
    if (type >= std::size(kClassFields)) return {};
    FoundField found = FindField(kClassFields[type], label);
    return found.spec && found.spec->name ? std::move(found.key) : std::string();
}

std::string OldMovingFieldKey(std::string_view label)
{
    FoundField found = FindField(kMovingFields, label);
    return found.spec && found.spec->name ? std::move(found.key) : std::string();
}

std::string OldStaticFieldKey(std::string_view label, uint32_t occurrence)
{
    FoundField found = FindField(std::span<const SectionField>(kStaticFields).first(std::size(kStaticFields) - 1), label, occurrence);
    return found.spec && found.spec->name ? std::move(found.key) : std::string();
}

namespace {
// Whether a record of `table` maps onto `key`; a group's records are its name, a dot and their own key (an unnamed group adds nothing).
bool TableHasKey(std::span<const SectionField> table, std::string_view key)
{
    for (const SectionField& f : table) {
        if (!f.name) continue;
        if (f.map != Map::Group) {
            if (key == f.name) return true;
            continue;
        }
        std::string_view rest = key;
        if (*f.name) {
            const std::string_view prefix = f.name;
            if (!rest.starts_with(prefix) || rest.size() <= prefix.size() || rest[prefix.size()] != '.') continue;
            rest.remove_prefix(prefix.size() + 1);
        }
        if (TableHasKey(f.group, rest)) return true;
    }
    return false;
}
} // namespace

bool OldObjectFieldKeyKnown(uint8_t type, std::string_view key)
{
    return type < std::size(kClassFields) && TableHasKey(kClassFields[type], key);
}

bool OldMovingFieldKeyKnown(std::string_view key)
{
    return TableHasKey(kMovingFields, key);
}

bool OldStaticFieldKeyKnown(std::string_view key)
{
    return TableHasKey(std::span<const SectionField>(kStaticFields).first(std::size(kStaticFields) - 1), key);
}

bool OldSaveDocument(
        const OldSaveBody& body, const nlohmann::json& catalogIndex, const SaveOrigin& origin, ordered_json& document, std::string& error)
{
    if (!DocumentWriter(body, catalogIndex, origin, error).Run(document)) return false;
    formats::SlotsToRecords(document);
    return true;
}

bool OldSaveBytesDocument(std::span<const uint8_t> bytes, const std::filesystem::path& packRoot, SaveOrigin& origin, ordered_json& document,
        std::string& error)
{
    OldSaveBody body;
    if (!ReadOldSaveBody(bytes, body, error)) return false;
    if (const auto problems = CheckOldSaveBody(body); !problems.empty()) {
        error = problems.front();
        return false;
    }
    if (!formats::ReadSavePackOrigin(packRoot, origin, error)) return false;
    const std::filesystem::path indexPath = pack_path::Resolve(packRoot, "catalog/index.json");
    std::ifstream in(indexPath, std::ios::binary);
    if (!in) {
        error = "cannot read " + indexPath.string() + " (convert the catalog first)";
        return false;
    }
    const nlohmann::json index = nlohmann::json::parse(in, nullptr, false);
    if (index.is_discarded() || !index.is_object()) {
        error = indexPath.string() + ": not JSON";
        return false;
    }
    return OldSaveDocument(body, index, origin, document, error);
}

bool ConvertOldSave(const std::filesystem::path& save, const std::filesystem::path& packRoot, const std::filesystem::path& output,
        const json_schema::Registry& schemas, std::string& error)
{
    std::vector<uint8_t> bytes;
    if (!ReadBoundedFile(save.string(), kMaxOldSaveBytes, bytes, error)) {
        error = save.string() + ": " + error;
        return false;
    }
    SaveOrigin origin;
    origin.file = save.filename().string();
    ordered_json document;
    if (!OldSaveBytesDocument(bytes, packRoot, origin, document, error)) {
        error = save.string() + ": " + error;
        return false;
    }
    return formats::WriteOblSaveDocument(document, output, schemas, error);
}

} // namespace obl::convert
