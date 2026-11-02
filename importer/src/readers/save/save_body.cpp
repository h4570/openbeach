#include "importer/src/readers/save/save_body.h"

#include "formats/save_records.h"
#include "importer/src/readers/save/save_writer.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <map>

namespace obl::convert {
namespace {

using T = SaveTag;

// One record of an emitter: its label, tag and, for arrays, the exact element count (0 leaves a text's length open).
struct FieldSpec {
    const char* name;
    SaveTag tag;
    uint32_t count = 1;
};

constexpr FieldSpec kWorld[] = {
        {"sizeof", T::UInt},
        {"m_MapName", T::String, 0},
        {"m_fGameStartTime", T::Float},
        {"m_fGameTime", T::Float},
        {"m_GameDay", T::Int},
        {"m_GameDayTime", T::Float},
        {"m_LastGameDayTime", T::Float},
        {"m_fLastGameDayTime", T::Float},
        {"m_fGameDeltaTime", T::Float},
        {"m_fGameSpeed", T::Float},
        {"m_GameSpeedLevel", T::Int},
        {"m_fNextStaffFeedbackTime", T::Float},
        {"m_bStartup", T::Bool},
        {"m_NumValidBuildings", T::Int},
        {"m_ValidBuildingIDs", T::IntArray, 0x80},
        {"m_bBreakDownsAllowed", T::Bool},
        {"m_bAllowLitter", T::Bool},
        {"m_BoatList", T::IntArray, 5},
        {"m_NumBoats", T::Int},
        {"m_fPowerUse", T::Float},
        {"m_fMaxPower", T::Float},
        {"m_fCurrentPower", T::Float},
        {"m_fPowerDemand", T::Float},
        {"m_ScriptFlags", T::Int},
        {"m_ScriptVars", T::IntArray, 0x20},
        {"m_bPaused", T::Bool},
        {"m_bForcedPaused", T::Bool},
        {"m_NumGuests", T::Int},
        {"m_NumLitter", T::Int},
        {"m_NumBuildings", T::Int},
        {"m_fScenicVal", T::Float},
        {"m_fWaterContam", T::Float},
        {"m_iObjectiveStatus[0]", T::UInt},
        {"m_iObjectiveStatus[1]", T::UInt},
        {"m_iObjectiveStatus[2]", T::UInt},
        {"m_iObjectiveStatus[3]", T::UInt},
        {"m_TotalGuestArrive", T::IntArray, 2},
        {"m_TotalGuestDepart", T::IntArray, 2},
        {"m_TodayGuestArrive", T::IntArray, 2},
        {"m_TodayGuestDepart", T::IntArray, 2},
        {"m_TotalGuestUnhappyDepart", T::IntArray, 2},
        {"m_TodayGuestUnhappyDepart", T::IntArray, 2},
        {"m_fIncomeToday", T::Float},
        {"m_fDemolishIncomeToday", T::Float},
        {"m_fFineIncomeToday", T::Float},
        {"m_fAccomIncomeToday", T::Float},
        {"m_fAttractIncomeToday", T::Float},
        {"m_fRetailIncomeToday", T::Float},
        {"m_fTotalFines", T::Float},
        {"m_fCostsToday", T::Float},
        {"m_fConstructCostToday", T::Float},
        {"m_fSceneryCostToday", T::Float},
        {"m_fWagesCostToday", T::Float},
        {"m_fStockCostToday", T::Float},
        {"m_fAverageProfit", T::Float},
        {"m_fAverageCost", T::Float},
        {"m_fAverageIncome", T::Float},
        {"m_fProfitRecord", T::FloatArray, 0x80},
        {"m_fIncomeRecord", T::FloatArray, 0x80},
        {"m_fCostRecord", T::FloatArray, 0x80},
        {"m_BuildingsConstructed", T::Int},
        {"m_DiffBuildingsConstructed", T::Int},
        {"m_BarsConstructed", T::Int},
        {"m_BuildingsLeftToBuild", T::Int},
        {"m_fAverageTotty", T::Float},
        {"m_fAverageBeauty", T::Float},
        {"m_fAverageSafety", T::Float},
        {"m_PartyCount", T::Int},
        {"m_ShagCount", T::Int},
        {"m_PissedCount", T::Int},
        {"m_GuestsDrowned", T::Int},
        {"m_GuestsEaten", T::Int},
        {"m_UnbuiltAfterStartup", T::Int},
        {"Worldscript state", T::Int},
};

constexpr FieldSpec kObjectManager[] = {
        {"sizeof", T::UInt},
        {"m_dwNumActiveObjects", T::Int},
        {"m_dwNumActiveBuildingObjects", T::Int},
        {"m_dwNumActiveGuestObjects", T::Int},
        {"m_dwNumActiveOtherObjects", T::Int},
        {"m_dwObjectIDList", T::IntArray, 18500},
        {"m_dwNextFreeObjectID", T::Int},
        {"m_bGlobalKeyline", T::Bool},
};

constexpr FieldSpec kPlayer[] = {{"sizeof", T::UInt}, {"Name", T::WideString, 0}, {"Cash", T::Float}};

constexpr FieldSpec kBoat[] = {
        {"GuestInfo", T::Int},
        {"GuestType", T::IntArray, 20},
};
constexpr FieldSpec kBoatTail[] = {
        {"GuestKeywordNumbers", T::IntArray, 20},
        {"NumGuests", T::Int},
        {"Time", T::Float},
        {"Day", T::Int},
        {"TimeFlags", T::Int},
};
constexpr FieldSpec kFerryTail[] = {
        {"m_GuestsLeft.fAvgHapp", T::FloatArray, 20},
        {"m_GuestsLeft.iHappyLeavers", T::UInt},
        {"m_GuestsLeft.iUnhappyLeavers", T::UInt},
};

constexpr FieldSpec kRepairJob[] = {
        {"RepairID", T::Int},
        {"iCalls", T::UInt},
        {"fDistance", T::Float},
        {"fTimeCalled", T::Float},
        {"bUrgent", T::Bool},
        {"Type", T::Int},
};
constexpr FieldSpec kRepairActive[] = {{"RepairID", T::Int}, {"bRepairing", T::Bool}, {"bUrgent", T::Bool}, {"Type", T::Int}};
constexpr FieldSpec kRepairTail[] = {{"m_BreakdownCount", T::Int}, {"m_VandalismCount", T::Int}, {"m_ActiveCount", T::Int}};

constexpr FieldSpec kFeedbackTail[] = {
        {"m_CleanerMessages", T::Int},
        {"m_LifeGuardMessages", T::Int},
        {"m_SecurityGuardMessages", T::Int},
        {"m_MechanicMessages", T::Int},
        {"m_RepMessages", T::Int},
        {"m_BuilderMessages", T::Int},
        {"m_StaffMessages", T::Int},
        {"m_NumOfSuppressedMessages", T::Int},
        {"m_SuppressWorldMess", T::Bool},
        {"m_SuppressObjectMess", T::Bool},
        {"m_SuppressedMessages", T::IntArray, 100},
        {"m_fLastCheckedMissedDesires", T::Float},
};

constexpr FieldSpec kDelayedMessage[] = {
        {"dwSender", T::Int},
        {"dwReciever", T::Int},
        {"dwType", T::Int},
        {"dwFlags", T::Int},
        {"dwData", T::Int},
        {"fData", T::Float},
        {"fTime", T::Float},
        {"member@0x14", T::Int},
};

constexpr FieldSpec kMapViewHead[] = {
        {"sizeof", T::UInt},
        {"m_MapX", T::UInt},
        {"m_MapY", T::UInt},
        {"m_MapPosition.x", T::Float},
        {"m_MapPosition.y", T::Float},
        {"m_MapVelocity.x", T::Float},
        {"m_MapVelocity.y", T::Float},
};
constexpr FieldSpec kMapViewTail[] = {
        {"m_bMapLocked", T::Bool},
        {"m_bMovingMap", T::Bool},
        {"m_fMoveMapDuration", T::Float},
        {"m_fMoveMapStartTime", T::Float},
        {"m_iMoveMapStartX", T::UInt},
        {"m_iMoveMapStartY", T::UInt},
        {"m_iMoveMapDestX", T::UInt},
        {"m_iMoveMapDestY", T::UInt},
        {"m_fLastUnBurnTime", T::Float},
        {"m_GuestObjectsDrawn", T::Int},
};

constexpr FieldSpec kCleaningJob[] = {{"CleaningID", T::Int}, {"iCalls", T::UInt}, {"fTimeCalled", T::Float}, {"bUrgent", T::Bool}};
constexpr FieldSpec kCleaningActive[] = {{"CleaningID", T::Int}, {"bCleaning", T::Bool}, {"bUrgent", T::Bool}};
constexpr FieldSpec kCleaningTail[] = {{"m_CleaningCount", T::Int}, {"m_ActiveCount", T::Int}};

constexpr FieldSpec kConstructionJobHead[] = {{"BuildID", T::Int}, {"BuildersOnJob", T::Int}};
constexpr FieldSpec kHoarding[] = {{"Direct", T::Int}, {"x", T::Int}, {"y", T::Int}, {"member@0xc", T::Int}};
constexpr FieldSpec kConstructionJobTimes[] = {
        {"m_fConstStart", T::Float},
        {"m_fDemolishStart", T::Float},
        {"m_fConstDone", T::Float},
        {"m_fDemolDone", T::Float},
        {"m_fConstDone", T::Float},
        {"m_fDemolishDone", T::Float},
        {"m_bUnderConstruction", T::Bool},
        {"m_bUnderDemolishion", T::Bool},
        {"m_BuildingReady", T::Bool},
        {"m_BuildingReady", T::Bool},
};
constexpr FieldSpec kBuilderPoint[] = {{"X", T::UInt}, {"Y", T::UInt}, {"Dir", T::Int}};
constexpr FieldSpec kConstructionBuilder[] = {
        {"ID", T::Int},
        {"Status", T::Int},
        {"Location", T::UInt},
        {"BuildID", T::Int},
        {"ManuallyAssigned", T::Bool},
};
constexpr uint32_t kMaxConstructionBuilders = 50; // the emitter writes at most this many builder records

constexpr FieldSpec kWeather[] = {
        {"m_LightIndex", T::UInt},
        {"m_LightPrevIndex", T::UInt},
        {"m_WeatherTime", T::Float},
        {"m_DesireModifiers", T::FloatArray, 26},
        {"m_dwNextEvent", T::Int},
        {"m_dwNumEvents", T::Int},
        {"m_bLightFlash", T::Bool},
        {"m_dwLightCol", T::Int},
        {"m_fFlashTime", T::Float},
        {"m_bManual", T::Bool},
        {"m_ManualCol", T::UInt},
        {"m_fLightningStrikeTime", T::Float},
        {"m_dwStrikeLevel", T::Int},
        {"m_dwStrikeBuilding", T::Int},
        {"m_bStrikeBuilding", T::Bool},
        {"m_rStrikeArea.top", T::UInt},
        {"m_rStrikeArea.bottom", T::UInt},
        {"m_rStrikeArea.left", T::UInt},
        {"m_rStrikeArea.right", T::UInt},
        {"m_StrikePoint.x", T::Float},
        {"m_StrikePoint.y", T::Float},
        {"m_StrikePoint.z", T::Float},
        {"m_bStrikes", T::Bool},
        {"m_LightningLightmap", T::Bool},
        {"PrevState", T::UInt},
        {"CurState", T::UInt},
        {"m_LastCol", T::Dword},
        {"m_dwDayOffset", T::Int},
        {"m_dwCurDayOffset", T::Int},
};

// ---- Objects ----

constexpr FieldSpec kSprite[] = {
        {"m_fStartTime", T::Float},
        {"m_eAnimType", T::UInt},
        {"m_iFPS", T::UInt},
        {"m_iCurrAnim", T::UInt},
        {"m_iCurrFrame", T::UInt},
        {"m_iCurrDir", T::UInt},
        {"m_iCurrPal", T::UInt},
        {"m_bLit", T::Bool},
        {"m_bKeyline", T::Bool},
        {"m_bTriggeredSound", T::Bool},
};

constexpr FieldSpec kEntityHead[] = {
        {"m_bWaitingForDeath", T::Bool},
        {"m_bRequiresAttention", T::Bool},
        {"m_Name", T::String, 0},
        {"m_dwID", T::Int},
        {"m_bType", T::Byte},
        {"m_bOwner", T::Byte},
        {"m_WorldXYZ.x", T::Float},
        {"m_WorldXYZ.y", T::Float},
        {"m_WorldXYZ.z", T::Float},
        {"m_dwState", T::Int},
        {"m_bVisible", T::Bool},
        {"m_XDrawOffset", T::UInt},
        {"m_YDrawOffset", T::UInt},
        {"Sprite name", T::String, 0},
};
constexpr FieldSpec kEntityTail[] = {{"m_bDebugBreak", T::Bool}, {"m_SelectLevel", T::UInt}};

// The last two bytes reuse the labels of the called-cleaning/called-repairing flags.
constexpr FieldSpec kProp[] = {
        {"m_bGridW", T::Byte},
        {"m_bGridH", T::Byte},
        {"m_bCleaning", T::Bool},
        {"m_bRepairing", T::Bool},
        {"m_bCalledCleaning", T::Bool},
        {"m_bCalledRepairing", T::Bool},
        {"m_bCalledCleaning", T::Bool},
        {"m_bCalledRepairing", T::Bool},
};

constexpr FieldSpec kScenery[] = {
        {"m_AttachedGuest", T::IntArray, 2},
        {"m_NumItems", T::Int},
        {"m_NumAttachedGuests", T::Int},
        {"m_MaxNumAttachedGuests", T::Int},
        {"m_FreeSeat", T::Int},
        {"m_bVandalised", T::Bool},
        {"m_RepairState", T::UInt},
        {"m_bFlashLight", T::Bool},
        {"m_PreVandlismAnim", T::UInt},
        {"m_ReburnRect.left", T::UInt},
        {"m_ReburnRect.top", T::UInt},
        {"m_ReburnRect.right", T::UInt},
        {"m_ReburnRect.bottom", T::UInt},
        {"m_ScrXYRect.left", T::UInt},
        {"m_ScrXYRect.top", T::UInt},
        {"m_ScrXYRect.right", T::UInt},
        {"m_ScrXYRect.bottom", T::UInt},
        {"m_fLightsOn", T::Float},
        {"m_fLightsOff", T::Float},
        {"m_bSoundPlaying", T::Bool},
        {"m_OwnerBuildID", T::Int},
        {"m_bCanSelect", T::Bool},
};

constexpr FieldSpec kBuilding[] = {
        {"m_staff", T::IntArray, 10},
        {"m_numMalestaff", T::Int},
        {"m_numFemalestaff", T::Int},
        {"m_NonServeStaff", T::IntArray, 10},
        {"m_NumNonServestaff", T::Int},
        {"m_staff_busy", T::ByteArray, 10},
        {"m_burn", T::Bool},
        {"m_Entrance", T::Bool},
        {"m_queue_point.x", T::Float},
        {"m_queue_point.y", T::Float},
        {"m_queue_point.z", T::Float},
        {"m_serve_point.x", T::Float},
        {"m_serve_point.y", T::Float},
        {"m_serve_point.z", T::Float},
        {"m_EntrancePoint.x", T::Float},
        {"m_EntrancePoint.y", T::Float},
        {"m_EntrancePoint.z", T::Float},
        {"m_queue_direction.x", T::Float},
        {"m_queue_direction.y", T::Float},
        {"m_queue_direction.z", T::Float},
        {"m_TopWorldPos.x", T::Float},
        {"m_TopWorldPos.y", T::Float},
        {"m_TopWorldPos.z", T::Float},
        {"m_Placed", T::Float},
        {"m_NumLilos", T::Int},
        {"m_NumBoards", T::Int},
        {"m_NumSnorkels", T::Int},
        {"m_CurCapacity", T::Int},
        {"m_NumVehicles", T::Int},
        {"m_Vehicles", T::IntArray, 6},
        {"m_bVehicleInUse", T::ByteArray, 6},
        {"m_LastMsgTime", T::Float},
        {"m_fLightsOn", T::Float},
        {"m_fLightsOff", T::Float},
        {"m_bHasBall", T::Bool},
        {"m_BallPosition.x", T::Float},
        {"m_BallPosition.y", T::Float},
        {"m_BallPosition.z", T::Float},
        {"m_OpenStatus", T::UInt},
        {"m_NumGuests", T::Int},
        {"m_SatisfactionPercentage", T::Float},
        {"m_Commod_IntensityM", T::FloatArray, 5}, // overlaps the next array by three floats (one interleaved table)
        {"m_Commod_IntensityF", T::FloatArray, 5},
        {"m_Commod_Price", T::FloatArray, 5},
        {"m_Commod_PriceFactor", T::FloatArray, 5},
        {"m_Commod_Cost", T::FloatArray, 5},
        {"m_BeerStrength", T::Float},
        {"m_BeerType", T::Int},
        {"m_fIncomeToday", T::Float},
        {"m_fCostToday", T::Float},
        {"m_fProfitToday", T::Float},
        {"m_fAverageProfit", T::Float},
        {"m_BuildingAge", T::Int},
        {"m_fProfitRecord", T::FloatArray, 64},
        {"m_fLastFaultDayTime", T::Float},
        {"m_LastFaultDay", T::Int},
        {"m_NumFaults", T::Int},
        {"m_UserRepairInterval", T::Int},
        {"m_RepairInterval", T::Int},
        {"m_RepairState", T::UInt},
        {"m_bWasBroken", T::Bool},
        {"m_LastRepairCall", T::Int},
        {"m_fLastRepairDayTime", T::Float},
        {"m_LastRepairDay", T::Int},
        {"m_NumVisitsToday", T::Int},
        {"m_VisitsSinceCleaned", T::Int},
        {"m_LastFailTime", T::Int},
        {"m_PowerConsumption", T::UInt},
        {"m_NumTimesCleanedToday", T::Int},
        {"m_bClosedForPower", T::Bool},
        {"m_bUserClose", T::Bool},
        {"m_bCalledForInspection", T::Bool},
        {"m_bCalledForRepair", T::Bool},
        {"m_bBuiltByUser", T::Bool},
        {"m_iNumTables", T::UInt},
        {"m_Tables", T::IntArray, 10},
        {"m_bBurnLimbo", T::Bool},
        {"m_rScrExtents.left", T::UInt},
        {"m_rScrExtents.top", T::UInt},
        {"m_rScrExtents.right", T::UInt},
        {"m_rScrExtents.bottom", T::UInt},
        {"m_rScrRect.left", T::UInt},
        {"m_rScrRect.top", T::UInt},
        {"m_rScrRect.right", T::UInt},
        {"m_rScrRect.bottom", T::UInt},
        {"m_fTimeInLimbo", T::Float},
        {"m_iShadowOffset", T::UInt},
        {"m_iPickOffset", T::UInt},
        {"m_iLightMapOffset", T::UInt},
        {"m_bSoundPlaying", T::Bool},
        {"m_SoundID", T::UInt},
};
constexpr FieldSpec kBuildingFlags[] = {{"bTempUnderConstruction", T::Bool}, {"bTempUnderDemolishion", T::Bool}};
constexpr FieldSpec kBuildingTail[] = {
        {"m_iNumLoungers", T::UInt},
        {"m_dwLoungers", T::IntArray, 20},
        {"m_InfluenceArea", T::Int},
        {"m_fBuildingAnimTime", T::Float},
};
constexpr FieldSpec kEventSlotTimes[] = {{"Start", T::Float}, {"Duration", T::Float}, {"Price", T::Float}};
constexpr FieldSpec kQueueHead[] = {
        {"m_dwOwner", T::Int},
        {"m_dwMaxLength", T::Int},
        {"m_ServePoint.x", T::Float},
        {"m_ServePoint.y", T::Float},
        {"m_QueueDirection.x", T::Float},
        {"m_QueueDirection.y", T::Float},
};

constexpr FieldSpec kTableChair[] = {
        {"ChairSeated", T::Bool},
        {"ChairOccupier", T::Int},
        {"ChairDir", T::UInt},
        {"iPosition", T::UInt},
        {"bWaitingForWaiter", T::Bool},
};
constexpr FieldSpec kTableTail[] = {
        {"m_bDrawWaiter", T::Bool},
        {"m_WaiterServing", T::Int},
        {"m_WaiterPos", T::UInt},
        {"m_WaiterID", T::Int},
        {"m_bWaiterReady", T::Bool},
        {"m_iRotation", T::UInt},
        {"m_dwOwner", T::Int},
        {"m_bServeTable", T::Bool},
        {"m_GuestsRemoved", T::UInt},
        {"m_GuestsAdded", T::UInt},
};

constexpr FieldSpec kMovingHead[] = {
        {"m_fLastTimeRemaining", T::Float},
        {"m_bMoving", T::Bool},
        {"m_fMass", T::Float},
        {"m_vPosition.x", T::Float},
        {"m_vPosition.y", T::Float},
        {"m_vVelocity.x", T::Float},
        {"m_vVelocity.y", T::Float},
        {"m_vSteerForce.x", T::Float},
        {"m_vSteerForce.y", T::Float},
        {"m_fMaxForce", T::Float},
        {"m_fMaxVel", T::Float},
        {"m_vTarget.x", T::Float},
        {"m_vTarget.y", T::Float},
        {"m_DestXYZ.x", T::Float},
        {"m_DestXYZ.y", T::Float},
        {"m_DestXYZ.z", T::Float},
        {"m_DestObjectID", T::Int},
        {"m_BuildingLeavingID", T::Int},
        {"m_eBehaviourState", T::UInt},
        {"m_GroupID", T::Int},
        {"m_bFollowObject", T::Bool},
        {"m_dwFollowID", T::Int},
        {"m_iFollowDistance", T::UInt},
        {"m_bReachedFollowDistance", T::Bool},
};
// A path's save data: the route (its integers carry the uint tag), then the cursor.
constexpr FieldSpec kPathDataHead[] = {{"m_fTimeStamp", T::Float}, {"m_iRefCount", T::UInt}};
constexpr FieldSpec kPathDataTail[] = {
        {"m_Start.x", T::UInt},
        {"m_Start.y", T::UInt},
        {"m_Stop.x", T::UInt},
        {"m_Stop.y", T::UInt},
        {"m_ePathType", T::UInt},
        {"m_bValid", T::Bool},
        {"m_bReverse", T::Bool},
        {"m_iCurrStep", T::UInt},
};
constexpr FieldSpec kMovingTail[] = {
        {"m_bWalkInBuilding", T::Bool},
        {"m_dwBuildingID", T::Int},
        {"m_bBuildingTraversal", T::Bool},
        {"m_iNumWalkPts", T::UInt},
        {"m_iCurrWalkPt", T::UInt},
        {"m_bEnteredTheSea", T::Bool},
        {"m_bInsideBuilding", T::Bool},
        {"m_bMoveToEntrance", T::Bool},
        {"m_bControlDirection", T::Bool},
        {"m_bVehiclePath", T::Bool},
        {"m_iPosOffset", T::UInt},
        {"m_iLastDir", T::UInt},
        {"m_fAnimTimeDiv2", T::Float},
        {"m_bWaitingForMoveImHere", T::Bool},
        {"m_fMessageReceived", T::Float},
        {"m_eTurnType", T::UInt},
        {"m_fTargetTolerance", T::Float},
};

constexpr FieldSpec kGuestHead[] = {
        {"m_Tolerance", T::FloatArray, 14},
        {"m_person", T::FloatArray, 13},
        {"m_desires", T::FloatArray, 26},
        {"m_opinion", T::FloatArray, 7},
        {"m_rides.last", T::IntArray, 5},
        {"m_rides.start", T::Int},
        {"m_rides.end", T::Int},
        {"m_invent.slot", T::IntArray, 5},
        {"m_desired_commod", T::Int},
        {"m_VehicleID", T::Int},
        {"m_activity", T::Bool},
        {"m_StopAction", T::Bool},
        {"m_walkpoints[0].x", T::Float},
        {"m_walkpoints[0].y", T::Float},
        {"m_walkpoints[0].z", T::Float},
        {"m_walkpoints[1].x", T::Float},
        {"m_walkpoints[1].y", T::Float},
        {"m_walkpoints[1].z", T::Float},
        {"m_walkpoints[2].x", T::Float},
        {"m_walkpoints[2].y", T::Float},
        {"m_walkpoints[2].z", T::Float},
        {"m_walkpoint", T::Int},
        {"m_drunk", T::Float},
        {"m_oxygen", T::Int},
        {"m_cash", T::Float},
        {"m_name", T::WideString, 0},
        {"m_happiness", T::Float},
        {"m_WalkAnimation", T::String, 0},
        {"m_StandAnimation", T::String, 0},
        {"m_Fidget", T::Bool},
        {"m_CurrAnimation", T::String, 0},
        {"m_ReturnAnimation", T::String, 0},
        {"m_LeaveState", T::Int},
};
// A guest's save data holds m_DayArrived twice, at its place and after m_AmountLoition.
constexpr FieldSpec kGuestTail[] = {
        {"m_EventID", T::Int},
        {"m_ActionState", T::Int},
        {"m_RepeatAction", T::Int},
        {"m_AttachedOffsetX", T::Int},
        {"m_AttachedOffsetY", T::Int},
        {"m_FightPerson", T::Int},
        {"m_ActivityPerson", T::Int},
        {"m_bJustLittered", T::Bool},
        {"m_fTimeLittered", T::Float},
        {"m_StayingAt", T::Int},
        {"m_WakeTime", T::Float},
        {"m_BedTime", T::Float},
        {"m_NightsToStay", T::Int},
        {"m_DayArrived", T::Int},
        {"m_fTimeArrived", T::Float},
        {"m_TimeToLeave", T::Float},
        {"m_LastID", T::IntArray, 2},
        {"m_bLastActivity", T::ByteArray, 2},
        {"m_ArrayPos", T::Int},
        {"m_fSunBurnLevel", T::Float},
        {"m_AmountLoition", T::Int},
        {"m_DayArrived", T::Int},
        {"m_AppliedLotion", T::Bool},
        {"m_BestSpotOpinion", T::Bool},
        {"m_Best", T::Int},
        {"m_BestType", T::Int},
        {"m_fBestScore", T::Float},
        {"m_WorstSpotOpinion", T::Bool},
        {"m_WorstType", T::Int},
        {"m_fWorstScore", T::Float},
        {"m_CurID", T::Int},
        {"m_CurTypeID", T::Int},
        {"m_fCurScore", T::Float},
        {"m_fPriceScore", T::Float},
        {"m_fQueueScore", T::Float},
        {"m_fSafetyScore", T::Float},
        {"m_fCleanScore", T::Float},
        {"m_bMissedPurchase", T::Bool},
        {"m_fFoodIll", T::Float},
        {"m_bImIll", T::Bool},
        {"m_bLastGood", T::Bool},
        {"m_bLastBad", T::Bool},
        {"m_HasSatDown", T::Bool},
        {"m_bLeavingEarly", T::Bool},
        {"m_Thoughts", T::IntArray, 3},
        {"m_OffenceState", T::UInt},
        {"m_fTimeOfOffence", T::Float},
        {"m_fLastUpdateHappened", T::Float},
};

constexpr FieldSpec kStaff[] = {
        {"m_HomeID", T::Int},
        {"m_Wage", T::Int},
        {"m_walkpoint", T::Int},
        {"m_happiness", T::Float},
        {"m_SalaryExpectation", T::Float},
        {"m_StartTime", T::Int},
        {"m_EndTime", T::Int},
        {"m_hoursWorkedInRow", T::UInt},
        {"m_OverWorkReduct", T::UInt},
        {"m_IdleTimePerHour", T::Int},
        {"m_HiredOn", T::Int},
        {"m_DirectStaff", T::Bool},
        {"m_GotoObject", T::Int},
        {"m_LastAction", T::Int},
        {"m_DirectPos.x", T::Float},
        {"m_DirectPos.y", T::Float},
        {"m_bFoundDirecting", T::Bool},
        {"m_NumFeedBackMesslog", T::Int},
        {"m_bCantDoJob", T::Bool},
        {"m_WanderPos.x", T::Float},
        {"m_WanderPos.y", T::Float},
        {"m_WanderPos.z", T::Float},
        {"m_DebugInfo", T::Int},
        {"m_Thoughts[0]", T::WideString, 0},
        {"m_Thoughts[1]", T::WideString, 0},
        {"m_Thoughts[2]", T::WideString, 0},
        {"m_bGotTheSack", T::Bool},
        {"m_bEndedShift", T::Bool},
        {"m_bGonePastShift", T::Bool},
};
constexpr FieldSpec kServiceStaff[] = {{"m_dwActionState", T::Int}};
constexpr FieldSpec kBuilder[] = {
        {"m_sBuilderCount", T::Int}, // a class-wide counter, written by every builder
        {"m_NearestValidTileX", T::Int},
        {"m_NearestValidTileY", T::Int},
        {"m_NumBuildersWhistling", T::Int},
        {"m_NumTimesPlayedAnimInRow", T::Int},
        {"m_fLastTaskTime", T::Float},
        {"m_BuilderNumber", T::Int},
        {"m_HomePoint.x", T::Float},
        {"m_HomePoint.y", T::Float},
        {"m_HomePoint.z", T::Float},
        {"m_ActionState", T::Int},
        {"m_Night", T::Int},
        {"m_InHut", T::Int},
        {"m_CurrentFeedback", T::Int},
        {"m_FeedbackStatus", T::Int},
        {"m_RequiredFeedback", T::Int},
        {"m_BuilderLocation", T::Int},
        {"m_JobType", T::Int},
        {"m_LastStartTime", T::Int},
        {"m_bCheckForJobs", T::Bool},
        {"m_WaitTillMorning", T::Int},
};
constexpr FieldSpec kCleaner[] = {
        {"m_sCleanerCount", T::Int},
        {"m_bLitterFound", T::Bool},
        {"m_bToiletFound", T::Bool},
        {"m_bBinFound", T::Bool},
        {"m_bCleanToilets", T::Bool},
        {"m_bCleanLitter", T::Bool},
        {"m_bCleanBins", T::Bool},
        {"m_fLastTaskTime", T::Float},
        {"m_LitterDistance", T::Int},
        {"m_ToiletsToday", T::Int},
        {"m_BinsToday", T::Int},
        {"m_LitterToday", T::Int},
        {"m_BusyToilet", T::Int},
        {"m_FeedbackStatus", T::Int},
        {"m_RequiredFeedback", T::Int},
        {"m_CurrentFeedback", T::Int},
        {"m_BinPos.x", T::Float},
        {"m_BinPos.y", T::Float},
        {"m_BinPos.z", T::Float},
        {"m_ActionState", T::Int},
        {"m_UrgentType", T::Int},
        {"m_bUrgentClean", T::Bool},
};
constexpr FieldSpec kLifeguardHead[] = {
        {"m_saving", T::Bool},
        {"m_RescueThenDeath", T::Bool},
        {"m_saving_id", T::Int},
        {"m_BeachArea", T::Int},
        {"m_ExitPoint.x", T::Float},
        {"m_ExitPoint.y", T::Float},
        {"m_ExitPoint.z", T::Float},
        {"m_ReturnAnimation", T::String, 0},
        {"m_CurrAnimation", T::String, 0},
        {"m_LeaveState", T::Int},
        {"m_StopAction", T::Bool},
        {"m_ActionState", T::Int},
};
constexpr FieldSpec kLifeguardTail[] = {
        {"m_AttachedOffsetX", T::Int},
        {"m_AttachedOffsetY", T::Int},
        {"m_FeedbackStatus", T::Int},
        {"m_RequiredFeedback", T::Int},
        {"m_CurrentFeedback", T::Int},
        {"m_NumSaves", T::Int},
        {"m_NumDrowned", T::Int},
        {"m_GuestDrowned", T::Bool},
        {"m_timesCalledOut", T::Int},
        {"m_dwTimesPathBlocked", T::Int},
        {"m_InfluenceArea", T::Int},
        {"m_bInMyArea", T::Bool},
        {"m_bInSea", T::Bool},
        {"m_bEnteringSea", T::Bool},
        {"m_AtHotel", T::Bool},
        {"m_DrawGuest", T::Bool},
};
// The job mask has no label of its own: three bits written as booleans.
constexpr FieldSpec kMechanic[] = {
        {"m_sMechanicCount", T::Int},
        {"vandalism", T::Bool},
        {"breakdown", T::Bool},
        {"watering", T::Bool},
        {"m_bFoundRepair", T::Bool},
        {"m_bFoundBetterJob", T::Bool},
        {"m_fLastTaskTime", T::Float},
        {"m_FeedbackStatus", T::Int},
        {"m_RequiredFeedback", T::Int},
        {"m_CurrentFeedback", T::Int},
        {"m_dwRepairType", T::Int},
        {"m_WateringPos.x", T::Float},
        {"m_WateringPos.y", T::Float},
        {"m_WateringPos.z", T::Float},
        {"m_dwInspectionsToday", T::Int},
        {"m_dwRepairsToday", T::Int},
        {"m_dwVandalsToday", T::Int},
        {"m_dwWateredToday", T::Int},
        {"m_ActionState", T::Int},
};
constexpr FieldSpec kRepHead[] = {
        {"m_sRepCount", T::Int},
        {"m_talking_id", T::Int},
        {"counters[0]", T::IntArray, 5},
        {"counters[1]", T::IntArray, 5},
        {"counters[2]", T::IntArray, 5},
        {"counters[3]", T::IntArray, 5},
        {"counters[4]", T::IntArray, 5},
        {"counters[5]", T::IntArray, 5},
        {"counters[6]", T::IntArray, 5},
        {"m_ReturnAnimation", T::String, 0},
        {"m_CurrAnimation", T::String, 0},
        {"m_LeaveState", T::Int},
        {"m_StopAction", T::Bool},
        {"m_ActionState", T::Int},
};
constexpr FieldSpec kRepTail[] = {
        {"m_AttachedOffsetX", T::Int},
        {"m_AttachedOffsetY", T::Int},
        {"m_FeedbackStatus", T::Int},
        {"m_RequiredFeedback", T::Int},
        {"m_CurrentFeedback", T::Int},
        {"m_GuestsTalkedToToday", T::Int},
        {"m_MinTalkTime", T::Int},
        {"m_MaxTalkTime", T::Int},
        {"m_fTimeWithGuests", T::Float},
        {"m_bSentToGuest", T::Bool},
        {"m_GuestOffSetPosY", T::Float},
        {"m_GuestOffSetPosX", T::Float},
        {"m_GuestOpinionValue", T::Int},
};
constexpr FieldSpec kSecurityGuard[] = {
        {"m_sGuardCount", T::Int},
        {"m_dwOffenderID", T::Int},
        {"m_dwFightWithID", T::Int},
        {"m_fTolerance", T::Float},
        {"m_eTolerance", T::UInt},
        {"m_eSeverity", T::UInt},
        {"m_eAction", T::UInt},
        {"m_bDoubleFine", T::Bool},
        {"m_bMisdemeanourFound", T::Bool},
        {"m_bShouldReprimand", T::Bool},
        {"m_bShouldDetain", T::Bool},
        {"m_NotEnoughBins", T::Bool},
        {"m_MisdemeanourPos.x", T::Float},
        {"m_MisdemeanourPos.y", T::Float},
        {"m_MisdemeanourPos.z", T::Float},
        {"m_iNumMinorFinesToday", T::UInt},
        {"m_iNumSeriousFinesToday", T::UInt},
        {"m_iNumVandals", T::UInt},
        {"m_iNumFights", T::UInt},
        {"m_iNumJailed", T::UInt},
        {"m_FeedbackStatus", T::Int},
        {"m_RequiredFeedback", T::Int},
        {"m_CurrentFeedback", T::Int},
        {"m_ActionState", T::Int},
        {"m_fMinorFineAmount", T::Float},
        {"m_fSeriousFineAmount", T::Float},
        {"m_fFineTotalToday", T::Float},
        {"m_FineOffsetX", T::Float},
        {"m_FineOffsetY", T::Float},
        {"m_FineOffsetY", T::Float},
        {"m_FineLevel", T::Int},
        {"m_iNumCautioned", T::UInt},
};

constexpr FieldSpec kVehicleHead[] = {
        {"m_Guests", T::IntArray, 300},
        {"m_dwGuestsToWaitFor", T::Int},
        {"m_dwCurrCap", T::Int},
        {"m_fRideTime", T::Float},
        {"m_dwOwner", T::Int},
        {"m_DestroyPoint", T::Byte},
        {"m_UnloadPoint", T::Byte},
        {"m_iUnloadDir", T::UInt},
        {"m_TurnArea.x", T::UInt},
        {"m_TurnArea.y", T::UInt},
};
constexpr uint32_t kVehicleWayPoints = 64;
constexpr FieldSpec kVehicleMiddle[] = {
        {"m_iNumWayPts", T::UInt},
        {"m_iCurrentWayPoint", T::UInt},
        {"m_CurrentRoamPoint.x", T::Float},
        {"m_CurrentRoamPoint.y", T::Float},
        {"m_CurrentRoamPoint.z", T::Float},
        {"m_CurrentFishPoint.x", T::Float},
        {"m_CurrentFishPoint.y", T::Float},
        {"m_CurrentFishPoint.z", T::Float},
        {"m_bMoveToTurnPoint", T::Bool},
        {"m_bFishing", T::Bool},
        {"m_bTurnAnim", T::Bool},
        {"m_bTurnCW", T::Bool},
        {"m_iNumTurns", T::UInt},
        {"m_fTurnAnimStartTime", T::Float},
        {"m_fTurnAnimLength", T::Float},
        {"m_iStartCircuitPoint", T::UInt},
        {"m_iStopCircuitPoint", T::UInt},
        {"m_iCurrCircuitPoint", T::UInt},
};
constexpr uint32_t kVehicleSprites = 5; // vehicle, door and three auxiliary sprites, each only when present
constexpr FieldSpec kVehicleTail[] = {
        {"m_eVehicleType", T::UInt},
        {"m_bWaitForGuestToBoard", T::Bool},
        {"m_bPlayingDoorAnim", T::Bool},
        {"m_FerryEntryPoint.x", T::Float},
        {"m_FerryEntryPoint.y", T::Float},
        {"m_FerryEntryPoint.z", T::Float},
        {"m_FerryExitPoint.x", T::Float},
        {"m_FerryExitPoint.y", T::Float},
        {"m_FerryExitPoint.z", T::Float},
        {"m_FerryEntryOffset.x", T::Float},
        {"m_FerryEntryOffset.y", T::Float},
        {"m_FerryExitOffset.x", T::Float},
        {"m_FerryExitOffset.y", T::Float},
        {"m_iFerryEntryDir", T::UInt},
        {"m_iFerryExitDir", T::UInt},
        {"m_bGotGrid", T::Bool},
        {"m_CurrGridXY.x", T::UInt},
        {"m_CurrGridXY.y", T::UInt},
};

constexpr FieldSpec kShark[] = {
        {"m_dwAlertRange", T::Int},
        {"m_dwSniffRange", T::Int},
        {"m_SwimCircleRect[0]", T::UInt},
        {"m_SwimCircleRect[1]", T::UInt},
        {"m_SwimCircleRect[2]", T::UInt},
        {"m_SwimCircleRect[3]", T::UInt},
        {"m_dwActionState", T::Int},
        {"m_dwMyVictim", T::Int},
        {"m_dwGuestsEaten", T::Int},
        {"m_dwOwner", T::Int},
};

constexpr FieldSpec kPerformerHead[] = {{"m_dwActionState", T::Int}, {"m_iType", T::UInt}, {"m_OwnerID", T::Int}};
constexpr FieldSpec kActionPoint[] = {{"m_ActionPoint.x", T::Float}, {"m_ActionPoint.y", T::Float}, {"m_ActionPoint.z", T::Float}};

// Object type byte -> class, as the object manager creates them. Type 19 saves and loads through the service staff methods.
constexpr const char* kObjectClasses[] = {
        nullptr,
        "Scenery",
        "BuildingObject",
        "GuestObject",
        "Vehicle",
        "Furniture",
        "ServiceStaffObject",
        "LifeguardObject",
        "MechanicObject",
        "RepObject",
        "BuilderObject",
        "CleanerObject",
        "SecurityGuardObject",
        "Shark",
        "Cabaret",
        "Singer",
        "WeightLifter",
        "Catwalk",
        "WetTShirt",
        "SnootyStaffObject",
        "Aerobic",
};
constexpr uint8_t kLastObjectType = 20;
enum ObjectType : uint8_t {
    kTypeScenery = 1,
    kTypeBuilding,
    kTypeGuest,
    kTypeVehicle,
    kTypeTable,
    kTypeServiceStaff,
    kTypeLifeguard,
    kTypeMechanic,
    kTypeRep,
    kTypeBuilder,
    kTypeCleaner,
    kTypeSecurityGuard,
    kTypeShark,
    kTypeCabaret,
    kTypeSinger,
    kTypeWeightLifter,
    kTypeCatwalk,
    kTypeWetTShirt,
    kTypeSnootyStaff,
    kTypeAerobic
};

// ---- Reader ----

std::string Indexed(std::string_view prefix, std::string_view list, size_t index)
{
    return std::string(prefix) + std::string(list) + "[" + std::to_string(index) + "].";
}

// The old record stream as a RecordSource: peeks look at the next records' tags; labels are the reader's own.
class StreamSource final : public RecordSource {
public:
    StreamSource(SaveStream& stream, size_t size, std::string& error) : m_stream(stream), m_error(error), m_size(size) {}

    bool Take(OldBlock& block, std::string name, SaveTag tag, uint32_t count, SaveRecord* out) override
    {
        SaveRecord record;
        if (!m_stream.Next(record, m_error)) {
            if (m_error.empty()) Fail(m_stream.Offset(), block, name, "missing record");
            return false;
        }
        if (record.tag != tag)
            return Fail(record.offset, block, name, std::string("expected ") + SaveTagName(tag) + ", found " + SaveTagName(record.tag));
        if (count != 0 && record.count != count)
            return Fail(record.offset, block, name, "expected " + std::to_string(count) + " items, found " + std::to_string(record.count));
        block.fields.push_back({std::move(name), record});
        if (out) *out = record;
        return true;
    }
    bool Has(const OldBlock&, const std::string&, std::span<const SaveTag> tags) override
    {
        SaveStream copy = m_stream;
        SaveRecord record;
        std::string ignored;
        for (const SaveTag tag : tags)
            if (!copy.Next(record, ignored) || record.tag != tag) return false;
        return true;
    }
    bool Class(OldBlock& block, std::string_view name) override
    {
        SaveRecord record;
        block.kind = std::string(name);
        if (!m_stream.Next(record, m_error)) {
            if (m_error.empty()) Fail(m_stream.Offset(), block, "class", "missing section");
            return false;
        }
        block.offset = record.offset;
        if (record.tag != SaveTag::Class || record.ClassName() != name)
            return Fail(record.offset, block, "class", "expected class " + std::string(name));
        return true;
    }
    bool Fail(size_t offset, const OldBlock& block, std::string_view field, std::string_view why) override
    {
        m_error = "offset " + std::to_string(offset) + ": " + block.kind + "." + std::string(field) + ": " + std::string(why);
        return false;
    }
    size_t Offset() const override { return m_stream.Offset(); }
    size_t Remaining() const override { return m_size - m_stream.Offset(); }

private:
    SaveStream& m_stream;
    std::string& m_error;
    size_t m_size;
};

// The emitters' walk over any RecordSource: tables, counted lists and optional parts.
class BodyReader {
public:
    explicit BodyReader(RecordSource& source) : m_source(source) {}

    // Whether the optional or repeated part that starts with `specs` (labels under `prefix`) follows.
    bool NextAre(const OldBlock& block, const std::string& prefix, std::span<const FieldSpec> specs)
    {
        SaveTag tags[16];
        const size_t n = std::min(specs.size(), std::size(tags));
        for (size_t i = 0; i < n; ++i) tags[i] = specs[i].tag;
        return m_source.Has(block, prefix + specs[0].name, std::span<const SaveTag>(tags, n));
    }
    bool NextIs(const OldBlock& block, const std::string& label, SaveTag tag)
    {
        return m_source.Has(block, label, std::span<const SaveTag>(&tag, 1));
    }
    size_t Remaining() const { return m_source.Remaining(); }

    bool Take(OldBlock& block, std::string name, SaveTag tag, uint32_t count = 1, SaveRecord* out = nullptr)
    {
        return m_source.Take(block, std::move(name), tag, count, out);
    }
    bool Table(OldBlock& block, std::string_view prefix, std::span<const FieldSpec> specs)
    {
        for (const FieldSpec& spec : specs)
            if (!Take(block, std::string(prefix) + spec.name, spec.tag, spec.count)) return false;
        return true;
    }
    // A list length written before its elements. Checked against the remaining bytes (every record is at least two bytes)
    // before anything loops over it; signed counts must not be negative.
    bool Count(OldBlock& block, std::string name, SaveTag tag, uint32_t recordsPerItem, uint32_t& count)
    {
        SaveRecord record;
        if (!Take(block, name, tag, 1, &record)) return false;
        count = record.Bits();
        if (tag == SaveTag::Int && count > INT32_MAX) return Fail(record.offset, block, name, "negative count");
        const size_t remaining = Remaining() / 2;
        if (recordsPerItem && count > remaining / recordsPerItem)
            return Fail(record.offset, block, name, "count " + std::to_string(count) + " exceeds the remaining records");
        return true;
    }
    bool Class(OldBlock& block, std::string_view name) { return m_source.Class(block, name); }
    size_t Offset() const { return m_source.Offset(); }
    bool Fail(size_t offset, const OldBlock& block, std::string_view field, std::string_view why)
    {
        return m_source.Fail(offset, block, field, why);
    }

private:
    RecordSource& m_source;
};

// ---- Sections ----

bool ReadGrid(BodyReader& r, OldBlock& b)
{
    if (!r.Class(b, "WorldMap") || !r.Take(b, "sizeof", T::UInt) || !r.Take(b, "m_IslandSize", T::Int)) return false;
    // One `Closed` flag per beach area, then per sea area; the split needs the map, so both carry one label here.
    for (size_t i = 0; r.NextIs(b, "m_Points->Closed[" + std::to_string(i) + "]", T::UInt); ++i)
        if (!r.Take(b, "m_Points->Closed[" + std::to_string(i) + "]", T::UInt)) return false;
    // The cells in row order, one flags dword and one height word each.
    while (r.NextIs(b, "m_uiFlags", T::Dword))
        if (!r.Take(b, "m_uiFlags", T::Dword) || !r.Take(b, "m_usZOffset", T::Word)) return false;
    return true;
}

bool ReadBoat(BodyReader& r, OldBlock& b, const std::string& prefix)
{
    if (!r.Table(b, prefix, kBoat)) return false;
    for (int i = 0; i < 20; ++i)
        if (!r.Take(b, prefix + "GuestKeyword[" + std::to_string(i) + "]", T::String, 0)) return false;
    return r.Table(b, prefix, kBoatTail);
}

constexpr uint32_t kBoatRecords = 27;

bool ReadFerry(BodyReader& r, OldBlock& b, OldBlock& events)
{
    uint32_t count = 0;
    if (!r.Class(b, "FerryManager") || !r.Take(b, "sizeof", T::UInt) || !r.Count(b, "List size", T::UInt, kBoatRecords, count))
        return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!ReadBoat(r, b, Indexed("", "Boat", i))) return false;
    SaveRecord cursor;
    if (!r.Take(b, "Current It", T::UInt, 1, &cursor)) return false;
    if (cursor.Bits() != 0xffffffffu && cursor.Bits() >= count) return r.Fail(cursor.offset, b, "Current It", "cursor past the schedule");
    if (!r.Count(b, "Now list size", T::UInt, kBoatRecords, count)) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!ReadBoat(r, b, Indexed("", "NowBoat", i))) return false;
    if (!r.Table(b, "", kFerryTail)) return false;

    // The event manager's data follows without a class record.
    events.kind = "EventManager";
    events.offset = r.Offset();
    if (!r.Take(events, "m_dwNextEvent", T::Int) || !r.Take(events, "m_dwNumEvents", T::Int) ||
            !r.Take(events, "m_dwNumCurEvents", T::Int) || !r.Count(events, "m_CurEvents.size()", T::UInt, 1, count))
        return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!r.Take(events, "EventID[" + std::to_string(i) + "]", T::Int)) return false;
    return r.Take(events, "m_dwDayOffset", T::Int) && r.Take(events, "m_dwCurDayOffset", T::Int);
}

bool ReadRepair(BodyReader& r, OldBlock& b)
{
    SaveRecord size;
    if (!r.Class(b, "RepairManager") || !r.Take(b, "sizeof", T::UInt, 1, &size)) return false;
    uint32_t count = 0;
    for (const char* queue : {"BuildingJob", "SceneryJob"}) {
        if (!r.Count(b, std::string(queue) + " list size", T::UInt, std::size(kRepairJob), count)) return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!r.Table(b, Indexed("", queue, i), kRepairJob)) return false;
    }
    if (!r.Count(b, "ActiveJob list size", T::UInt, std::size(kRepairActive), count)) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!r.Table(b, Indexed("", "ActiveJob", i), kRepairActive)) return false;
    return r.Table(b, "", kRepairTail);
}

bool ReadFeedbackText(BodyReader& r, OldBlock& b, const std::string& prefix)
{
    SaveRecord set;
    if (!r.Take(b, prefix + "pwText set", T::Bool, 1, &set)) return false;
    return set.Bits() ? r.Take(b, prefix + "pwText", T::WideString, 0) : r.Take(b, prefix + "pText", T::String, 0);
}

bool ReadFeedback(BodyReader& r, OldBlock& b)
{
    uint32_t count = 0;
    if (!r.Class(b, "FeedbackManager") || !r.Take(b, "sizeof", T::UInt) || !r.Count(b, "List size", T::UInt, 3, count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string prefix = Indexed("", "Pending", i);
        if (!r.Take(b, prefix + "ID", T::Int) || !ReadFeedbackText(r, b, prefix)) return false;
    }
    if (!r.Count(b, "Buffer size", T::UInt, 5, count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string prefix = Indexed("", "History", i);
        if (!r.Take(b, prefix + "ID", T::Int) || !r.Take(b, prefix + "day", T::Int) || !r.Take(b, prefix + "time", T::Float) ||
                !ReadFeedbackText(r, b, prefix))
            return false;
    }
    return r.Table(b, "", kFeedbackTail);
}

bool ReadMessages(BodyReader& r, OldBlock& b)
{
    uint32_t count = 0;
    if (!r.Class(b, "MessageHandler") || !r.Take(b, "sizeof", T::UInt) ||
            !r.Count(b, "Delayed list size", T::UInt, std::size(kDelayedMessage), count))
        return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!r.Table(b, Indexed("", "DelayedMessage", i), kDelayedMessage)) return false;
    return true;
}

bool ReadMapView(BodyReader& r, OldBlock& b)
{
    uint32_t count = 0;
    if (!r.Class(b, "ZEngine") || !r.Table(b, "", kMapViewHead) || !r.Count(b, "burnqueue size", T::UInt, 1, count)) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!r.Take(b, "burnqueue entry[" + std::to_string(i) + "]", T::UInt)) return false;
    return r.Table(b, "", kMapViewTail);
}

bool ReadCleaning(BodyReader& r, OldBlock& b)
{
    uint32_t count = 0;
    if (!r.Class(b, "CleaningManager") || !r.Take(b, "sizeof", T::UInt) ||
            !r.Count(b, "Job list size", T::UInt, std::size(kCleaningJob), count))
        return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!r.Table(b, Indexed("", "Job", i), kCleaningJob)) return false;
    if (!r.Count(b, "ActiveJob list size", T::UInt, std::size(kCleaningActive), count)) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!r.Table(b, Indexed("", "ActiveJob", i), kCleaningActive)) return false;
    return r.Table(b, "", kCleaningTail);
}

bool ReadConstruction(BodyReader& r, OldBlock& b, OldBlock& weather)
{
    uint32_t count = 0;
    if (!r.Class(b, "ConstructionManager") || !r.Take(b, "sizeof", T::UInt) || !r.Count(b, "List size", T::UInt, 43, count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string prefix = Indexed("", "Job", i);
        uint32_t hoardings = 0;
        if (!r.Table(b, prefix, kConstructionJobHead) || !r.Count(b, prefix + "m_NumHoardings", T::Int, std::size(kHoarding), hoardings))
            return false;
        for (uint32_t j = 0; j < hoardings; ++j)
            if (!r.Table(b, Indexed(prefix, "Hoarding", j), kHoarding)) return false;
        if (!r.Table(b, prefix, kConstructionJobTimes)) return false;
        for (uint32_t j = 0; j < 10; ++j)
            if (!r.Table(b, Indexed(prefix, "BuilderConstPoint", j), kBuilderPoint)) return false;
    }
    if (!r.Take(b, "m_ConstructionCount", T::Int)) return false;
    SaveRecord builders;
    if (!r.Take(b, "m_NumberOfBuilders", T::Int, 1, &builders)) return false;
    // Written as is, followed by at most kMaxConstructionBuilders entries (compared unsigned).
    count = std::min(builders.Bits(), kMaxConstructionBuilders);
    for (uint32_t i = 0; i < count; ++i)
        if (!r.Table(b, Indexed("", "Builder", i), kConstructionBuilder)) return false;

    // The weather data follows without a class record.
    weather.kind = "Weather";
    weather.offset = r.Offset();
    return r.Table(weather, "", kWeather);
}

// ---- Objects ----

bool ReadSprite(BodyReader& r, OldBlock& b, const std::string& prefix)
{
    return r.Table(b, prefix, kSprite);
}

bool ReadEntity(BodyReader& r, OldBlock& b)
{
    return r.Table(b, "", kEntityHead) && ReadSprite(r, b, "sprite.") && r.Table(b, "", kEntityTail);
}

bool ReadProp(BodyReader& r, OldBlock& b)
{
    b.sharedFrom = b.fields.size();
    return r.Table(b, "", kProp) && ReadEntity(r, b);
}

bool ReadMover(BodyReader& r, OldBlock& b)
{
    b.sharedFrom = b.fields.size();
    SaveRecord onPath;
    if (!r.Table(b, "", kMovingHead) || !r.Take(b, "on path", T::UInt, 1, &onPath)) return false;
    if (onPath.Bits()) {
        uint32_t steps = 0;
        if (!r.Table(b, "path.", kPathDataHead) || !r.Count(b, "path.m_iNumSteps", T::UInt, 2, steps)) return false;
        for (uint32_t i = 0; i < steps; ++i) {
            const std::string prefix = Indexed("path.", "m_pDirections", i);
            if (!r.Take(b, prefix + "x", T::UInt) || !r.Take(b, prefix + "y", T::UInt)) return false;
        }
        if (!r.Table(b, "path.", kPathDataTail)) return false;
    }
    return r.Table(b, "", kMovingTail) && ReadEntity(r, b);
}

// A sprite attached by the script: a presence flag, then the sprite's name and records.
bool ReadAttached(BodyReader& r, OldBlock& b, const char* label)
{
    SaveRecord present;
    if (!r.Take(b, label, T::UInt, 1, &present)) return false;
    if (!present.Bits()) return true;
    return r.Take(b, "attached.Sprite name", T::String, 0) && ReadSprite(r, b, "attached.");
}

bool ReadBuildingEvents(BodyReader& r, OldBlock& b)
{
    if (!r.Take(b, "events.m_OwnerID", T::Int) || !r.Take(b, "events.m_bEventActive", T::Bool)) return false;
    for (uint32_t slot = 0; slot < 3; ++slot) {
        const std::string prefix = Indexed("events.", "Slot", slot);
        uint32_t count = 0;
        if (!r.Take(b, prefix + "Type", T::UInt) || !r.Take(b, prefix + "NumActors", T::UInt) ||
                !r.Count(b, prefix + "ActorSize", T::UInt, 1, count))
            return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!r.Take(b, prefix + "ActorID[" + std::to_string(i) + "]", T::Int)) return false;
        if (!r.Take(b, prefix + "NumGuests", T::UInt) || !r.Count(b, prefix + "GuestSize", T::UInt, 1, count)) return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!r.Take(b, prefix + "GuestID[" + std::to_string(i) + "]", T::Int)) return false;
        if (!r.Table(b, prefix, kEventSlotTimes)) return false;
    }
    return true;
}

bool ReadBuilding(BodyReader& r, OldBlock& b)
{
    if (!r.Table(b, "", kBuilding)) return false;
    // The animation sprites (as many as the building definition names) and an optional extra one. A sprite starts with a
    // float; the flags after them are booleans, so the count follows from the records without the catalog.
    for (size_t i = 0; r.NextIs(b, Indexed("", "sprites", i) + kSprite[0].name, T::Float); ++i)
        if (!ReadSprite(r, b, Indexed("", "sprites", i))) return false;
    SaveRecord events;
    if (!r.Table(b, "", kBuildingFlags) || !r.Take(b, "Events", T::UInt, 1, &events)) return false;
    if (events.Bits() && !ReadBuildingEvents(r, b)) return false;
    uint32_t thoughts = 0;
    if (!r.Count(b, "m_ThoughtList.size()", T::UInt, 3, thoughts)) return false;
    for (uint32_t i = 0; i < thoughts; ++i) {
        const std::string prefix = Indexed("", "m_ThoughtList", i);
        if (!r.Take(b, prefix + "ID", T::Int) || !r.Take(b, prefix + "TextLength", T::UInt) ||
                !r.Take(b, prefix + "pText", T::WideString, 0))
            return false;
    }
    SaveRecord queue;
    if (!r.Take(b, "m_QueueValue", T::Bool, 1, &queue)) return false;
    if (queue.Bits()) {
        uint32_t count = 0;
        if (!r.Table(b, "queue.", kQueueHead) || !r.Count(b, "queue.m_qlQueue.size()", T::UInt, 1, count)) return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!r.Take(b, "queue.m_qlQueue[" + std::to_string(i) + "]", T::Int)) return false;
        if (!r.Count(b, "queue.m_qlCommmod.size()", T::UInt, 1, count)) return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!r.Take(b, "queue.m_qlCommmod[" + std::to_string(i) + "]", T::Int)) return false;
    }
    return r.Table(b, "", kBuildingTail) && ReadProp(r, b);
}

bool ReadTable(BodyReader& r, OldBlock& b)
{
    // One chair per seat of the definition, each with its sprite when the table has chair sprites. A chair starts
    // bool/int/uint/uint/bool, the waiter fields after the chairs bool/int/uint/int/bool.
    size_t chairs = 0;
    for (; r.NextAre(b, Indexed("", "Chair", chairs), kTableChair); ++chairs) {
        const std::string prefix = Indexed("", "Chair", chairs);
        if (!r.Table(b, prefix, kTableChair)) return false;
        if (r.NextIs(b, prefix + "sprite." + kSprite[0].name, T::Float) && !ReadSprite(r, b, prefix + "sprite.")) return false;
    }
    if (!r.Table(b, "", kTableTail)) return false;
    size_t plates = 0;
    for (; r.NextIs(b, Indexed("", "Plate", plates) + "sprite." + kSprite[0].name, T::Float); ++plates) {
        const std::string prefix = Indexed("", "Plate", plates);
        if (!ReadSprite(r, b, prefix + "sprite.") || !r.Take(b, prefix + "m_bDrawPlate", T::Bool)) return false;
    }
    if (plates && plates != chairs) return r.Fail(r.Offset(), b, "Plate", "plate count differs from the chair count");
    return ReadProp(r, b);
}

bool ReadStaff(BodyReader& r, OldBlock& b)
{
    return r.Table(b, "", kStaff) && ReadMover(r, b);
}

bool ReadVehicle(BodyReader& r, OldBlock& b)
{
    if (!r.Table(b, "", kVehicleHead)) return false;
    for (uint32_t i = 0; i < kVehicleWayPoints; ++i) {
        const std::string prefix = Indexed("", "m_WayPoints", i);
        if (!r.Take(b, prefix + "x", T::Float) || !r.Take(b, prefix + "y", T::Float) || !r.Take(b, prefix + "z", T::Float)) return false;
    }
    if (!r.Table(b, "", kVehicleMiddle)) return false;
    // The sprites present among five; the type after them is a uint, a sprite starts with a float.
    for (size_t i = 0; i < kVehicleSprites && r.NextIs(b, Indexed("", "sprites", i) + kSprite[0].name, T::Float); ++i)
        if (!ReadSprite(r, b, Indexed("", "sprites", i))) return false;
    return r.Table(b, "", kVehicleTail) && ReadMover(r, b);
}

bool ReadPerformer(BodyReader& r, OldBlock& b, uint8_t type)
{
    if (type == kTypeWetTShirt) {
        if (!r.Table(b, "", kPerformerHead) || !r.Take(b, "m_bFinished", T::Bool) || !r.Take(b, "m_Contestent", T::Int) ||
                !r.Table(b, "", kActionPoint))
            return false;
        return ReadMover(r, b);
    }
    const bool finished = type == kTypeCatwalk || type == kTypeWeightLifter;
    if (!r.Table(b, "", kPerformerHead) || !r.Take(b, finished ? "m_bFinished" : "m_bFinish", T::Bool)) return false;
    if (type != kTypeCabaret && !r.Table(b, "", kActionPoint)) return false;
    if ((type == kTypeSinger || type == kTypeWeightLifter) && !ReadAttached(r, b, "m_pAttachedSprite")) return false;
    return ReadMover(r, b);
}

bool ReadObjectFields(BodyReader& r, OldBlock& b, uint8_t type)
{
    switch (type) {
    case kTypeScenery: return r.Table(b, "", kScenery) && ReadProp(r, b);
    case kTypeBuilding: return ReadBuilding(r, b);
    case kTypeGuest:
        return r.Table(b, "", kGuestHead) && ReadAttached(r, b, "attached sprite") && r.Table(b, "", kGuestTail) && ReadMover(r, b);
    case kTypeVehicle: return ReadVehicle(r, b);
    case kTypeTable: return ReadTable(r, b);
    case kTypeServiceStaff:
    case kTypeSnootyStaff: return r.Table(b, "", kServiceStaff) && ReadStaff(r, b);
    case kTypeLifeguard:
        return r.Table(b, "", kLifeguardHead) && ReadAttached(r, b, "attached sprite") && r.Table(b, "", kLifeguardTail) && ReadStaff(r, b);
    case kTypeMechanic: return r.Table(b, "", kMechanic) && ReadStaff(r, b);
    case kTypeRep: return r.Table(b, "", kRepHead) && ReadAttached(r, b, "attached sprite") && r.Table(b, "", kRepTail) && ReadStaff(r, b);
    case kTypeBuilder: return r.Table(b, "", kBuilder) && ReadStaff(r, b);
    case kTypeCleaner: return r.Table(b, "", kCleaner) && ReadStaff(r, b);
    case kTypeSecurityGuard: return r.Table(b, "", kSecurityGuard) && ReadStaff(r, b);
    case kTypeShark: return r.Table(b, "", kShark) && ReadMover(r, b);
    default: return ReadPerformer(r, b, type);
    }
}

bool ReadObjects(BodyReader& r, const OldBlock& manager, std::vector<OldBlock>& objects)
{
    // The three counts of the header; buildings and guests are loaded as their class whatever their type byte says.
    const uint32_t counts[3] = {manager.fields[2].record.Bits(), manager.fields[3].record.Bits(), manager.fields[4].record.Bits()};
    const uint8_t fixed[3] = {kTypeBuilding, kTypeGuest, 0};
    // Every object is at least its header and a Entity (over 30 records of two bytes or more).
    constexpr uint64_t kMinObjectBytes = 60;
    uint64_t total = 0;
    for (uint32_t count : counts) total += count;
    if (counts[0] > INT32_MAX || counts[1] > INT32_MAX || counts[2] > INT32_MAX || total * kMinObjectBytes > r.Remaining())
        return r.Fail(manager.offset, manager, "counts", "object counts exceed the input");
    for (int group = 0; group < 3; ++group) {
        for (uint32_t i = 0; i < counts[group]; ++i) {
            OldBlock object;
            object.offset = r.Offset();
            object.kind = group == 0 ? "BuildingObject" : group == 1 ? "GuestObject" : "Object";
            SaveRecord type;
            if (!r.Take(object, "Object type", T::Byte, 1, &type) || !r.Take(object, "Object Name", T::String, 0)) return false;
            object.type = static_cast<uint8_t>(type.Bits());
            uint8_t cls = fixed[group];
            if (!cls) {
                // The loader's switch for the others has no building/guest case.
                if (object.type < 1 || object.type > kLastObjectType || object.type == kTypeBuilding || object.type == kTypeGuest)
                    return r.Fail(type.offset, object, "Object type", "unsupported type " + std::to_string(object.type));
                cls = object.type;
            }
            object.kind = kObjectClasses[cls];
            if (!ReadObjectFields(r, object, cls)) return false;
            objects.push_back(std::move(object));
        }
    }
    return true;
}

} // namespace

const OldField* OldBlock::Find(std::string_view name) const
{
    for (const OldField& field : fields)
        if (field.name == name) return &field;
    return nullptr;
}

bool ReadOldSaveBody(RecordSource& source, const OldSaveHeader& header, OldSaveBody& body)
{
    BodyReader r(source);
    OldSaveBody parsed;
    parsed.header = header;
    parsed.storage = body.storage; // the source's records may live there
    auto add = [&](const char* kind) -> OldBlock& {
        parsed.sections.emplace_back();
        parsed.sections.back().kind = kind;
        return parsed.sections.back();
    };
    parsed.sections.reserve(16);
    OldBlock& world = add("World");
    world.offset = parsed.header.worldOffset;
    if (!r.Table(world, "", kWorld) || !ReadGrid(r, add("WorldMap"))) return false;
    OldBlock& path = add("Path");
    if (!r.Class(path, "Path") || !r.Take(path, "sizeof", T::UInt)) return false;
    OldBlock& manager = add("ObjectManager");
    if (!r.Class(manager, "ObjectManager") || !r.Table(manager, "", kObjectManager) || !ReadObjects(r, manager, parsed.objects))
        return false;
    OldBlock& player = add("Player");
    if (!r.Class(player, "Player") || !r.Table(player, "", kPlayer)) return false;
    OldBlock& ferry = add("FerryManager");
    if (!ReadFerry(r, ferry, add("EventManager")) || !ReadRepair(r, add("RepairManager")) || !ReadFeedback(r, add("FeedbackManager")) ||
            !ReadMessages(r, add("MessageHandler")) || !ReadMapView(r, add("ZEngine")) || !ReadCleaning(r, add("CleaningManager")))
        return false;
    OldBlock& construction = add("ConstructionManager");
    if (!ReadConstruction(r, construction, add("Weather"))) return false;
    body = std::move(parsed);
    return true;
}

bool ReadOldSaveBody(std::span<const uint8_t> bytes, OldSaveBody& body, std::string& error)
{
    body = {};
    SaveStream stream(bytes);
    OldSaveHeader header;
    if (!ReadOldSaveHeader(stream, header, error)) return false;
    StreamSource source(stream, bytes.size(), error);
    OldSaveBody parsed;
    if (!ReadOldSaveBody(source, header, parsed)) return false;
    if (source.Remaining() != 0) {
        error = "offset " + std::to_string(source.Offset()) + ": records after the weather section";
        return false;
    }
    body = std::move(parsed);
    return true;
}

namespace {

int32_t Signed(const OldField& field)
{
    return std::bit_cast<int32_t>(field.record.Bits());
}

const OldBlock* Section(const OldSaveBody& body, std::string_view kind)
{
    for (const OldBlock& block : body.sections)
        if (block.kind == kind) return &block;
    return nullptr;
}

bool HasSuffix(std::string_view name, std::string_view suffix)
{
    return name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix;
}

} // namespace

std::vector<std::string> CheckOldSaveBody(const OldSaveBody& body)
{
    std::vector<std::string> problems;
    const OldBlock* manager = Section(body, "ObjectManager");
    if (!manager || manager->fields.size() < std::size(kObjectManager)) return {"no ObjectManager section"};
    const uint32_t buildings = manager->fields[2].record.Bits(), guests = manager->fields[3].record.Bits();
    if (uint64_t(manager->fields[1].record.Bits()) != uint64_t(body.objects.size()))
        problems.push_back("ObjectManager: m_dwNumActiveObjects " + std::to_string(manager->fields[1].record.Bits()) + " but " +
                std::to_string(body.objects.size()) + " objects");

    // Ids: the object's own m_id, unique and below the manager's next free id.
    const uint32_t nextFree = manager->fields[6].record.Bits();
    std::map<int32_t, const OldBlock*> ids;
    for (size_t i = 0; i < body.objects.size(); ++i) {
        const OldBlock& object = body.objects[i];
        const uint8_t expected = i < buildings ? kTypeBuilding : i < size_t(buildings) + guests ? kTypeGuest : object.type;
        if (object.type != expected)
            problems.push_back(object.kind + " at " + std::to_string(object.offset) + ": type byte " + std::to_string(object.type));
        const OldField* id = object.Find("m_dwID");
        if (!id) continue;
        const int32_t value = Signed(*id);
        if (value < 0 || uint32_t(value) >= nextFree)
            problems.push_back(object.kind + " at " + std::to_string(object.offset) + ": id " + std::to_string(value) + " outside 0.." +
                    std::to_string(nextFree));
        if (!ids.emplace(value, &object).second) problems.push_back("duplicate object id " + std::to_string(value));
    }

    // References that must name a live object of the right class. -1 is "none" everywhere.
    // `removed`: an id of no object is allowed (a table or a repair/cleaning job that outlived its object, as
    // formats::CheckOblSaveDocument).
    auto check = [&](const OldBlock& from, const OldField& field, const char* kind, bool removed = false) {
        const int32_t value = Signed(field);
        if (value == -1) return;
        const auto it = ids.find(value);
        if (it == ids.end()) {
            if (!removed) problems.push_back(from.kind + "." + field.name + ": no object " + std::to_string(value));
        } else if (kind && it->second->kind != kind)
            problems.push_back(
                    from.kind + "." + field.name + ": object " + std::to_string(value) + " is a " + it->second->kind + ", not a " + kind);
    };
    for (const OldBlock& object : body.objects) {
        const OldField* own = object.Find("m_dwID");
        for (const OldField& field : object.fields) {
            const std::string_view name = field.name;
            if (name == "queue.m_dwOwner" || name == "events.m_OwnerID") {
                if (own && field.record.Bits() != own->record.Bits())
                    problems.push_back(
                            object.kind + "." + field.name + ": " + std::to_string(Signed(field)) + " is not the building's own id");
            } else if (name.starts_with("queue.m_qlQueue[")) {
                check(object, field, "GuestObject");
            } else if (name.starts_with("events.") && (name.find("ActorID[") != name.npos || name.find("GuestID[") != name.npos)) {
                check(object, field, nullptr);
            } else if (object.type == kTypeTable && name == "m_dwOwner") {
                check(object, field, "BuildingObject", true);
            }
        }
    }
    if (const OldBlock* repair = Section(body, "RepairManager"))
        for (const OldField& field : repair->fields)
            if (HasSuffix(field.name, ".RepairID")) check(*repair, field, nullptr, true);
    if (const OldBlock* cleaning = Section(body, "CleaningManager"))
        for (const OldField& field : cleaning->fields)
            if (HasSuffix(field.name, ".CleaningID")) check(*cleaning, field, nullptr, true);
    if (const OldBlock* construction = Section(body, "ConstructionManager"))
        for (const OldField& field : construction->fields) {
            if (field.name.starts_with("Job[") && HasSuffix(field.name, "].BuildID")) check(*construction, field, "BuildingObject");
            if (field.name.starts_with("Builder[") && HasSuffix(field.name, ".ID")) check(*construction, field, "BuilderObject");
        }
    return problems;
}

bool DescribeOldSaveObjects(std::span<const uint8_t> bytes, std::string& report, std::string& error)
{
    report.clear();
    OldSaveBody body;
    if (!ReadOldSaveBody(bytes, body, error)) return false;
    char line[200];
    const auto add = [&](const char* format, auto... args) {
        std::snprintf(line, sizeof(line), format, args...);
        report += line;
    };
    size_t records = 0;
    report += "sections\n";
    for (const OldBlock& section : body.sections) {
        add("  %-20s at %8zu  %6zu records\n", section.kind.c_str(), section.offset, section.fields.size());
        records += section.fields.size();
    }
    const OldBlock* manager = Section(body, "ObjectManager");
    add("objects   %zu: %u buildings, %u guests, %u others\n", body.objects.size(), manager->fields[2].record.Bits(),
            manager->fields[3].record.Bits(), manager->fields[4].record.Bits());
    std::map<std::string, std::pair<size_t, size_t>> classes; // class -> objects, records
    for (const OldBlock& object : body.objects) {
        auto& entry = classes[object.kind];
        ++entry.first;
        entry.second += object.fields.size();
        records += object.fields.size();
    }
    for (const auto& [kind, entry] : classes) add("  %-20s %6zu  %7zu records\n", kind.c_str(), entry.first, entry.second);
    add("records   %zu typed\n", records);
    const auto problems = CheckOldSaveBody(body);
    add("checks    %s\n", problems.empty() ? "ok" : (std::to_string(problems.size()) + " problems").c_str());
    for (const std::string& problem : problems) report += "  " + problem + "\n";
    return true;
}

bool LoadOblSaveDocument(const nlohmann::json& document, const nlohmann::json& catalogIndex, const SaveOrigin& origin,
        const json_schema::Registry& schemas, OldSaveBody& body, std::vector<std::string>& warnings, std::string& error, uint32_t gridSide)
{
    if (!formats::ValidateOblSaveDocument(document, origin, schemas, warnings, error, gridSide)) return false;
    nlohmann::json filled = document;
    formats::FillSaveDefaults(filled, schemas);
    return OldSaveBodyFromDocument(filled, catalogIndex, origin, body, error);
}

} // namespace obl::convert
