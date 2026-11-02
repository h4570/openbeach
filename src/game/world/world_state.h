#pragma once

#include "game/player/player_state.h"

#include <cstdint>
#include <memory>
#include <string>

// The World's own state as named groups. Field names m_* are the ones used by
// WriteWorldJson; array lengths come from its array counts. The groups are base structs, so a reader still writes `world.m_gameTime` while
// the save field table (world_save.h) and the tests can address one group at a time.
namespace obl::audio {
struct ScriptedSound;
}
namespace obl::feedback {
struct Manager;
}
namespace obl::repair {
struct Manager;
}
namespace obl::cleaning {
struct Manager;
}
namespace obl::ferry {
struct Manager;
}
namespace obl::messages {
struct Handler;
}
namespace obl::disaster {
struct Manager;
}
namespace obl::path {
struct Path;
struct PathTiles;
} // namespace obl::path
namespace obl::weather {
struct Weather;
}
namespace obl::map {
struct Grid;
}
namespace obl::object {
struct Manager;
}
namespace obl::construction {
struct Manager;
}
namespace obl::commodity {
class Providers;
}
namespace obl::tables {
struct WorldParams;
}
namespace obl::world_script {
struct WorldScript; // the scenario script (world_script_types.h)
}
namespace obl::world_script_host {
class WorldScriptHost;
}
namespace obl::ui {
struct MapView; // game/ui/map_view.h
struct SoftwareCursor;
using Cursor = SoftwareCursor;
struct Input;
} // namespace obl::ui

namespace obl::world_data {
using MapView = ui::MapView; // the map view
} // namespace obl::world_data

namespace obl::world {

// The subsystems in the order the World frees them (the teardown order of DestroyWorld, kept by ~World).
enum class Subsystem {
    CommodityProviders,
    Player,
    ObjectManager,
    MessageHandler,
    Path,
    Grid,
    WorldParams,
    EventManager,
    FerryManager,
    RepairManager,
    CleaningManager,
    FeedbackManager,
    MapView,
    Weather,
    PathTiles,
    ConstructionManager,
    WorldScriptHost,
    ScriptedSound
};

// How a subsystem is freed lives with its module (counters, image globals, the game destructors of the map view and the scripted
// sound), so the host installs one teardown function (world_lifecycle.cpp). Without one nothing is freed: a unit-test fixture lends its
// subsystems to a World on the stack.
using SubsystemTeardown = void (*)(Subsystem which, void* pointer);
void SetSubsystemTeardown(SubsystemTeardown teardown);
void DestroySubsystem(Subsystem which, void* pointer);

template<Subsystem Which> struct SubsystemDeleter {
    void operator()(void* pointer) const { DestroySubsystem(Which, pointer); }
};
// A unique_ptr whose readers still see a plain pointer: the module facades take `T*` and the World keeps the ownership.
template<class T, Subsystem Which> class Owned : public std::unique_ptr<T, SubsystemDeleter<Which>> {
public:
    using std::unique_ptr<T, SubsystemDeleter<Which>>::unique_ptr;
    using std::unique_ptr<T, SubsystemDeleter<Which>>::operator=;
    operator T*() const noexcept { return this->get(); } // NOLINT(google-explicit-constructor)
};

// The subsystems the World creates (world_construct_subsystems.h) and owns. The map view remains opaque;
// the WorldScript is owned by the script host.
struct Subsystems {
    Owned<tables::WorldParams, Subsystem::WorldParams> m_worldParams;
    Owned<messages::Handler, Subsystem::MessageHandler> m_messageHandler;
    Owned<object::Manager, Subsystem::ObjectManager> m_objectManager;
    Owned<map::Grid, Subsystem::Grid> m_grid; // "WorldMap"
    Owned<path::Path, Subsystem::Path> m_path; // g_Path is its alias
    Owned<player::Player, Subsystem::Player> m_player;
    world_script::WorldScript* m_worldScript; // the scenario script; the script host owns it
    Owned<ferry::Manager, Subsystem::FerryManager> m_ferryManager;
    Owned<repair::Manager, Subsystem::RepairManager> m_repairManager;
    Owned<cleaning::Manager, Subsystem::CleaningManager> m_cleaningManager;
    Owned<feedback::Manager, Subsystem::FeedbackManager> m_feedbackManager;
    Owned<ui::MapView, Subsystem::MapView> m_mapView; // "ZEngine"
    Owned<path::PathTiles, Subsystem::PathTiles> m_pathTiles; // g_PathTiles is its alias
    Owned<world_script_host::WorldScriptHost, Subsystem::WorldScriptHost> m_worldScriptHost;
    Owned<weather::Weather, Subsystem::Weather> m_weather;
    Owned<disaster::Manager, Subsystem::EventManager> m_eventManager;
    Owned<construction::Manager, Subsystem::ConstructionManager> m_constructionManager;
    Owned<commodity::Providers, Subsystem::CommodityProviders> m_commodityProviders;
    Owned<audio::ScriptedSound, Subsystem::ScriptedSound> m_scriptedSound;
};

// The game clock, speed and pause.
struct Clock {
    float m_gameStartTime;
    float m_gameTime;
    int m_gameDay;
    float m_gameDayTime; // hour of the day, 0 <= h < 24
    float m_previousDayTime;
    float m_lastGameDayTime;
    float m_gameDeltaTime;
    float m_gameSpeed; // multiplier of real frame time
    int m_gameSpeedLevel; // 0..2, table 1, 2, 8
    float m_nextStaffFeedbackTime;
    uint8_t m_paused;
    uint8_t m_forcedPaused;
};

// The scenario's rules and the values its script reads and writes.
struct Scenario {
    char m_mapId[0x100]; // the pack's map id (maps/<id>/)
    uint8_t m_startup;
    uint8_t m_loadingSave; // set while a save loads (the constructor clears it at its end); the script's commands are silenced meanwhile
    int m_numValidBuildings;
    int m_validBuildingIDs[0x80];
    uint8_t m_breakDownsAllowed;
    uint8_t m_allowLitter;
    int m_scriptState; // the last MT_STATECHANGE the world received (world::ReceiveMessage), the script state it entered
    int m_scriptFlags;
    int m_scriptVars[0x20];
    float m_demoTime; // script variable DemoTime; world::SetScriptValue also sets m_demoTimeSet
    uint8_t m_demoTimeSet;
    uint32_t m_objectiveStatus[4];
    int m_unbuiltAfterStartup;
};

// The PIS texts: text-table keys from the scenario, looked up by the GUI; never saved.
struct Texts {
    std::string m_islandTitle; // PIS island status texts (GUI ISLAND_TITLE/AREA/WEATHER/TEMP/HAZARDS)
    std::string m_islandArea;
    std::string m_islandWeather;
    std::string m_islandTemp;
    std::string m_islandHazards;
    std::string m_scenarioDescription; // PIS SCENARIO_OBJ_DESCRIPTION (GUI SCENARIO_DESCRIPTION)
    std::string m_scenarioObjectives; // PIS SCENARIO_OBJ_OBJECTIVES (GUI SCENARIO_OBJECTIVES)
    uint32_t m_numObjectives; // PIS SCENARIO_OBJ_NUMBER; at most 4 texts are kept
    std::string m_objectiveText[4]; // PIS SCENARIO_OBJ_<n>
    std::string m_scenarioTitle;
};

struct CircuitPoint {
    int32_t x;
    int32_t y;
};

// The ferry's boats and the jet-ski / power-boat circuits. The boat list holds raw pointer values as opaque ints: the
// save writes them, nothing reads them back, and they stay so until the save format changes.
struct Boats {
    int m_boatList[5];
    int m_numBoats;
    CircuitPoint m_circuitPoints[2][40]; // JetSki / PowerBoat; not saved
    uint32_t m_numCircuitPoints[2];
};

struct Power {
    float m_powerUse;
    float m_maxPower;
    float m_currentPower;
    float m_powerDemand;
};

// Live island counts and the guest and building tallies of the statistics screens.
struct Counters {
    int m_numGuests;
    int m_numLitter;
    int m_numBuildings;
    float m_scenicVal;
    float m_waterContam;
    int m_totalGuestArrive[2];
    int m_totalGuestDepart[2];
    int m_todayGuestArrive[2];
    int m_todayGuestDepart[2];
    int m_totalGuestUnhappyDepart[2];
    int m_todayGuestUnhappyDepart[2];
    int m_buildingsConstructed;
    int m_diffBuildingsConstructed;
    int m_barsConstructed;
    int m_buildingsLeftToBuild;
    float m_averageTotty;
    float m_averageBeauty;
    float m_averageSafety;
    int m_partyCount;
    int m_shagCount;
    int m_pissedCount;
    int m_guestsDrowned;
    int m_guestsEaten;
};

// The day's money, the averages and the 128-day histories.
struct Finances {
    float m_incomeToday;
    float m_demolishIncomeToday;
    float m_fineIncomeToday;
    float m_accomIncomeToday;
    float m_attractIncomeToday;
    float m_retailIncomeToday;
    float m_totalFines;
    float m_costsToday;
    float m_constructCostToday;
    float m_sceneryCostToday;
    float m_wagesCostToday;
    float m_stockCostToday;
    float m_averageProfit;
    float m_averageCost;
    float m_averageIncome;
    float m_profitRecord[0x80]; // partly uninitialized in saves (the constructor leaves the records as allocated)
    float m_incomeRecord[0x80];
    float m_costRecord[0x80];
};

// Made with `new World()`: value-initialization zeroes every group (world_construct.h then sets the start values by name). The
// destructor frees the subsystems in a fixed order; the caller clears the game context's world afterwards.
struct World : Subsystems, Clock, Scenario, Texts, Boats, Power, Counters, Finances {
    World() = default;
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Queries and the circuit script command, defined in world_queries.cpp (pure logic in world_queries.h). Each counts its own
    // `world.<query>` counter. The adjacent-name queries read the object manager of the game context, not this world.
    int Hour();
    int ResortRating();
    bool BuildingInScenario(int id);
    int CountBuildings(int id);
    int CountFinishedBuildings(int id);
    bool BuildingAllowed(int id);
    bool BuildingLockedByTree(int id);
    uint32_t AdjacentBuilding(uint32_t id, uint32_t forward);
    uint32_t AdjacentStaff(uint32_t id, int type, uint32_t forward);
    uint32_t CircuitPointCount(int circuit);
    CircuitPoint* CircuitPoints(int circuit);
    void AddCircuitPoint(int circuit, int x, int y);
    // Writes a random walkable point into `out` and returns it (needs no world, only the map grid).
    static float* RandomWalkPoint(float* out);
};

} // namespace obl::world

// Compatibility names for readers whose module has not migrated its World-facing signatures yet.
namespace obl::world_data {
using player::Player;
using world::CircuitPoint;
using world::World;
using world_script::WorldScript;
using world_script_host::WorldScriptHost;
} // namespace obl::world_data
