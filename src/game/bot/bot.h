#pragma once

// Scenario bots (docs/architecture/bots/index.md): a bot reads the world through a read-only snapshot (WorldView) and a few read-only
// queries (Queries), and answers with commands (command.h) that the host submits like the GUI does. Pure code: no game, no Windows
// API, so bots run unchanged in the Windows and the Linux headless host and their decisions are unit-testable on a hand-made view.

#include "game/game/command.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obl::bot {

// The scenario's verdict as its world script states it. Every campaign script ends the scenario with `ShowMenu ScenarioWon <text>`
// (`GameWon` in scenario 12) or `ShowMenu ScenarioLost <text>`; sandbox islands have no goal and stay Running.
enum class Outcome : uint8_t { Running, Won, Lost };
std::string_view OutcomeName(Outcome outcome);
std::optional<Outcome> ParseOutcome(std::string_view name);
// The outcome a `ShowMenu <dialog>` of the world script announces; Running for every other dialog.
Outcome OutcomeOfDialog(std::string_view dialog);

// One building type the scenario offers (its BUILDING_<n> list, in that order) with the build-tree state of this tick.
struct BuildingType {
    std::string key; // BuildingDef::m_key, what `place-building` takes
    int32_t category = 0; // 0 hotel, 1 food, 2 leisure, 3 other
    int32_t cost = 0;
    int32_t width = 0; // footprint in tiles at direction 0
    int32_t height = 0;
    uint32_t buildOn = 0; // LAND 1, BEACH 2, SHALLOW_WATER 4, DEEP_SEA 8
    int32_t power = 0; // negative = generator output
    int32_t maxInstances = 0;
    std::string tree; // the predecessor's key, "none" when the type has none
    bool allowed = false; // Allowed: in the scenario, below MAXINSTANCES, predecessor present
    bool lockedByTree = false; // Locked by a tree: the predecessor has no finished instance
    int32_t finished = 0; // instances without a build or demolish job
    int32_t total = 0; // every instance
    int32_t staffCapacity = 0; // places per home building (m_MaxStaff)
    bool operator==(const BuildingType&) const = default;
};

struct Building {
    uint32_t id = 0;
    std::string key;
    int32_t tileX = 0; // the tile under the building's world position
    int32_t tileY = 0;
    bool underConstruction = false;
    bool beingDemolished = false;
    bool builtByUser = false;
    uint32_t repairState = 0; // 0 perfect .. 3 broken; a guest finds a building unsafe from state 1 on
    int32_t beachArea = -1; // BAYWATCH's swimming area, -1 when outside an area
    int32_t guests = 0; // Building::m_NumGuests: the residents of a hotel or accommodation block, the visitors of others
    bool closed = false; // closed by the player or a breakdown (Building::m_OpenStatus), not by a power cut
    // Per commodity slot: the price guests pay and the reference they judge it by (a price below tolerance x reference is "cheap").
    std::vector<float> prices;
    std::vector<float> referencePrices;
    // The building's mean commodity intensity per gender: a performance boat splits its guests by the sums over all buildings.
    float maleIntensity = 0.0f;
    float femaleIntensity = 0.0f;
    bool operator==(const Building&) const = default;
};

// The staff professions of `hire-staff`, in the order of WorldView::staff.
inline constexpr std::array<std::string_view, 6> kProfessions = {"Cleaner", "Builder", "Mechanic", "Rep", "Lifeguard", "SecurityGuard"};

// What a bot sees at one tick. Plain values copied out of the world, so a bot cannot change the simulation except through commands.
struct WorldView {
    uint32_t tick = 0; // the command loop's tick: commands the bot returns now apply at the start of this tick
    int32_t day = 0; // Zero-based simulation day; the display adds one.
    float hour = 0.0f; // hour of the day, 0 <= h < 24
    float cash = 0.0f;
    float incomeToday = 0.0f; // World::m_incomeToday, what the script's `IncomeToday` compares (resets with the day)
    float maxPower = 0.0f; // World::m_maxPower: what the generators can deliver when they are in repair
    float currentPower = 0.0f; // World::m_currentPower: what they deliver now
    float powerDemand = 0.0f; // World::m_powerDemand: what the finished buildings ask for
    float scenic = 0.0f; // World::m_scenicVal: the summed rating of all scenery, what guests compare with their beauty tolerance
    float safety = 0.0f; // World::m_averageSafety, the script's `GuestSafetyLevel` (0..1, last cached average)
    int32_t guests = 0;
    int32_t rating = 0; // resort rating 0..100
    // The guest objects by their definition's gender: the totty opinion compares the share of the opposite sex with its tolerances.
    int32_t maleGuests = 0;
    int32_t femaleGuests = 0;
    float beauty = 0.0f; // World::m_averageBeauty: the guests' mean beauty opinion 0..1 (the script's GuestBeautyLevel)
    int32_t drunk = 0; // World::m_PissedCount: guests drunk (> 0.5) at the script's last count
    bool paused = false;
    int32_t speedLevel = 0; // 0..2
    int32_t mapWidth = 0;
    int32_t mapHeight = 0;
    int32_t objectiveCount = 0;
    std::array<int32_t, 4> objectives{}; // the script's ScenarioObj<n>: 1 met, 0 not (yet)
    Outcome outcome = Outcome::Running;
    std::vector<BuildingType> buildingTypes;
    std::vector<Building> buildings;
    std::array<int32_t, kProfessions.size()> staff{}; // hired staff per profession
    int32_t beachAreaCount = 0;
    std::array<std::vector<uint32_t>, kProfessions.size()> staffIds; // live member ids per profession, in object-list order
    // Every definition key of the scenario's tables, available or not (the chaos bot also tries the unavailable ones): scenery that
    // `place-scenery` takes, the surfaces of `paint-surface` (scenery of category 1), `set-beer` and `set-show` keys.
    std::vector<std::string> sceneryKeys;
    std::vector<std::string> surfaceKeys;
    std::vector<std::string> beerKeys;
    std::vector<std::string> showKeys;

    const BuildingType* FindType(std::string_view key) const;
};

// Read-only questions about the live world that a snapshot cannot answer cheaply. The host answers them with the same checks the
// executor makes, so a positive answer predicts that the command is accepted (cash and tree aside).
class Queries {
public:
    virtual ~Queries() = default;
    // The place mode's footprint check for building `key` with its origin at tile x/y and direction dir.
    virtual bool BuildingFits(std::string_view key, int32_t x, int32_t y, int32_t direction) const = 0;
    // The scenery place mode's footprint check for scenery `key` with its origin at tile x/y and direction dir (0..7).
    virtual bool SceneryFits(std::string_view, int32_t, int32_t, int32_t) const { return false; }
    // Swimming area controlled by a tower whose world position is at this tile; -1 outside the areas.
    virtual int32_t BeachAreaAt(int32_t, int32_t) const { return -1; }
};

class Bot {
public:
    virtual ~Bot() = default;
    // Called every ThinkInterval() ticks from tick 0 on. The commands appended to `out` apply at the start of view.tick, in order.
    virtual void Think(const WorldView& view, const Queries& queries, std::vector<command::Command>& out) = 0;
    virtual uint32_t ThinkInterval() const { return 25; }
};

// The per-scenario tables of a bot (building lists, hire targets, shark schedules), read from `tests/bots/<run>.plan` next to the run
// file so the bot code holds only the loop. A line is `<name>: <word> <word> ...`; `#` starts a comment. A word of a table row
// joins its fields with `:` (`SOFTDRNK:2`, `1:11.0:1.0`); a bot asks for a name the plan lacks and gets an empty list.
class BotPlan {
public:
    // One want of a building list: `KEY:count` or `KEY:count:save`; the key `-` is a blank entry that does nothing.
    struct Want {
        std::string key;
        int32_t count = 0;
        bool save = false; // when unaffordable, stop buying lower entries until the cash is there
    };

    static std::optional<BotPlan> Parse(std::string_view text, std::string* error = nullptr);
    const std::vector<std::string>& Words(std::string_view name) const;
    std::vector<int32_t> Ints(std::string_view name) const;
    std::vector<Want> Wants(std::string_view name) const;
    // Each word split at `:` into its fields.
    std::vector<std::vector<std::string>> Rows(std::string_view name) const;

private:
    std::vector<std::pair<std::string, std::vector<std::string>>> m_lists;
};

struct BotInfo {
    std::string_view id;
    std::string_view scenario; // the start it is written for, as a replay names it (`s1`)
    std::string_view summary;
    std::unique_ptr<Bot> (*make)(const BotPlan& plan);
};
std::span<const BotInfo> Bots();
const BotInfo* FindBot(std::string_view id);

// A bot run file (`.bot`, docs/architecture/bots/index.md): which bot plays which start, with the determinism settings of a replay, how long
// it may play and what it must reach.
struct BotRun {
    std::string bot;
    std::string start; // `start scenario <name>`, the names of command::ParseStartScenario
    uint32_t seed = 1;
    float fixedDt = 0.02f;
    uint32_t maxTicks = 0;
    Outcome expect = Outcome::Won;
    bool operator==(const BotRun&) const = default;
};
// Parses and validates a bot run file. On failure `error` says "line N: why".
std::optional<BotRun> ReadBotRun(std::string_view text, std::string* error = nullptr);

} // namespace obl::bot
