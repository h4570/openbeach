#pragma once

// The chaos bot (docs/testing/chaos-bot.md): a bot of the scenario-bot API (bot.h) that plays badly on purpose. It draws a weighted
// mix of valid and deliberately bad commands from its own seeded generator; the host checks invariants instead of a verdict. Pure code
// (no game, no Windows API), so the command stream of a seed is the same on every platform and unit-testable on a hand-made view.

#include "game/bot/bot.h"
#include "game/game/command.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace obl::chaos {

// The bot's own generator (splitmix64): the same numbers with every compiler and standard library, unlike <random>'s distributions,
// and separate from the game's rand(), so the bot's choices never shift the simulation's draws.
class Rng {
public:
    explicit Rng(uint64_t seed) : m_state(seed) {}
    uint64_t Next();
    uint32_t Below(uint32_t count); // 0 .. count - 1; 0 when count is 0
    int32_t Between(int32_t low, int32_t high); // low .. high inclusive
    bool Chance(uint32_t percent);

private:
    uint64_t m_state;
};

// The kinds of command the bot draws, each with a weight (kWeights in chaos.cpp).
enum class Kind : uint8_t {
    PlaceBuilding,
    PlaceScenery,
    PaintSurface,
    Dozer,
    Demolish,
    HireStaff,
    DismissStaff,
    StaffWage,
    StaffShift,
    SendStaff,
    BuildingOpen,
    CallService,
    Price,
    BeerOrShow,
    ObjectControl,
    Pause,
    Speed,
    BreakObject,
    Count,
};
std::string_view KindName(Kind kind);

class ChaosBot final : public bot::Bot {
public:
    static constexpr uint32_t kThinkInterval = 10;

    explicit ChaosBot(uint32_t seed) : m_rng(0x6368616f73000000ull ^ seed) {}
    void Think(const bot::WorldView& view, const bot::Queries& queries, std::vector<command::Command>& out) override;
    uint32_t ThinkInterval() const override { return kThinkInterval; }

    // One command of `kind` against `view` (the unit tests draw each kind directly).
    command::Command Draw(Kind kind, const bot::WorldView& view, const bot::Queries& queries);

private:
    Kind DrawKind();
    // Targets: live objects most of the time, ids seen earlier (possibly destroyed since) and junk ids sometimes.
    uint32_t AnyObject(const bot::WorldView& view);
    uint32_t AnyBuilding(const bot::WorldView& view);
    uint32_t AnyStaff(const bot::WorldView& view);
    void Tile(const bot::WorldView& view, int32_t& x, int32_t& y);
    std::string Key(const std::vector<std::string>& keys);
    int32_t Direction();
    float Price();

    Rng m_rng;
    std::set<uint32_t> m_seen; // every building and staff id any view showed
    Kind m_burstKind = Kind::Count;
    uint32_t m_burstLeft = 0;
};

// A scripted `mode tricky <family>` probe (docs/testing/chaos-bot.md "Scripted tricky mode"): a fixed command sequence instead of
// random ones, with report-only observations. No random fallback: a probe that cannot set up its situation reports a skip rather
// than silently testing a different action.
class TrickyProbe : public bot::Bot {
public:
    uint32_t ThinkInterval() const override { return 10; }
    std::vector<std::string> observations;
};
// The probe families by their run-file name (scenery-sale, accommodation, construction); null for an unknown one.
std::unique_ptr<TrickyProbe> MakeTrickyProbe(std::string_view family);
bool IsTrickyFamily(std::string_view family);

// Scripted scenery-sale probes (family scenery-sale).
class TrickyBot final : public TrickyProbe {
public:
    void Think(const bot::WorldView& view, const bot::Queries& queries, std::vector<command::Command>& out) override
    {
        if (m_phase == 0) {
            if (std::find(view.sceneryKeys.begin(), view.sceneryKeys.end(), "fountain04") != view.sceneryKeys.end()) {
                for (int y = 0; y < view.mapHeight; ++y)
                    for (int x = 0; x < view.mapWidth; ++x)
                        if (queries.SceneryFits("fountain04", x, y, 0)) {
                            m_x = x;
                            m_y = y;
                            m_cash = view.cash;
                            m_income = view.incomeToday;
                            out.push_back(command::PlaceScenery{"fountain04", x, y, 0});
                            m_phase = 1;
                            return;
                        }
            }
            observations.push_back("tricky-scenery-sale skipped=no-fitting-fountain");
            m_phase = 6;
        } else if (m_phase == 1) {
            // Repeated requests before the scenery state machine can remove the object.
            for (int i = 0; i < 3; ++i) out.push_back(command::Dozer{m_x, m_y});
            ++m_phase;
        } else if (m_phase == 2) {
            Observe(view, "burst");
            // Repeat after the state machine has advanced, including an already removed tile.
            out.push_back(command::Dozer{m_x, m_y});
            ++m_phase;
        } else if (m_phase == 3) {
            Observe(view, "delayed");
            out.push_back(command::Dozer{m_x, m_y});
            ++m_phase;
        } else if (m_phase == 4) {
            Observe(view, "empty-tile");
            ++m_phase;
        }
    }

private:
    void Observe(const bot::WorldView& view, const char* stage)
    {
        observations.push_back(std::string("tricky-scenery-sale stage=") + stage + " cash-delta=" + std::to_string(view.cash - m_cash) +
                " income-delta=" + std::to_string(view.incomeToday - m_income));
        m_cash = view.cash;
        m_income = view.incomeToday;
    }
    int m_phase = 0, m_x = 0, m_y = 0;
    float m_cash = 0, m_income = 0;
};

// Accommodation closure and demolition while guests are away versus asleep (families accommodation-asleep
// and accommodation-away). The fullest accommodation block is demolished (two requests in one tick) at night or by day, the next
// fullest the other way round; s12 has one inhabited block, so the two families give its residents each case. The next night the
// hotel is closed while its guests sleep and reopened in the morning, then closed by day and reopened. Observations report the guests
// and every hotel's and block's residents before and an hour after each step.
class TrickyAccommodation final : public TrickyProbe {
public:
    explicit TrickyAccommodation(bool awayFirst) : m_awayFirst(awayFirst) {}
    void Think(const bot::WorldView& view, const bot::Queries& queries, std::vector<command::Command>& out) override;

private:
    void Observe(const bot::WorldView& view, const char* stage);
    bool At(const bot::WorldView& view, int32_t day, float hour) const { return view.day > day || (view.day == day && view.hour >= hour); }
    bool m_awayFirst;
    int m_phase = 0;
    int32_t m_day = 0; // the day of the start
    std::optional<uint32_t> m_hotel; // object ids start at 0 (the s12 hotel), so a missing one is nullopt
    bool m_blocks = false;
};

// Construction traps (family construction): one placement per direction 0-3, each first at direction 4-7, a duplicate placement on the same
// footprint in the same tick, a placement of a tree-locked type while its predecessor is only under construction, a demolition during
// construction and a placement on the freed footprint. Observations report which placements produced a building, and which of
// them finished or are still under construction at the deadline.
class TrickyConstruction final : public TrickyProbe {
public:
    static constexpr uint32_t kDeadline = 3000; // ticks after the placements by which every building should have finished

    void Think(const bot::WorldView& view, const bot::Queries& queries, std::vector<command::Command>& out) override;

private:
    struct Placed {
        command::PlaceBuilding place;
        uint32_t tick = 0; // the think that submitted it
        uint32_t id = 0;
        bool finished = false;
    };
    bool Place(const bot::WorldView& view, const bot::Queries& queries, const bot::BuildingType& type, int32_t direction,
            std::vector<command::Command>& out, int32_t trap = -1);
    void Match(const bot::WorldView& view); // assigns new building ids to the placements by key
    int m_phase = 0;
    uint32_t m_placedAt = 0;
    std::set<uint32_t> m_known;
    std::vector<Placed> m_placed;
    std::string m_locked; // a type locked by its predecessor in m_placed, when the scenario has one
    command::PlaceBuilding m_again; // the footprint freed by the demolition during construction
};

// A chaos run file (`.chaos`, docs/testing/chaos-bot.md "Running it"): the start and determinism settings of a replay, how long to
// play, where the harness saves and reloads, and optionally a fixed command list instead of the generator (a shrunk finding).
struct ChaosRun {
    std::string start; // `start scenario <name>`, the names of command::ParseStartScenario
    uint32_t seed = 1; // the game's rand() and the bot's generator
    float fixedDt = 0.02f;
    uint32_t maxTicks = 0;
    std::optional<uint32_t> saveAt; // the tick of the save round trip; none = no round trip
    uint32_t checkpointEvery = 500; // world hashes in the recorded replay
    uint32_t checkRejected = 2; // rejections per command name whose world hash is checked (a hash costs ~50 ms in Debug)
    std::string tricky; // the scripted probe family (TrickyProbe) instead of random commands; empty = random
    std::string commands; // a replay file whose commands are played instead of the generator's (relative to the run file)
    bool operator==(const ChaosRun&) const = default;
};
// Parses and validates a chaos run file. On failure `error` says "line N: why".
std::optional<ChaosRun> ReadChaosRun(std::string_view text, std::string* error = nullptr);

// The game's rand() state after `calls` draws from srand(seed): the native GameRng LCG, jumped ahead
// in O(log calls). The save round trip seeds the reloaded world with it, so both continue from the same draws.
uint32_t RandStateAfter(uint32_t seed, uint64_t calls);

} // namespace obl::chaos
