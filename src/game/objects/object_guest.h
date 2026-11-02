#pragma once

#include "core/char16.h"
#include "game/objects/object_fields.h"
#include "game/objects/object_moving.h"

#include <cstdint>

namespace obl::ui {
struct SpriteInstance;
}

// Guest (type 3; docs/architecture/objects.md): the guest on the C++ Mover
// and Person. Names m_* are the save labels; fields that no Save writes carry our own names. The constructor sets what
// guest_construction.h lists; every other field keeps the zero of its member initializer.
namespace obl::object {

// Saved by the guest's Save (Load). Script variables (docs/architecture/objects.md) are the same fields.
struct Guest : Person {
    Guest() = default;
    // Builds the guest from its create record (guest_lifecycle_driver.cpp).
    explicit Guest(const char* params);
    ~Guest() override;
    // The slots whose function differs from Person's (the game/guest_*.cpp files).
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void Draw() override;
    void DrawOverlay() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    void ReduceSelectionToHover() override;
    void ClearSelection() override;
    // The guest's own entries: the delegated script ids (guest_script.cpp) and the desire-trace seam targets (guest.cpp).
    // Each reads the guest through `this`; the seam forwards to the trace's previous handler when one is installed.
    template<auto Seam> void RunThroughSeam();
    void UpdateDesires();
    void UpdateOpinions();
    void DecideSwimAction();
    void UpdateDrowning();
    void SendOutDeathMessage();
    bool StartStomachIllness();
    void RandomizePalette();
    void RandomizeAttachedPalette();
    void SetRepeatAction(int desire, float value);
    float* FindSunbathePoint();
    void FindFightPoint();
    void FindFightPerson();
    void TellNearbyOfFight();
    void TellNearbyOfTopless();
    void LookForVandalItem();
    void VandaliseBin();
    void AlterDesires();
    void SetupStartValues();
    void MissedDesire();
    void LookForIllPoint();
    void LookForBench();
    void QueueAtComplaints();
    void QueueAtWharf();
    void CreateLitter();
    void GetConsumePoint();
    void FindFlirtTarget();
    void MoveActionPerson();
    void AlterQueueOpinion();
    void AlterSafetyOpinion();
    void UpdatePriceScore();
    void AlterPriceOpinion();
    void UpdatePurchaseOpinion();
    void ReactToSpottedEvent();
    bool CheckBedTime();
    bool CheckLeaveResort();
    void SetupSnogOffset();
    void FaceEachOther();
    void FindLodging();
    void SetSurfacePoint();
    void GetNextPoolPoint();
    void ChooseFunSlideAction();
    void SetupFunPoolSwim();
    void SearchDesire();
    void AddIllPurchaseThought();
    void AddCommodBeachClosed();
    void AddThoughtIDidnt();
    void AddThoughtIDidIt();
    void NoRouteToLodging();
    void AddBuildingFullThought();
    void AddDrowningThought();
    void DemolishedAsleep();
    void GenerateBlockedMessage();
    void StartPathWalk();
    void SurfToShore();
    void WindSurfStraight();
    void GotoDancePoint();
    void WalkAlongPath();
    void HeadForEntryPoint();
    void GetSeaEntry();
    void GotoWorldUnloadPoint();
    void WalkToWindsurf();
    void HeadForBin();
    void WanderBeach();

    float m_Tolerance[14] = {}; // seven (min, max) pairs: beauty, crowding, totty, price, variety, cleanliness, safety
    float m_person[13] = {}; // desire growth rates; [12] = SLEEPPREF (never read)
    float m_desires[26] = {}; // Desire... script variables, order in docs/architecture/objects.md
    float m_opinion[7] = {}; // beauty, crowding, totty, price, variety, cleanliness, safety (0..1)
    int32_t m_rides_last[5] = {};
    int32_t m_rides_start = 0;
    int32_t m_rides_end = 0;
    int32_t m_invent_slot[5] = {};
    int32_t m_desired_commod = 0;
    int32_t m_VehicleID = 0;
    uint8_t m_activity = 0;
    uint8_t m_StopAction = 0;
    float m_walkpoints[3][3] = {}; // (x, y, z) x 3
    int32_t m_walkpoint = 0;
    float m_drunk = 0;
    int32_t m_oxygen = 0; // drowning countdown, 100 on leaving the state
    float m_cash = 0;
    Char16* m_name = nullptr; // heap string
    float m_happiness = 0;
    char m_WalkAnimation[40] = {};
    char m_StandAnimation[40] = {};
    uint8_t m_Fidget = 0;
    char m_CurrAnimation[40] = {};
    char m_ReturnAnimation[40] = {};
    int32_t m_LeaveState = 0;
    void* m_attachedSprite = nullptr; // SpriteInstance* (Save label "attached_sprite" = presence flag)
    int32_t m_EventID = 0;
    int32_t m_ActionState = 0;
    int32_t m_RepeatAction = 0;
    int32_t m_AttachedOffsetX = 0;
    int32_t m_AttachedOffsetY = 0;
    int32_t m_FightPerson = 0;
    int32_t m_ActivityPerson = 0;
    uint8_t m_justLittered = 0;
    float m_timeLittered = 0;
    int32_t m_StayingAt = 0; // hotel the guest sleeps in, -1 = none
    float m_WakeTime = 0;
    float m_BedTime = 0;
    int32_t m_NightsToStay = 0;
    float m_TimeToLeave = 0;
    float m_sunBurnLevel = 0;
    int32_t m_AmountLoition = 0; // sic (reference label)
    int32_t m_DayArrived = 0; // Save writes the label twice, both for this offset
    float m_timeArrived = 0;
    uint8_t m_AppliedLotion = 0;
    float m_foodIll = 0;
    uint8_t m_imIll = 0;
    uint8_t m_BestSpotOpinion = 0;
    int32_t m_Best = 0;
    int32_t m_BestType = 0;
    float m_bestScore = 0;
    uint8_t m_WorstSpotOpinion = 0;
    int32_t m_Worst = 0; // not saved; mirrors m_Best
    int32_t m_WorstType = 0;
    float m_worstScore = 0;
    uint8_t m_lastGood = 0;
    uint8_t m_lastBad = 0;
    int32_t m_CurID = 0;
    int32_t m_CurTypeID = 0;
    float m_curScore = 0;
    float m_priceScore = 0;
    float m_queueScore = 0;
    float m_safetyScore = 0;
    float m_cleanScore = 0;
    uint8_t m_missedPurchase = 0;
    int32_t m_LastID[2] = {};
    uint8_t m_lastActivity[2] = {};
    int32_t m_ArrayPos = 0;
    uint8_t m_HasSatDown = 0;
    void* m_happinessSprite = nullptr; // UI_happiness: DrawStatBar/DrawSelectionMarker set the mood frame; not saved
    ui::SpriteInstance* m_moodIcon = nullptr; // thought bubble, frame 1 sad, 0 happy; not saved
    float m_moodIconTime = 0; // set to 2.0 on a spot opinion; not saved
    uint8_t m_leavingEarly = 0;
    uint8_t m_topless = 0; // script variable Topless (guest GetValue/SetValue 0x5c); not saved
    uint8_t m_souvenirsDecided = 0; // CheckLeaveResort: souvenirs requested; not saved
    uint8_t m_missedDesireLast = 0; // copy of m_missedDesire taken at the start of SearchDesire; not saved
    uint8_t m_missedDesire = 0; // MissedDesire: the chosen desire failed once; SearchDesire clears it when it picks a new one; not saved
    int32_t m_NumTimesBlocked = 0;
    uint32_t m_OffenceState = 0;
    float m_timeOfOffence = 0;
    float m_lastUpdateHappened = 0;
    int32_t m_Thoughts[3] = {}; // text keys, [0] newest
};

// The guest's save records in the one order Save and Load share, then Person's (the caller). Two oddities of the format are kept:
// `m_DayArrived` is written twice (at its place and again after `m_AmountLoition`) and `m_TimeToLeave` follows `m_timeArrived`. Visitor:
// Int/UInt/Float/Bool(key, field, hashed), Floats/Ints/Bytes(key, values, count, hashed), Text(key, char*, capacity,
// hashed), Name(key, wchar_t*& heap string, hashed) and Attached(key, sprite, hashed). The snapshot hashes the four arrays,
// the vehicle, activity, drink, oxygen, cash, happiness, action state, the hotel stay and arrival records, sunburn and food illness.
template<SelfOf<Guest> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Floats("guest.tolerance", o.m_Tolerance, 14, Hashed::Yes);
    v.Floats("guest.person", o.m_person, 13, Hashed::Yes);
    v.Floats("guest.desires", o.m_desires, 26, Hashed::Yes);
    v.Floats("guest.opinion", o.m_opinion, 7, Hashed::Yes);
    v.Ints("guest.rides.last", o.m_rides_last, 5, Hashed::No);
    v.Int("guest.rides.start", o.m_rides_start, Hashed::No);
    v.Int("guest.rides.end", o.m_rides_end, Hashed::No);
    v.Ints("guest.inventory", o.m_invent_slot, 5, Hashed::No);
    v.Int("guest.desired_commodity_slot", o.m_desired_commod, Hashed::No);
    // Normalize sets it to -1 unless it names a vehicle of the snapshot (a guest without one keeps allocator bytes).
    v.Int("guest.vehicle", o.m_VehicleID, Hashed::Yes);
    v.Bool("guest.activity", o.m_activity, Hashed::Yes);
    v.Bool("guest.stop_action", o.m_StopAction, Hashed::No);
    for (auto& point : o.m_walkpoints) {
        v.Float("guest.walk_points[].x", point[0], Hashed::No);
        v.Float("guest.walk_points[].y", point[1], Hashed::No);
        v.Float("guest.walk_points[].z", point[2], Hashed::No);
    }
    v.Int("guest.walk_point", o.m_walkpoint, Hashed::No);
    v.Float("guest.drunk", o.m_drunk, Hashed::Yes);
    v.Int("guest.oxygen", o.m_oxygen, Hashed::Yes);
    v.Float("guest.cash", o.m_cash, Hashed::Yes);
    v.Name("guest.name", o.m_name, Hashed::No);
    v.Float("guest.happiness", o.m_happiness, Hashed::Yes);
    v.Text("guest.walk_animation", o.m_WalkAnimation, 40, Hashed::No);
    v.Text("guest.stand_animation", o.m_StandAnimation, 40, Hashed::No);
    v.Bool("guest.fidget", o.m_Fidget, Hashed::No);
    v.Text("guest.current_animation", o.m_CurrAnimation, 40, Hashed::No);
    v.Text("guest.return_animation", o.m_ReturnAnimation, 40, Hashed::No);
    v.Int("guest.leave_state", o.m_LeaveState, Hashed::No);
    v.Attached("guest.attached", o.m_attachedSprite, Hashed::No);
    v.Int("guest.event", o.m_EventID, Hashed::No);
    v.Int("guest.action_state", o.m_ActionState, Hashed::Yes);
    v.Int("guest.repeat_action", o.m_RepeatAction, Hashed::No);
    v.Int("guest.attached_offset_x", o.m_AttachedOffsetX, Hashed::No);
    v.Int("guest.attached_offset_y", o.m_AttachedOffsetY, Hashed::No);
    v.Int("guest.fight_person", o.m_FightPerson, Hashed::No);
    v.Int("guest.activity_person", o.m_ActivityPerson, Hashed::No);
    v.Bool("guest.just_littered", o.m_justLittered, Hashed::No);
    v.Float("guest.time_littered", o.m_timeLittered, Hashed::No);
    v.Int("guest.staying_at", o.m_StayingAt, Hashed::Yes);
    v.Float("guest.wake_time", o.m_WakeTime, Hashed::Yes);
    v.Float("guest.bed_time", o.m_BedTime, Hashed::Yes);
    v.Int("guest.nights_to_stay", o.m_NightsToStay, Hashed::Yes);
    v.Int("guest.day_arrived", o.m_DayArrived, Hashed::Yes);
    v.Float("guest.time_arrived", o.m_timeArrived, Hashed::Yes);
    v.Float("guest.time_to_leave", o.m_TimeToLeave, Hashed::Yes);
    v.Ints("guest.last_ids", o.m_LastID, 2, Hashed::No);
    v.Bytes("guest.last_activity", o.m_lastActivity, 2, Hashed::No);
    v.Int("guest.array_pos", o.m_ArrayPos, Hashed::No);
    v.Float("guest.sunburn", o.m_sunBurnLevel, Hashed::Yes);
    v.Int("guest.lotion", o.m_AmountLoition, Hashed::No);
    v.Int("guest.day_arrived", o.m_DayArrived, Hashed::Yes); // the converter checks the copy and drops it
    v.Bool("guest.applied_lotion", o.m_AppliedLotion, Hashed::No);
    v.Bool("guest.best_spot_opinion", o.m_BestSpotOpinion, Hashed::No);
    v.Int("guest.best", o.m_Best, Hashed::No);
    v.Int("guest.best_type", o.m_BestType, Hashed::No);
    v.Float("guest.best_score", o.m_bestScore, Hashed::No);
    v.Bool("guest.worst_spot_opinion", o.m_WorstSpotOpinion, Hashed::No);
    v.Int("guest.worst_type", o.m_WorstType, Hashed::No);
    v.Float("guest.worst_score", o.m_worstScore, Hashed::No);
    v.Int("guest.current", o.m_CurID, Hashed::No);
    v.Int("guest.current_type", o.m_CurTypeID, Hashed::No);
    v.Float("guest.current_score", o.m_curScore, Hashed::No);
    v.Float("guest.price_score", o.m_priceScore, Hashed::No);
    v.Float("guest.queue_score", o.m_queueScore, Hashed::No);
    v.Float("guest.safety_score", o.m_safetyScore, Hashed::No);
    v.Float("guest.clean_score", o.m_cleanScore, Hashed::No);
    v.Bool("guest.missed_purchase", o.m_missedPurchase, Hashed::No);
    v.Float("guest.food_ill", o.m_foodIll, Hashed::Yes);
    v.Bool("guest.ill", o.m_imIll, Hashed::Yes);
    v.Bool("guest.last_good", o.m_lastGood, Hashed::No);
    v.Bool("guest.last_bad", o.m_lastBad, Hashed::No);
    v.Bool("guest.has_sat_down", o.m_HasSatDown, Hashed::No);
    v.Bool("guest.leaving_early", o.m_leavingEarly, Hashed::No);
    v.Ints("guest.thoughts", o.m_Thoughts, 3, Hashed::No);
    v.UInt("guest.offence_state", o.m_OffenceState, Hashed::No);
    v.Float("guest.time_of_offence", o.m_timeOfOffence, Hashed::No);
    v.Float("guest.last_update", o.m_lastUpdateHappened, Hashed::No);
}

} // namespace obl::object
