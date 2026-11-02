#pragma once

#include "core/char16.h"
#include "core/pointer_width.h"
#include "game/tables/world_params_leaves.h"
#include "core/rect.h"
#include "game/ui/sprite.h"
#include "game/objects/object_fields.h"
#include "game/objects/object_static.h"

#include <array>
#include <cstdint>
#include <deque>

namespace obl::object {
struct BuildingEvents;
}

// Compatibility name for facade readers that only need the forward declaration.
namespace obl::layout {
using BuildingEvents = object::BuildingEvents;
}

// Building (docs/architecture/objects.md): a placed building with its staff,
// goods, money, repair state, vehicles, tables, loungers, thoughts, queue and event schedule. The logic is building_*.h.
// The building constructor sets what building_construction.h lists; every other field keeps the zero of its member initializer. Saved fields m_* are named after their save keys; cached/derived fields carry descriptive names. The field
// order follows the save layout; natural alignment places the fields.
namespace obl::object {

// One entry of Building::m_ThoughtList: the deque holds pointers to these; entry and text are `operator new` blocks owned by the list
// (building_thoughts.h).
struct ThoughtEntry {
    Char16* m_text = nullptr; // copy of the thought text, owned by the entry
    int32_t m_objectId = 0; // the guest that thought it (what RemoveThoughtsOf matches)
};
using ThoughtList = std::deque<ThoughtEntry*>;

struct Building : Prop {
    Building() = default;
    // Builds the building of the given key (building_construct.cpp).
    explicit Building(const char* key);
    // Releases the building's sprites, queue and events (building_destruct.cpp).
    ~Building() override;
    // The slots that differ from the parent's (the unit tests' trap seam).
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    void Draw() override;
    void DrawOverlay() override;
    int GetOverlayOffset() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    void ReduceSelectionToHover() override;
    bool IsSelectable() override;

    // The screen side (building_draw.cpp). UpdateScreenRect returns the building's screen rectangle (a screen::Rect).
    void* UpdateScreenRect();
    void UpdateScreenCells(float x, float y, float z);
    void DrawLightMask(uint8_t* lightBuffer);
    void BeginDemolish();
    // The beach-open mode of a tower's influence area (mode 0: open).
    bool IsBeachOpenMode(int32_t mode);
    void SetBeachOpenMode(int32_t mode);

    const tables::BuildingDef* m_definition = nullptr; // WorldParams building record (GetDefinition); not saved
    int32_t m_staff[10] = {}; // IDs of the staff serving guests
    int32_t m_numMalestaff = 0;
    int32_t m_numFemalestaff = 0;
    int32_t m_NonServeStaff[10] = {}; // cleaner, mechanic, ... assigned to the building
    int32_t m_NumNonServestaff = 0;
    uint8_t m_staff_busy[10] = {};
    uint8_t m_burn = 0;
    uint8_t m_Entrance = 0;
    float m_queue_point[3] = {};
    float m_serve_point[3] = {};
    float m_EntrancePoint[3] = {};
    float m_queue_direction[3] = {};
    float m_TopWorldPos[3] = {}; // set by SetTopWorldPosition
    float m_Placed = 0;
    int32_t m_NumLilos = 0;
    int32_t m_NumBoards = 0;
    int32_t m_NumSnorkels = 0;
    int32_t m_CurCapacity = 0;
    int32_t m_NumVehicles = 0;
    int32_t m_Vehicles[6] = {};
    uint8_t m_vehicleInUse[6] = {};
    // one per ANIMSPRITE of the definition (BuildingDef::m_NumAnimSprites), made by the constructor
    ui::SpriteInstance* m_animSprites[8] = {};
    void* m_constructionSprite = nullptr; // SpriteInstance*, saved with sprite serialization when non-null
    ui::SpriteInstance* m_shortageSprite = nullptr; // UI3_fb_shortage, made by the constructor, always released
    ui::SpriteInstance* m_spannerSprite = nullptr; // UI3_fb_spanner
    void* m_queue = nullptr; // BuildingQueue (two std::deque); null = no queue
    BuildingEvents* m_events = nullptr; // docs/architecture/objects.md; null = no schedule
    // GetWalkPoint reads entry `index` here
    const std::array<float, 2>* m_walkPoints = nullptr;
    uint32_t m_numWalkPoints = 0;
    float m_LastMsgTime = 0;
    float m_lightsOn = 0; // hours
    float m_lightsOff = 0;
    uint8_t m_hasBall = 0;
    float m_BallPosition[3] = {};
    ui::SpriteInstance* m_ballSprite = nullptr; // released by Destruct when m_hasBall
    uint32_t m_OpenStatus = 0; // 0 open, 1 closed
    int32_t m_NumGuests = 0;
    float m_SatisfactionPercentage = 0;
    // The male/female halves of one array of interleaved scores: each commodity slot holds one score per preference.
    float m_Commod_Intensity[5][2] = {};
    float m_Commod_Price[5] = {};
    float m_Commod_PriceFactor[5] = {};
    float m_Commod_Cost[5] = {};
    float m_BeerStrength = 0;
    int32_t m_BeerType = 0;
    float m_incomeToday = 0;
    float m_costToday = 0;
    float m_profitToday = 0;
    float m_averageProfit = 0;
    int32_t m_BuildingAge = 0;
    float m_profitRecord[64] = {};
    float m_lastFaultDayTime = 0;
    int32_t m_LastFaultDay = 0;
    int32_t m_NumFaults = 0;
    int32_t m_UserRepairInterval = 0;
    int32_t m_RepairInterval = 0;
    uint32_t m_RepairState = 0; // 0..3, 3 = broken
    uint8_t m_wasBroken = 0;
    int32_t m_LastRepairCall = 0;
    float m_lastRepairDayTime = 0;
    int32_t m_LastRepairDay = 0;
    int32_t m_NumVisitsToday = 0;
    int32_t m_VisitsSinceCleaned = 0;
    int32_t m_LastFailTime = 0;
    uint32_t m_PowerConsumption = 0;
    int32_t m_NumTimesCleanedToday = 0;
    uint8_t m_closedForPower = 0;
    uint8_t m_userClose = 0;
    uint8_t m_calledForInspection = 0;
    uint8_t m_calledForRepair = 0;
    uint8_t m_builtByUser = 0;
    uint32_t m_numTables = 0;
    int32_t m_Tables[10] = {};
    uint32_t m_numLoungers = 0;
    int32_t m_loungers[20] = {};
    uint8_t m_burnLimbo = 0;
    screen::Rect m_rScrExtents = {};
    screen::Rect m_rScrRect = {};
    float m_timeInLimbo = 0;
    int32_t m_shadowOffset = 0;
    int32_t m_pickOffset = 0;
    int32_t m_lightMapOffset = 0;
    uint8_t m_soundPlaying = 0;
    int32_t m_SoundID = 0;
    // The newest thought first (building_thoughts.h); the teardown deletes the entries, `delete` of the building frees the deque.
    ThoughtList m_ThoughtList;
    int32_t m_InfluenceArea = 0;
    float m_buildingAnimTime = 0;
};

// The building's save records, then Prop's (the caller). Visitor: Int/UInt/Float/Bool(key, field, hashed),
// Ints/Floats/Bytes(key, pointer, count, hashed) and four entries for the parts with their own records:
// Sprites(key, building) (the definition's animation sprites, then m_constructionSprite when set), Construction(building) (the two
// "bTempUnder..." flags, which Save asks the ConstructionManager for and Load turns into m_burn), Events(key, building) (the
// presence flag, then the schedule), Thoughts(key, building) and Queue(key, building, hashed) (the presence flag, then the
// queue). The two commodity intensity arrays overlap (the second starts two floats into the first). The snapshot hashes the
// staff, the money and repair counters, the open state, the table and lounger counts, the queue and the influence area.
template<SelfOf<Building> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Ints("building.staff", o.m_staff, 10, Hashed::Yes);
    v.Int("building.male_staff", o.m_numMalestaff, Hashed::Yes);
    v.Int("building.female_staff", o.m_numFemalestaff, Hashed::Yes);
    v.Ints("building.non_serve_staff", o.m_NonServeStaff, 10, Hashed::Yes);
    v.Int("building.non_serve_staff_count", o.m_NumNonServestaff, Hashed::Yes);
    v.Bytes("building.staff_busy", o.m_staff_busy, 10, Hashed::Yes);
    v.Bool("building.burn", o.m_burn, Hashed::No);
    v.Bool("building.entrance", o.m_Entrance, Hashed::No);
    v.Float("building.queue_point.x", o.m_queue_point[0], Hashed::No);
    v.Float("building.queue_point.y", o.m_queue_point[1], Hashed::No);
    v.Float("building.queue_point.z", o.m_queue_point[2], Hashed::No);
    v.Float("building.serve_point.x", o.m_serve_point[0], Hashed::No);
    v.Float("building.serve_point.y", o.m_serve_point[1], Hashed::No);
    v.Float("building.serve_point.z", o.m_serve_point[2], Hashed::No);
    v.Float("building.entrance_point.x", o.m_EntrancePoint[0], Hashed::No);
    v.Float("building.entrance_point.y", o.m_EntrancePoint[1], Hashed::No);
    v.Float("building.entrance_point.z", o.m_EntrancePoint[2], Hashed::No);
    v.Float("building.queue_direction.x", o.m_queue_direction[0], Hashed::No);
    v.Float("building.queue_direction.y", o.m_queue_direction[1], Hashed::No);
    v.Float("building.queue_direction.z", o.m_queue_direction[2], Hashed::No);
    v.Float("building.top_world_pos.x", o.m_TopWorldPos[0], Hashed::No);
    v.Float("building.top_world_pos.y", o.m_TopWorldPos[1], Hashed::No);
    v.Float("building.top_world_pos.z", o.m_TopWorldPos[2], Hashed::No);
    v.Float("building.placed", o.m_Placed, Hashed::No);
    v.Int("building.lilos", o.m_NumLilos, Hashed::No);
    v.Int("building.boards", o.m_NumBoards, Hashed::No);
    v.Int("building.snorkels", o.m_NumSnorkels, Hashed::No);
    v.Int("building.current_capacity", o.m_CurCapacity, Hashed::Yes);
    v.Int("building.vehicle_count", o.m_NumVehicles, Hashed::No);
    v.Ints("building.vehicles", o.m_Vehicles, 6, Hashed::No);
    v.Bytes("building.vehicle_in_use", o.m_vehicleInUse, 6, Hashed::No);
    v.Float("building.last_message_time", o.m_LastMsgTime, Hashed::No);
    v.Float("building.lights_on", o.m_lightsOn, Hashed::No);
    v.Float("building.lights_off", o.m_lightsOff, Hashed::No);
    v.Bool("building.has_ball", o.m_hasBall, Hashed::No);
    v.Float("building.ball_position.x", o.m_BallPosition[0], Hashed::No);
    v.Float("building.ball_position.y", o.m_BallPosition[1], Hashed::No);
    v.Float("building.ball_position.z", o.m_BallPosition[2], Hashed::No);
    v.UInt("building.open_status", o.m_OpenStatus, Hashed::Yes);
    v.Int("building.guests", o.m_NumGuests, Hashed::Yes);
    v.Float("building.satisfaction", o.m_SatisfactionPercentage, Hashed::Yes);
    v.Floats("building.commodity_intensity_male", &o.m_Commod_Intensity[0][0], 5, Hashed::No);
    v.Floats("building.commodity_intensity_female", &o.m_Commod_Intensity[0][0] + 2, 5, Hashed::No);
    v.Floats("building.commodity_price", o.m_Commod_Price, 5, Hashed::No);
    v.Floats("building.commodity_price_factor", o.m_Commod_PriceFactor, 5, Hashed::No);
    v.Floats("building.commodity_cost", o.m_Commod_Cost, 5, Hashed::No);
    v.Float("building.beer_strength", o.m_BeerStrength, Hashed::Yes);
    v.Int("building.beer", o.m_BeerType, Hashed::Yes);
    v.Float("building.income_today", o.m_incomeToday, Hashed::Yes);
    v.Float("building.cost_today", o.m_costToday, Hashed::Yes);
    v.Float("building.profit_today", o.m_profitToday, Hashed::Yes);
    v.Float("building.average_profit", o.m_averageProfit, Hashed::Yes);
    v.Int("building.age", o.m_BuildingAge, Hashed::Yes);
    v.Floats("building.profit_history", o.m_profitRecord, 64, Hashed::No);
    v.Float("building.last_fault_day_time", o.m_lastFaultDayTime, Hashed::Yes);
    v.Int("building.last_fault_day", o.m_LastFaultDay, Hashed::Yes);
    v.Int("building.faults", o.m_NumFaults, Hashed::Yes);
    v.Int("building.user_repair_interval", o.m_UserRepairInterval, Hashed::Yes);
    v.Int("building.repair_interval", o.m_RepairInterval, Hashed::Yes);
    v.UInt("building.repair_state", o.m_RepairState, Hashed::Yes);
    v.Bool("building.was_broken", o.m_wasBroken, Hashed::Yes);
    v.Int("building.last_repair_call", o.m_LastRepairCall, Hashed::Yes);
    v.Float("building.last_repair_day_time", o.m_lastRepairDayTime, Hashed::Yes);
    v.Int("building.last_repair_day", o.m_LastRepairDay, Hashed::Yes);
    v.Int("building.visits_today", o.m_NumVisitsToday, Hashed::Yes);
    v.Int("building.visits_since_cleaned", o.m_VisitsSinceCleaned, Hashed::Yes);
    v.Int("building.last_fail_time", o.m_LastFailTime, Hashed::Yes);
    v.UInt("building.power_consumption", o.m_PowerConsumption, Hashed::Yes);
    v.Int("building.cleaned_today", o.m_NumTimesCleanedToday, Hashed::Yes);
    v.Bool("building.closed_for_power", o.m_closedForPower, Hashed::Yes);
    v.Bool("building.user_closed", o.m_userClose, Hashed::Yes);
    v.Bool("building.called_for_inspection", o.m_calledForInspection, Hashed::Yes);
    v.Bool("building.called_for_repair", o.m_calledForRepair, Hashed::Yes);
    v.Bool("building.built_by_user", o.m_builtByUser, Hashed::Yes);
    v.UInt("building.table_count", o.m_numTables, Hashed::Yes);
    v.Ints("building.tables", o.m_Tables, 10, Hashed::No);
    v.Bool("building.burn_limbo", o.m_burnLimbo, Hashed::No);
    v.UInt("building.screen_extents.left", o.m_rScrExtents.left, Hashed::No);
    v.UInt("building.screen_extents.top", o.m_rScrExtents.top, Hashed::No);
    v.UInt("building.screen_extents.right", o.m_rScrExtents.right, Hashed::No);
    v.UInt("building.screen_extents.bottom", o.m_rScrExtents.bottom, Hashed::No);
    v.UInt("building.screen_rect.left", o.m_rScrRect.left, Hashed::No);
    v.UInt("building.screen_rect.top", o.m_rScrRect.top, Hashed::No);
    v.UInt("building.screen_rect.right", o.m_rScrRect.right, Hashed::No);
    v.UInt("building.screen_rect.bottom", o.m_rScrRect.bottom, Hashed::No);
    v.Float("building.time_in_limbo", o.m_timeInLimbo, Hashed::No);
    v.UInt("building.shadow_offset", o.m_shadowOffset, Hashed::No);
    v.UInt("building.pick_offset", o.m_pickOffset, Hashed::No);
    v.UInt("building.light_map_offset", o.m_lightMapOffset, Hashed::No);
    v.Bool("building.sound_playing", o.m_soundPlaying, Hashed::No);
    v.UInt("building.sound_id", o.m_SoundID, Hashed::No);
    v.Sprites("building.sprites[]", o);
    v.Construction(o);
    v.Events("building.events", o);
    v.Thoughts("building.thoughts", o);
    v.Queue("building.queue", o, Hashed::Yes);
    v.UInt("building.lounger_count", o.m_numLoungers, Hashed::Yes);
    v.Ints("building.loungers", o.m_loungers, 20, Hashed::No);
    v.Int("building.influence_area", o.m_InfluenceArea, Hashed::Yes);
    v.Float("building.anim_time", o.m_buildingAnimTime, Hashed::No);
}

} // namespace obl::object
