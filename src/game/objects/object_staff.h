#pragma once

#include "core/pointer_width.h"
#include "game/tables/world_params_leaves.h"
#include "core/char16.h"
#include "game/objects/object_fields.h"
#include "game/objects/object_moving.h"

#include <cstdint>

// Staff (docs/architecture/objects.md): the state every staff profession
// shares (home building, wage and happiness, shift, the directed target, the three thoughts). The logic is staff_*.h.
// The staff constructor sets what staff_construction.h lists; every other field keeps the zero of its member initializer. The
// professions (staff_*.h) extend it.
namespace obl::ui {
struct SpriteInstance;
} // namespace obl::ui

namespace obl::map {
struct Area;
} // namespace obl::map

namespace obl::object {

struct Staff : Person {
    Staff() = default;
    // Staff's part of a staff member (staff_construct.cpp); Staff adds no teardown.
    explicit Staff(const char* name);
    // The slots whose function differs from what Person dispatches (game/staff_virtuals.cpp).
    bool Update() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void DrawOverlay() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    void ReduceSelectionToHover() override;
    void ClearSelection() override;
    // SetHome, GetFeedbackText, Sack and DirectToObject specialize staff behavior. SetHome stores m_HomeID (the service and snooty staff do more); the world's create functions call it right
    // after Teleport. DirectToObject is the professions' own; script function 171 of the staff CallFunction calls it with
    // m_GotoObject.
    virtual void SetHome(int32_t id);
    virtual const char* GetFeedbackText(uint32_t action);
    // The working hours (staff_duties.cpp); a shift past midnight has start > end.
    void SetShift(int32_t start, int32_t end);
    bool IsOnShift();
    virtual void Sack();
    virtual void DirectToObject(int32_t id);

    int32_t m_HomeID = 0; // building the staff member belongs to, -1 = none
    const tables::StaffDef* m_staffDefinition = nullptr; // Staff definition lookup record; not saved
    int32_t m_Wage = 0;
    float m_happiness = 0;
    float m_SalaryExpectation = 0;
    int32_t m_StartTime = 0;
    int32_t m_EndTime = 0;
    uint32_t m_hoursWorkedInRow = 0; // Can retain uninitialized heap after load; excluded from world-hash v2.
    uint32_t m_OverWorkReduct = 0;
    uint8_t m_foundDirecting = 0;
    int32_t m_LastAction = 0;
    uint32_t m_scale = 0; // float bits of the definition's m_scale; not saved
    float m_salaryExpectationGrowth = 0; // daily growth factor of the salary expectation; not saved
    int32_t m_walkpoint = 0;
    uint8_t m_DirectStaff = 0;
    int32_t m_GotoObject = 0;
    float m_DirectPos[2] = {}; // Save writes the label m_DirectPos twice (x, y)
    uint32_t m_spare = 0; // no known reader or writer; kept so the layout of the fields after it holds
    int32_t m_IdleTimePerHour = 0;
    int32_t m_HiredOn = 0;
    int32_t m_NumFeedBackMesslog = 0;
    Char16 m_Thoughts[3][256] = {};
    uint8_t m_gotTheSack = 0;
    uint8_t m_endedShift = 0;
    uint8_t m_gonePastShift = 0;
    float m_targetHappiness = 0; // the happiness target recalculation; not saved
    float m_happinessRate = 0; // not saved
    uint8_t m_happinessChanging = 0; // not saved
    uint8_t m_directedToObject = 0;
    uint8_t m_cantDoJob = 0;
    float m_WanderPos[3] = {};
    int32_t m_DebugInfo = 0;
};

// The staff Save records, then Person's (the caller). Visitor: Int/UInt/Float/Bool(key, field, hashed) and
// Wide(key, Char16* text, capacity, hashed). The snapshot hashes home, wage, happiness, salary expectation, shift times, hire
// day and the sack and shift-end flags.
template<SelfOf<Staff> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Int("staff.home", o.m_HomeID, Hashed::Yes);
    v.Int("staff.wage", o.m_Wage, Hashed::Yes);
    v.Int("staff.walk_point", o.m_walkpoint, Hashed::No);
    v.Float("staff.happiness", o.m_happiness, Hashed::Yes);
    v.Float("staff.salary_expectation", o.m_SalaryExpectation, Hashed::Yes);
    v.Int("staff.start_time", o.m_StartTime, Hashed::Yes);
    v.Int("staff.end_time", o.m_EndTime, Hashed::Yes);
    v.UInt("staff.hours_in_row", o.m_hoursWorkedInRow, Hashed::No);
    v.UInt("staff.overwork_reduction", o.m_OverWorkReduct, Hashed::No);
    v.Int("staff.idle_time_per_hour", o.m_IdleTimePerHour, Hashed::No);
    v.Int("staff.hired_on", o.m_HiredOn, Hashed::Yes);
    v.Bool("staff.directed", o.m_DirectStaff, Hashed::No);
    v.Int("staff.goto_object", o.m_GotoObject, Hashed::No);
    v.Int("staff.last_action", o.m_LastAction, Hashed::No);
    v.Float("staff.direct_pos.x", o.m_DirectPos[0], Hashed::No);
    v.Float("staff.direct_pos.y", o.m_DirectPos[1], Hashed::No);
    v.Bool("staff.found_directing", o.m_foundDirecting, Hashed::No);
    v.Int("staff.feedback_messages", o.m_NumFeedBackMesslog, Hashed::No);
    v.Bool("staff.cant_do_job", o.m_cantDoJob, Hashed::No);
    v.Float("staff.wander_pos.x", o.m_WanderPos[0], Hashed::No);
    v.Float("staff.wander_pos.y", o.m_WanderPos[1], Hashed::No);
    v.Float("staff.wander_pos.z", o.m_WanderPos[2], Hashed::No);
    v.Int("staff.debug_info", o.m_DebugInfo, Hashed::No);
    for (auto& thought : o.m_Thoughts) v.Wide("staff.thoughts[]", thought, 256, Hashed::No);
    v.Bool("staff.sacked", o.m_gotTheSack, Hashed::Yes);
    v.Bool("staff.ended_shift", o.m_endedShift, Hashed::Yes);
    v.Bool("staff.gone_past_shift", o.m_gonePastShift, Hashed::No);
}

// Type 6 (barman, cook, ... driven by the GuestLogic script).
struct ServiceStaff : Staff {
    ServiceStaff() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit ServiceStaff(const char* name);
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~ServiceStaff() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    void Draw() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    void SetHome(int32_t id) override;

    int32_t m_actionState = 0;
    // The `GuestLogic` script found by the constructor (not saved). Unlike the professions' it is not stored in the professions' script word.
    void* m_logic = nullptr;
};

// The service staff save record, then Staff's (the caller). Not hashed.
template<SelfOf<ServiceStaff> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Int("service.action_state", o.m_actionState, Hashed::No);
}

// Type 19 (docs/architecture/objects.md, the restaurant waiter): allocated as 0x77c, no Save of its own (so no field table); the word at 0x778 is
// unidentified.
struct SnootyStaff : Staff {
    SnootyStaff() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit SnootyStaff(const char* name);
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~SnootyStaff() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void SetHome(int32_t id) override;

    uint32_t m_jobSpare = 0;
};

// ---- Builder -----------------------------------------------------------------------------------------------------------------------

// Type 10, builder. The word after the staff fields is unidentified.
struct Builder : Staff {
    Builder() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit Builder(const char* name);
    void SetNumber(int32_t number); // staff_duties.cpp
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~Builder() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    const char* GetFeedbackText(uint32_t action) override;
    void Sack() override;
    void DirectToObject(int32_t id) override;

    uint32_t m_jobSpare = 0;
    int32_t m_NearestValidTileX = 0;
    int32_t m_NearestValidTileY = 0;
    int32_t m_NumTimesPlayedAnimInRow = 0;
    int32_t m_WaitTillMorning = 0;
    float m_lastTaskTime = 0;
    uint8_t m_checkForJobs = 0;
    int32_t m_ActionState = 0;
    int32_t m_Night = 0;
    int32_t m_InHut = 0;
    int32_t m_BuilderNumber = 0;
    float m_HomePoint[3] = {};
    int32_t m_BuilderLocation = 0;
    int32_t m_JobType = 0;
    int32_t m_LastStartTime = 0;
    int32_t m_CurrentFeedback = 0;
    int32_t m_FeedbackStatus = 0;
    int32_t m_RequiredFeedback = 0;
};

// The builder's Save records, then Staff's (the caller). Counter(key, hashed) is a class-wide global, not a member. The snapshot
// hashes the action state and the job type.
template<SelfOf<Builder> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Counter("builder.class_count", Hashed::No);
    v.Int("builder.nearest_valid_tile_x", o.m_NearestValidTileX, Hashed::No);
    v.Int("builder.nearest_valid_tile_y", o.m_NearestValidTileY, Hashed::No);
    v.Counter("builder.whistling", Hashed::No);
    v.Int("builder.anim_repeats", o.m_NumTimesPlayedAnimInRow, Hashed::No);
    v.Float("builder.last_task_time", o.m_lastTaskTime, Hashed::No);
    v.Int("builder.number", o.m_BuilderNumber, Hashed::No);
    v.Float("builder.home_point.x", o.m_HomePoint[0], Hashed::No);
    v.Float("builder.home_point.y", o.m_HomePoint[1], Hashed::No);
    v.Float("builder.home_point.z", o.m_HomePoint[2], Hashed::No);
    v.Int("builder.action_state", o.m_ActionState, Hashed::Yes);
    v.Int("builder.night", o.m_Night, Hashed::No);
    v.Int("builder.in_hut", o.m_InHut, Hashed::No);
    v.Int("builder.current_feedback", o.m_CurrentFeedback, Hashed::No);
    v.Int("builder.feedback_status", o.m_FeedbackStatus, Hashed::No);
    v.Int("builder.required_feedback", o.m_RequiredFeedback, Hashed::No);
    v.Int("builder.location", o.m_BuilderLocation, Hashed::No);
    v.Int("builder.job_type", o.m_JobType, Hashed::Yes);
    v.Int("builder.last_start_time", o.m_LastStartTime, Hashed::No);
    v.Bool("builder.check_for_jobs", o.m_checkForJobs, Hashed::No);
    v.Int("builder.wait_till_morning", o.m_WaitTillMorning, Hashed::No);
}

// ---- Cleaner -----------------------------------------------------------------------------------------------------------------------

// Type 11, cleaner. m_bClean* are the task categories the player enabled (not skills). The words at 0x778 and
// 0x7a8 are unidentified.
struct Cleaner : Staff {
    Cleaner() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit Cleaner(const char* name);
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~Cleaner() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    const char* GetFeedbackText(uint32_t action) override;
    void Sack() override;
    void DirectToObject(int32_t id) override;

    uint32_t m_jobSpare = 0;
    uint8_t m_litterFound = 0;
    uint8_t m_toiletFound = 0;
    uint8_t m_binFound = 0;
    uint8_t m_cleanToilets = 0;
    uint8_t m_cleanLitter = 0;
    uint8_t m_cleanBins = 0;
    float m_lastTaskTime = 0;
    int32_t m_LitterDistance = 0;
    int32_t m_ToiletsToday = 0;
    int32_t m_BinsToday = 0;
    int32_t m_LitterToday = 0;
    int32_t m_BusyToilet = 0;
    int32_t m_FeedbackStatus = 0;
    int32_t m_RequiredFeedback = 0;
    int32_t m_CurrentFeedback = 0;
    uint32_t m_feedbackSpare[2] = {};
    float m_BinPos[3] = {};
    int32_t m_ActionState = 0;
    int32_t m_UrgentType = 0;
    uint8_t m_urgentClean = 0;
};

// The cleaner's Save records, then Staff's (the caller). The snapshot hashes the action state.
template<SelfOf<Cleaner> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Counter("cleaner.class_count", Hashed::No);
    v.Bool("cleaner.litter_found", o.m_litterFound, Hashed::No);
    v.Bool("cleaner.toilet_found", o.m_toiletFound, Hashed::No);
    v.Bool("cleaner.bin_found", o.m_binFound, Hashed::No);
    v.Bool("cleaner.clean_toilets", o.m_cleanToilets, Hashed::No);
    v.Bool("cleaner.clean_litter", o.m_cleanLitter, Hashed::No);
    v.Bool("cleaner.clean_bins", o.m_cleanBins, Hashed::No);
    v.Float("cleaner.last_task_time", o.m_lastTaskTime, Hashed::No);
    v.Int("cleaner.litter_distance", o.m_LitterDistance, Hashed::No);
    v.Int("cleaner.toilets_today", o.m_ToiletsToday, Hashed::No);
    v.Int("cleaner.bins_today", o.m_BinsToday, Hashed::No);
    v.Int("cleaner.litter_today", o.m_LitterToday, Hashed::No);
    v.Int("cleaner.busy_toilet", o.m_BusyToilet, Hashed::No);
    v.Int("cleaner.feedback_status", o.m_FeedbackStatus, Hashed::No);
    v.Int("cleaner.required_feedback", o.m_RequiredFeedback, Hashed::No);
    v.Int("cleaner.current_feedback", o.m_CurrentFeedback, Hashed::No);
    v.Float("cleaner.bin_pos.x", o.m_BinPos[0], Hashed::No);
    v.Float("cleaner.bin_pos.y", o.m_BinPos[1], Hashed::No);
    v.Float("cleaner.bin_pos.z", o.m_BinPos[2], Hashed::No);
    v.Int("cleaner.action_state", o.m_ActionState, Hashed::Yes);
    v.Int("cleaner.urgent_type", o.m_UrgentType, Hashed::No);
    v.Bool("cleaner.urgent_clean", o.m_urgentClean, Hashed::No);
}

// ---- Lifeguard ---------------------------------------------------------------------------------------------------------------------

// Type 7, lifeguard. The two words at 0x808 are unidentified.
struct Lifeguard : Staff {
    Lifeguard() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit Lifeguard(const char* name);
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~Lifeguard() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void Draw() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    const char* GetFeedbackText(uint32_t action) override;
    void Sack() override;
    void DirectToObject(int32_t id) override;

    uint8_t m_saving = 0;
    uint8_t m_RescueThenDeath = 0;
    int32_t m_saving_id = 0; // guest being rescued
    const map::Area* m_beachArea = nullptr; // an area of the grid, rebuilt from m_BeachArea on Load; not serialized
    int32_t m_BeachArea = 0;
    float m_ExitPoint[3] = {};
    char m_ReturnAnimation[40] = {};
    char m_CurrAnimation[40] = {};
    int32_t m_LeaveState = 0;
    uint8_t m_StopAction = 0;
    int32_t m_ActionState = 0;
    void* m_attachedSprite = nullptr; // SpriteInstance* (Save label "attached sprite" = presence flag)
    int32_t m_AttachedOffsetX = 0;
    int32_t m_AttachedOffsetY = 0;
    int32_t m_FeedbackStatus = 0;
    int32_t m_RequiredFeedback = 0;
    int32_t m_CurrentFeedback = 0;
    uint32_t m_feedbackSpare[2] = {};
    int32_t m_NumSaves = 0;
    int32_t m_NumDrowned = 0;
    uint8_t m_GuestDrowned = 0;
    int32_t m_timesCalledOut = 0;
    int32_t m_timesPathBlocked = 0;
    int32_t m_InfluenceArea = 0; // copied from the home building in state 1
    uint8_t m_inMyArea = 0;
    uint8_t m_inSea = 0;
    uint8_t m_enteringSea = 0;
    uint8_t m_AtHotel = 0;
    uint8_t m_DrawGuest = 0;
};

// The lifeguard's Save records, then Staff's (the caller). Text(key, char* text, capacity, hashed); BeachArea(area,
// index) writes nothing and rebuilds the area pointer on Load; Attached(key, sprite, hashed) is the presence flag and the attached
// sprite's state. The snapshot hashes the action state and the influence area.
template<SelfOf<Lifeguard> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Bool("lifeguard.saving", o.m_saving, Hashed::No);
    v.Bool("lifeguard.rescue_then_death", o.m_RescueThenDeath, Hashed::No);
    v.Int("lifeguard.saving_guest", o.m_saving_id, Hashed::No);
    v.Int("lifeguard.beach_area", o.m_BeachArea, Hashed::No);
    v.BeachArea(o.m_beachArea, o.m_BeachArea);
    v.Float("lifeguard.exit_point.x", o.m_ExitPoint[0], Hashed::No);
    v.Float("lifeguard.exit_point.y", o.m_ExitPoint[1], Hashed::No);
    v.Float("lifeguard.exit_point.z", o.m_ExitPoint[2], Hashed::No);
    v.Text("lifeguard.return_animation", o.m_ReturnAnimation, 40, Hashed::No);
    v.Text("lifeguard.current_animation", o.m_CurrAnimation, 40, Hashed::No);
    v.Int("lifeguard.leave_state", o.m_LeaveState, Hashed::No);
    v.Bool("lifeguard.stop_action", o.m_StopAction, Hashed::No);
    v.Int("lifeguard.action_state", o.m_ActionState, Hashed::Yes);
    v.Attached("lifeguard.attached", o.m_attachedSprite, Hashed::No);
    v.Int("lifeguard.attached_offset_x", o.m_AttachedOffsetX, Hashed::No);
    v.Int("lifeguard.attached_offset_y", o.m_AttachedOffsetY, Hashed::No);
    v.Int("lifeguard.feedback_status", o.m_FeedbackStatus, Hashed::No);
    v.Int("lifeguard.required_feedback", o.m_RequiredFeedback, Hashed::No);
    v.Int("lifeguard.current_feedback", o.m_CurrentFeedback, Hashed::No);
    v.Int("lifeguard.saves", o.m_NumSaves, Hashed::No);
    v.Int("lifeguard.drowned", o.m_NumDrowned, Hashed::No);
    v.Bool("lifeguard.guest_drowned", o.m_GuestDrowned, Hashed::No);
    v.Int("lifeguard.times_called_out", o.m_timesCalledOut, Hashed::No);
    v.Int("lifeguard.times_path_blocked", o.m_timesPathBlocked, Hashed::No);
    v.Int("lifeguard.influence_area", o.m_InfluenceArea, Hashed::Yes);
    v.Bool("lifeguard.in_my_area", o.m_inMyArea, Hashed::No);
    v.Bool("lifeguard.in_sea", o.m_inSea, Hashed::No);
    v.Bool("lifeguard.entering_sea", o.m_enteringSea, Hashed::No);
    v.Bool("lifeguard.at_hotel", o.m_AtHotel, Hashed::No);
    v.Bool("lifeguard.draw_guest", o.m_DrawGuest, Hashed::No);
}

// ---- Mechanic ----------------------------------------------------------------------------------------------------------------------

// Type 8, mechanic. The job mask has no label in Save: it is written as three booleans (breakdown,
// vandalism, watering) derived from bits 0..2 and read back into the same bits.
struct Mechanic : Staff {
    Mechanic() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit Mechanic(const char* name);
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~Mechanic() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    const char* GetFeedbackText(uint32_t action) override;
    void Sack() override;
    void DirectToObject(int32_t id) override;

    uint32_t m_JobMask = 0; // bit 0 repairs, 1 vandalism, 2 watering (MECHANIC_REPAIR/VANDALS/WATER)
    uint32_t m_jobSpare = 0;
    uint8_t m_foundRepair = 0;
    uint8_t m_foundBetterJob = 0;
    float m_lastTaskTime = 0;
    int32_t m_FeedbackStatus = 0;
    int32_t m_RequiredFeedback = 0;
    int32_t m_CurrentFeedback = 0;
    uint32_t m_feedbackSpare[2] = {};
    int32_t m_ActionState = 0;
    int32_t m_repairType = 0;
    int32_t m_inspectionsToday = 0;
    int32_t m_repairsToday = 0;
    int32_t m_vandalsToday = 0;
    int32_t m_wateredToday = 0;
    float m_WateringPos[3] = {};
};

// The mechanic's Save records, then Staff's (the caller). Visitor as Staff's plus Counter(key, hashed) for the
// class-wide count (a global, not a member) and MaskBit(key, mask, bit, hashed). Nothing is hashed: only the script writes
// m_ActionState, so a fresh mechanic holds heap garbage there.
template<SelfOf<Mechanic> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Counter("mechanic.class_count", Hashed::No);
    v.MaskBit("mechanic.vandalism", o.m_JobMask, 1, Hashed::No);
    v.MaskBit("mechanic.breakdown", o.m_JobMask, 0, Hashed::No);
    v.MaskBit("mechanic.watering", o.m_JobMask, 2, Hashed::No);
    v.Bool("mechanic.found_repair", o.m_foundRepair, Hashed::No);
    v.Bool("mechanic.found_better_job", o.m_foundBetterJob, Hashed::No);
    v.Float("mechanic.last_task_time", o.m_lastTaskTime, Hashed::No);
    v.Int("mechanic.feedback_status", o.m_FeedbackStatus, Hashed::No);
    v.Int("mechanic.required_feedback", o.m_RequiredFeedback, Hashed::No);
    v.Int("mechanic.current_feedback", o.m_CurrentFeedback, Hashed::No);
    v.Int("mechanic.repair_type", o.m_repairType, Hashed::No);
    v.Float("mechanic.watering_pos.x", o.m_WateringPos[0], Hashed::No);
    v.Float("mechanic.watering_pos.y", o.m_WateringPos[1], Hashed::No);
    v.Float("mechanic.watering_pos.z", o.m_WateringPos[2], Hashed::No);
    v.Int("mechanic.inspections_today", o.m_inspectionsToday, Hashed::No);
    v.Int("mechanic.repairs_today", o.m_repairsToday, Hashed::No);
    v.Int("mechanic.vandals_today", o.m_vandalsToday, Hashed::No);
    v.Int("mechanic.watered_today", o.m_wateredToday, Hashed::No);
    v.Int("mechanic.action_state", o.m_ActionState, Hashed::No);
}

// ---- Rep ---------------------------------------------------------------------------------------------------------------------------

// Type 9, rep.
struct Rep : Staff {
    Rep() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit Rep(const char* name);
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~Rep() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void Draw() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    const char* GetFeedbackText(uint32_t action) override;
    void Sack() override;
    void DirectToObject(int32_t id) override;

    int32_t m_talking_id = 0;
    int32_t m_counters[7][5] = {}; // Save label "counters", written as seven int[5]
    char m_ReturnAnimation[40] = {};
    char m_CurrAnimation[40] = {};
    int32_t m_LeaveState = 0;
    uint8_t m_StopAction = 0;
    int32_t m_ActionState = 0;
    void* m_attachedSprite = nullptr; // SpriteInstance* (Save label "attached sprite" = presence flag)
    int32_t m_AttachedOffsetX = 0;
    int32_t m_AttachedOffsetY = 0;
    int32_t m_FeedbackStatus = 0;
    int32_t m_RequiredFeedback = 0;
    int32_t m_CurrentFeedback = 0;
    uint32_t m_feedbackSpare[2] = {};
    int32_t m_GuestsTalkedToToday = 0;
    int32_t m_MinTalkTime = 0;
    int32_t m_MaxTalkTime = 0;
    int32_t m_happinessBoostToday = 0; // float bits: the guest time credited today (MT_NEW_DAY zeroes it); shown as the happiness boost
    float m_timeWithGuests = 0;
    uint8_t m_sentToGuest = 0;
    float m_GuestOffSetPosX = 0;
    float m_GuestOffSetPosY = 0;
    float m_GuestOffSetPosZ = 0; // not saved
    int32_t m_GuestOpinionValue = 0;
};

// The rep's Save records, then Staff's. Visitor as the mechanic's plus Ints(key, values, count, hashed) (one entry per
// counter row), Text(key, char* text, capacity, hashed) and Attached(key, sprite pointer, hashed) for the script's
// sprite (presence flag, then name and sprite state). The snapshot hashes m_ActionState.
template<SelfOf<Rep> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Counter("rep.class_count", Hashed::No);
    v.Int("rep.talking_guest", o.m_talking_id, Hashed::No);
    for (auto& row : o.m_counters) v.Ints("rep.counters[]", row, 5, Hashed::No);
    v.Text("rep.return_animation", o.m_ReturnAnimation, 40, Hashed::No);
    v.Text("rep.current_animation", o.m_CurrAnimation, 40, Hashed::No);
    v.Int("rep.leave_state", o.m_LeaveState, Hashed::No);
    v.Bool("rep.stop_action", o.m_StopAction, Hashed::No);
    v.Int("rep.action_state", o.m_ActionState, Hashed::Yes);
    v.Attached("rep.attached", o.m_attachedSprite, Hashed::No);
    v.Int("rep.attached_offset_x", o.m_AttachedOffsetX, Hashed::No);
    v.Int("rep.attached_offset_y", o.m_AttachedOffsetY, Hashed::No);
    v.Int("rep.feedback_status", o.m_FeedbackStatus, Hashed::No);
    v.Int("rep.required_feedback", o.m_RequiredFeedback, Hashed::No);
    v.Int("rep.current_feedback", o.m_CurrentFeedback, Hashed::No);
    v.Int("rep.guests_talked_to_today", o.m_GuestsTalkedToToday, Hashed::No);
    v.Int("rep.min_talk_time", o.m_MinTalkTime, Hashed::No);
    v.Int("rep.max_talk_time", o.m_MaxTalkTime, Hashed::No);
    v.Float("rep.time_with_guests", o.m_timeWithGuests, Hashed::No);
    v.Bool("rep.sent_to_guest", o.m_sentToGuest, Hashed::No);
    v.Float("rep.guest_offset_y", o.m_GuestOffSetPosY, Hashed::No);
    v.Float("rep.guest_offset_x", o.m_GuestOffSetPosX, Hashed::No);
    v.Int("rep.guest_opinion_value", o.m_GuestOpinionValue, Hashed::No);
}

// ---- SecurityGuard -----------------------------------------------------------------------------------------------------------------

// Type 12, security guard. The Save label m_FineOffsetY is used for both m_FineOffsetY and m_FineOffsetY_7f0
// (the converter names the second one the fine offset's z).
struct SecurityGuard : Staff {
    SecurityGuard() = default;
    // Builds the staff member of the given kind (staff_construct.cpp).
    explicit SecurityGuard(const char* name);
    // The slots whose function differs from Staff's (game/staff_virtuals.cpp).
    ~SecurityGuard() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    const char* GetFeedbackText(uint32_t action) override;
    void Sack() override;
    void DirectToObject(int32_t id) override;

    uint32_t m_jobSpare = 0;
    int32_t m_offenderId = 0;
    int32_t m_fightWithId = 0;
    float m_tolerance = 0;
    int32_t m_toleranceClass = 0;
    int32_t m_severity = 0;
    int32_t m_action = 0;
    uint8_t m_doubleFine = 0;
    uint8_t m_misdemeanourFound = 0;
    uint8_t m_shouldReprimand = 0;
    uint8_t m_shouldDetain = 0;
    uint8_t m_NotEnoughBins = 0;
    float m_MisdemeanourPos[3] = {};
    int32_t m_numMinorFinesToday = 0;
    int32_t m_numSeriousFinesToday = 0;
    int32_t m_numVandals = 0;
    int32_t m_numFights = 0;
    int32_t m_numJailed = 0;
    int32_t m_numCautioned = 0;
    int32_t m_FeedbackStatus = 0;
    int32_t m_RequiredFeedback = 0;
    int32_t m_CurrentFeedback = 0;
    uint32_t m_feedbackSpare[2] = {};
    int32_t m_ActionState = 0;
    float m_minorFineAmount = 0;
    float m_seriousFineAmount = 0;
    float m_fineTotalToday = 0;
    int32_t m_FineLevel = 0;
    float m_FineOffsetX = 0;
    float m_FineOffsetY = 0;
    float m_FineOffsetY_7f0 = 0;
};

// The security guard's Save records, then Staff's. Visitor as the mechanic's; the snapshot hashes m_ActionState.
template<SelfOf<SecurityGuard> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Counter("security.class_count", Hashed::No);
    v.Int("security.offender", o.m_offenderId, Hashed::No);
    v.Int("security.fight_with", o.m_fightWithId, Hashed::No);
    v.Float("security.tolerance", o.m_tolerance, Hashed::No);
    v.UInt("security.tolerance_level", o.m_toleranceClass, Hashed::No);
    v.UInt("security.severity", o.m_severity, Hashed::No);
    v.UInt("security.action", o.m_action, Hashed::No);
    v.Bool("security.double_fine", o.m_doubleFine, Hashed::No);
    v.Bool("security.misdemeanour_found", o.m_misdemeanourFound, Hashed::No);
    v.Bool("security.should_reprimand", o.m_shouldReprimand, Hashed::No);
    v.Bool("security.should_detain", o.m_shouldDetain, Hashed::No);
    v.Bool("security.not_enough_bins", o.m_NotEnoughBins, Hashed::No);
    v.Float("security.misdemeanour_pos.x", o.m_MisdemeanourPos[0], Hashed::No);
    v.Float("security.misdemeanour_pos.y", o.m_MisdemeanourPos[1], Hashed::No);
    v.Float("security.misdemeanour_pos.z", o.m_MisdemeanourPos[2], Hashed::No);
    v.UInt("security.minor_fines_today", o.m_numMinorFinesToday, Hashed::No);
    v.UInt("security.serious_fines_today", o.m_numSeriousFinesToday, Hashed::No);
    v.UInt("security.vandals", o.m_numVandals, Hashed::No);
    v.UInt("security.fights", o.m_numFights, Hashed::No);
    v.UInt("security.jailed", o.m_numJailed, Hashed::No);
    v.Int("security.feedback_status", o.m_FeedbackStatus, Hashed::No);
    v.Int("security.required_feedback", o.m_RequiredFeedback, Hashed::No);
    v.Int("security.current_feedback", o.m_CurrentFeedback, Hashed::No);
    v.Int("security.action_state", o.m_ActionState, Hashed::Yes);
    v.Float("security.minor_fine_amount", o.m_minorFineAmount, Hashed::No);
    v.Float("security.serious_fine_amount", o.m_seriousFineAmount, Hashed::No);
    v.Float("security.fine_total_today", o.m_fineTotalToday, Hashed::No);
    v.Float("security.fine_offset.x", o.m_FineOffsetX, Hashed::No);
    v.Float("security.fine_offset.y", o.m_FineOffsetY, Hashed::No);
    v.Float("security.fine_offset.z", o.m_FineOffsetY_7f0, Hashed::No);
    v.Int("security.fine_level", o.m_FineLevel, Hashed::No);
    v.UInt("security.cautioned", o.m_numCautioned, Hashed::No);
}

// The script of a staff member ("<Profession>Logic": the constructors of the professions store it) is the staff script word, where a guest keeps its
// definition (`Person::m_definition`); `Person::m_script` is the guest's. The professions' Update and the staff
// ReceiveMessage read it from there.
inline script::ScriptProgram* StaffScript(const object::Staff& staff)
{
    return reinterpret_cast<script::ScriptProgram*>(reinterpret_cast<uintptr_t>(staff.m_definition));
}

// Faces a sprite in a direction, the last one when the animation has fewer (staff_duties.cpp).
void FaceDirection(ui::SpriteInstance& sprite, int32_t direction);
// Walks a cleaner to its side of bin `binId`: moves `point` and posts the walk (staff_duties.cpp).
void SendCleanerToBin(int32_t binId, Base& cleaner, float* point);

} // namespace obl::object
