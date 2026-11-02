#pragma once

#include "core/char16.h"
#include <cstdint>

namespace obl::gui_variable {

inline constexpr int kTextCapacity = 1026;

// World::m_{Total,Today}Guest{Arrive,Depart,UnhappyDepart}[2]: guests of the two kinds (boat / bus) the ferry panel counts.
enum class GuestFlowField {
    TotalArrive,
    TotalDepart,
    TodayArrive,
    TodayDepart,
    TotalUnhappyDepart,
    TodayUnhappyDepart,
};

enum class MoneyField {
    Income,
    DemolishIncome,
    FineIncome,
    AccomIncome,
    AttractIncome,
    RetailIncome,
    Costs,
    ConstructCost,
    SceneryCost,
    WagesCost,
    StockCost,
    AverageProfit,
    AverageCost,
    AverageIncome,
    TotalFines, // World::m_totalFines, the FINE_INCOME_$ of the scenario panel
};

enum class WorldCounter {
    BuildingsConstructed,
    DiffBuildingsConstructed,
    BarsConstructed,
    BuildingsLeftToBuild,
    PartyCount,
    ShagCount,
    PissedCount,
    GuestsDrowned,
    GuestsEaten,
    UnbuiltAfterStartup,
};

enum class IslandText { Title, Area, Weather, Temp, Hazards };

class Env {
public:
    virtual ~Env() = default;

    virtual float Cash() const = 0; // g_World->m_player->m_cash
    virtual int NumGuests() const = 0; // World::m_NumGuests
    virtual int GameDay() const = 0; // Zero-based simulation day.
    virtual float GameTime() const = 0; // Simulation hours since midnight.
    virtual float MinDistance() const = 0; // Sound::minDistance
    virtual float MaxDistance() const = 0; // Sound::maxDistance
    virtual const Char16* Text(const char* key) const = 0; // the key's text in the loaded text table
    virtual int WholeHour(float time) const = 0; // the whole hour of a game time
    virtual int QuarterMinutes(float time) const = 0; // its minutes rounded down to a quarter (0, 15, 30 or 45)

    virtual float PowerOutput() const = 0; // World::m_currentPower
    virtual float PowerDemand() const = 0; // World::m_powerDemand
    virtual float PowerUse() const = 0; // World::m_powerUse
    virtual int WeatherColor() const = 0; // Weather::m_ManualCol, 0x00RRGGBB (the weather's override colour getters unpack it)
    virtual int LitterCount() const = 0; // Current island litter count.
    virtual int ObjectCount(int objectClass) const = 0; // the object manager's count of that class
    virtual int SceneryNonLitterCount() const = 0; // class 1 objects whose name has no "Litter"
    virtual int GuestGenderCount(bool nonZero) const = 0; // guests whose GuestDef::m_Gender is 0 / not 0
    virtual int GuestFlow(GuestFlowField field, int type) const = 0; // World::m_*Guest* [2] counters, type 0 or 1
    virtual float Money(MoneyField field) const = 0; // World day/average income and cost fields

    // Build panel: GuiManager::m_context[0] / [5] are indices into WorldParams (no range check, like the building definition lookup).
    virtual int SelectedBuilding() const = 0; // GuiManager context [0], the building picked for placing
    virtual int SelectedScenery() const = 0; // GuiManager context [5], the scenery picked in the palette
    virtual const char* BuildingKey(int building) const = 0; // BuildingDef::m_key, the BUILD_<key> of BuildingList.ble
    virtual int BuildingCost(int building) const = 0; // BuildingDef::m_Cost2Build
    virtual int BuildingPower(int building) const = 0; // BuildingDef::m_Power, negative = generator
    virtual const char* BuildingTreeKey(int building) const = 0; // BuildingDef::m_Tree, the building to build first
    virtual const char* BuildingHoursKey(int building) const = 0; // OpeningHoursDef::m_key of BuildingDef::m_OpeningHours
    virtual int BuildingIdByKey(const char* key) const = 0; // the WorldParams building id of a key (a miss is a fatal error)
    virtual bool BuildingAllowed(int building) const = 0; // the world's allowed-building check
    virtual bool HasIdleBuilding(const char* key) const = 0; // a built building of that type with no build/demolish job
    virtual int BuildingCommodityCount(int building) const = 0; // BuildingDef::m_NumCommodities
    virtual float BuildingAppeal(int building, int slot, bool female) const = 0; // BuildingDef::m_CommodityAB[slot][female]
    virtual const char* SceneryKey(int scenery) const = 0; // SceneryDef::m_key
    virtual int SceneryCost(int scenery) const = 0; // SceneryDef::m_Cost2Build

    virtual const char* ScenarioDescriptionKey() const = 0; // World::m_scenarioDescription
    virtual const char* ScenarioObjectivesKey() const = 0; // World::m_scenarioObjectives
    virtual const char* ScenarioTitleKey() const = 0; // World::m_scenarioTitle
    virtual uint32_t ObjectiveCount() const = 0; // Number of scenario objectives.
    virtual const char* ObjectiveKey(uint32_t index) const = 0;
    virtual uint32_t ObjectiveStatus(uint32_t index) const = 0; // World::m_objectiveStatus[index], 1 = done
    virtual int WorldCounterValue(WorldCounter counter) const = 0;
    virtual bool SandboxMode() const = 0; // Game::m_sandbox
    virtual int CurrentScenario() const = 0; // Game::m_lastUnlockedScenario, 0-based
    virtual bool ScenarioCompleted(int scenario) const = 0; // the player has completed that scenario
    virtual bool ScenarioAlwaysAvailable(int scenario) const = 0; // that scenario is marked as always available
    virtual int GuestCapacityOfLodgings() const = 0; // BuildingDef::m_GuestCapacity summed over ACCOMBLK and HOTEL objects
    virtual int MeasureWidth(const Char16* text) const = 0;

    // The ferry schedule's next boat; writes `day` only for a non-empty schedule.
    virtual double NextBoatTime(int todayOnly, uint32_t* day) const = 0;
    virtual double BoatTimeToday(int index) const = 0; // the index-th ferry boat of today, -1 past the end
    // Some object with that name exists: a WHARF turns the bus captions into ferry ones.
    virtual bool ObjectExists(const char* key) const = 0;

    virtual const char* IslandKey(IslandText text) const = 0; // World::m_island*, a text-table key
    virtual const char* ScriptTextKey() const = 0; // GuiManager::m_scriptTextKey
    virtual const char* MessageTextKey() const = 0; // GuiManager::m_messageTextKey
    virtual double GameVersion() const = 0;
    // gui_tables::GetHeadingKey: the heading text key of the active table filter, null if none.
    virtual const char* FilterHeadingKey() const = 0;
    virtual int BrowserCount() const = 0; // GuiManager context [6], the count the OB_NUM_* captions show

    virtual int StaffCategory() const = 0; // GuiManager context [10]: 0..5 one kind of staff, 6 all of them
    virtual int StaffCountAll() const = 0; // count of the staff without service staff
    virtual int StaffOnPayroll(int objectClass) const = 0; // objects of that class whose Staff::m_gotTheSack is 0
    virtual int IdleBuildingCount(const char* key) const = 0; // built buildings of that type with no build/demolish job
    virtual int BuildingMaxStaff(int building) const = 0; // BuildingDef::m_MaxStaff

    virtual bool ProviderText(const char* key, Char16* out) const = 0;

    using Swprintf = int (*)(Char16* out, const Char16* format, ...);
    virtual Swprintf Format() const = 0;
};

struct Output {
    Char16 text[kTextCapacity];
    uint32_t color; // starts as the widget colour; a negative amount turns it red
    int xOffset; // the scenario captions leave room for a heading: the draw rectangle starts this many pixels further right
    bool undefined;
    bool fromProvider; // the key is none of the table's: the text came from the variable provider
};

bool Evaluate(const char* key, const Env& env, uint32_t widgetColor, Output& out);

// The keys of the table (not the provider's), for tests that walk them.
int HandledKeyCount();
const char* HandledKey(int index);

} // namespace obl::gui_variable

namespace obl::ui {
struct GuiVariable;
} // namespace obl::ui
