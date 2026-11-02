#include "game/game/startup_tables.h"
#include "flow.h"
#include "game/game/game_api.h"

#include "game/world/world_control.h"
#include "assertions.h"
#include "building_events.h"
#include "platform/test_tools/crash.h"
#include "core/log.h"
#include "test_util.h"
#include "world_state.h"
#include "game/world/world_script.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace obl::test {
namespace {

constexpr uint32_t kDefaultTimeoutMs = 60000;

// game_api::Update frames in a row with the gameplay state active, a world and no save waiting to be loaded.
int g_gameplayFrames;
int g_lastScenarioBoot;

bool g_awaiting;
uint32_t g_awaitStart;
unsigned long long g_awaitFrame0;
double g_awaitTarget;

std::string Join(const std::vector<std::string>& args, size_t count)
{
    std::string text;
    for (size_t i = 0; i < count && i < args.size(); ++i) text += (i ? " " : "") + args[i];
    return text;
}

double ArgNumber(const std::vector<std::string>& args, size_t i, double fallback)
{
    return i < args.size() ? std::atof(args[i].c_str()) : fallback;
}

// Hours since day 0, 00:00, so conditions survive midnight.
bool AbsoluteHours(double& out)
{
    layout::World* world = World();
    if (!world) return false;
    out = world->m_gameDay * 24.0 + world->m_gameDayTime;
    return true;
}

struct Condition {
    bool known = true;
    size_t words = 1; // arguments used by the condition; the optional timeout follows
    bool met = false;
};

Condition Evaluate(const std::vector<std::string>& args, bool first)
{
    Condition c;
    const std::string& name = args[0];
    if (name == "gameplay") {
        c.met = g_gameplayFrames >= 1;
    } else if (name == "world-loaded") {
        // A few settled frames: a loaded save finishes inside game_api::Update (ui::save_panel::LoadPendingSave ->
        // WorldTestState::FinishLoad), but the first updates still rebuild the GUI and the path grid.
        c.met = g_gameplayFrames >= 3;
    } else if (name == "frames") {
        c.words = 2;
        c.met = Frames() - g_awaitFrame0 >= static_cast<unsigned long long>(ArgNumber(args, 1, 1));
    } else if (name == "hour" || name == "hours") {
        c.words = 2;
        double now;
        if (AbsoluteHours(now)) {
            if (first) {
                double value = ArgNumber(args, 1, 0);
                if (name == "hours") {
                    g_awaitTarget = now + value;
                } else {
                    // Next time the clock reads the hour (today's if it has not passed yet).
                    double dayStart = now - std::fmod(now, 24.0);
                    g_awaitTarget = dayStart + value;
                    if (g_awaitTarget < now) g_awaitTarget += 24;
                }
            }
            c.met = now >= g_awaitTarget;
        }
    } else if (args.size() >= 3 && (args[1] == ">=" || args[1] == "<=" || args[1] == "==" || args[1] == ">" || args[1] == "<")) {
        // await <probe|report key> <op> <value>
        c.words = 3;
        double actual;
        if (ReadValue(name, actual)) {
            double value = ArgNumber(args, 2, 0);
            const std::string& op = args[1];
            c.met = op == ">="   ? actual >= value
                    : op == "<=" ? actual <= value
                    : op == "==" ? actual == value
                    : op == ">"  ? actual > value
                                 : actual < value;
        }
    } else {
        c.known = false;
    }
    return c;
}

} // namespace

void OnFrame()
{
    CountFrame();
    WatchdogBeat();
    bool gameplay = GameStateIndex() == 0 && World() && Game()->m_loadSlot == -1;
    g_gameplayFrames = gameplay ? g_gameplayFrames + 1 : 0;
}

void ResetFlow()
{
    g_gameplayFrames = 0;
    g_lastScenarioBoot = 0;
    g_awaiting = false;
}

void NotifyWorldReloading()
{
    g_gameplayFrames = 0;
    ResetWorldStateCommands();
}

AwaitResult StepAwait(const std::vector<std::string>& args, uint32_t now)
{
    if (args.empty()) {
        OBL_LOG("autotest: FAILED await (missing condition)");
        return AwaitResult::Failed;
    }
    bool first = !g_awaiting;
    if (first) {
        g_awaiting = true;
        g_awaitStart = now;
        g_awaitFrame0 = Frames();
    }
    Condition c = Evaluate(args, first);
    std::string label = Join(args, c.words);
    if (!c.known) {
        g_awaiting = false;
        OBL_LOG("autotest: FAILED await %s (unrecognized condition)", label.c_str());
        return AwaitResult::Failed;
    }
    if (c.met) {
        g_awaiting = false;
        OBL_LOG("autotest: await %s met after %lu ms, %llu frames", label.c_str(), now - g_awaitStart, Frames() - g_awaitFrame0);
        return AwaitResult::Done;
    }
    uint32_t timeout = static_cast<uint32_t>(ArgNumber(args, c.words, kDefaultTimeoutMs));
    if (now - g_awaitStart >= timeout) {
        g_awaiting = false;
        OBL_LOG("autotest: FAILED await %s (timeout after %lu ms, %llu frames)", label.c_str(), now - g_awaitStart,
                Frames() - g_awaitFrame0);
        return AwaitResult::Failed;
    }
    return AwaitResult::Waiting;
}

void BootNewGame(game::Game& game, bool sandbox, int island)
{
    NotifyWorldReloading();
    // The sandbox button / the island screen: sandbox flag (also reloads the scenario progress), selected
    // scenario in Game::m_lastUnlockedScenario (0-based), then the start button's game_api::SetNextState(5 = LoadGame), which
    // enters WorldTest after 0.1 s. game_api::SetSandboxMode and game_api::SetNextState take their arguments the same way
    // (both checked by the state tests); the state switch itself happens at the top of game_api::Update.
    game::game_api::SetSandboxMode(sandbox);
    game.m_lastUnlockedScenario = island - 1;
    game::game_api::SetNextState(static_cast<int>(game::GameStateId::LoadGame));
}

int LastScenarioBoot()
{
    return g_lastScenarioBoot;
}

// boot-gameplay                      sandbox, island 1 (what the menu's "piaskownica" + "graj" does)
// boot-gameplay <slot>|last          load SaveGames/save<slot>.dat
// boot-gameplay sandbox|scenario [n] new game on island / scenario n (1-based)
// next-state <0..6>                  game_api::SetNextState (0 WorldTest, 1 MainMenu, 2 Intro, 3 ExitGame, 4 ResetGame, 5 LoadGame, 6 GameWon)
// speed <factor>                     game speed multiplier (the game uses 1, 2 or 8)
// speed-level <0..2>                 the game's own speed switch (world::SetGameSpeedLevel: table -> the world's speed field)
// speed-table                        log g_GameSpeedTable
// world-script-change <state>        the scenario script's `ChangeState <state>;` (MT_STATECHANGE, delivered by the next update); logs
//                                    `state=` (the index, negative when nothing was posted). STATE_WORLD_AFTERGAME without a pause is
//                                    where a won scenario continues after ScenarioWon's "Continue", with no day limit left.
bool ExecuteFlowCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "boot-gameplay") {
        game::Game* game = Game();
        if (!game) {
            OBL_LOG("autotest: FAILED boot-gameplay (no game object)");
            return true;
        }
        g_lastScenarioBoot = 0;
        if (!args.empty() && args[0] != "sandbox" && args[0] != "scenario") {
            LoadSaveSlot(args[0]);
            return true;
        }
        bool sandbox = args.empty() || args[0] == "sandbox";
        int island = static_cast<int>(ArgNumber(args, 1, 1));
        if (!sandbox) g_lastScenarioBoot = island;
        BootNewGame(*game, sandbox, island);
        OBL_LOG("autotest: boot-gameplay %s island=%d", sandbox ? "sandbox" : "scenario", island);
    } else if (name == "next-state") {
        game::Game* game = Game();
        const int id = static_cast<int>(ArgNumber(args, 0, -1));
        if (!game || args.size() != 1 || id < 0 || id >= static_cast<int>(game::GameStateId::Count)) {
            OBL_LOG("autotest: FAILED next-state (needs a game and one state index 0..6)");
            return true;
        }
        game::game_api::SetNextState(id);
        OBL_LOG("autotest: next-state %d", id);
    } else if (name == "world-script-change") {
        layout::World* world = World();
        if (!world || args.size() != 1) {
            OBL_LOG("autotest: FAILED world-script-change (needs a world and one state name)");
            return true;
        }
        const int32_t state = world::ChangeWorldScriptState(*world, args[0].c_str());
        OBL_LOG("autotest: %s world-script-change %s state=%d", state >= 0 ? "PASS" : "FAILED", args[0].c_str(), state);
    } else if (name == "speed" || name == "speed-level" || name == "speed-table") {
        layout::World* world = World();
        if (name == "speed-table") {
            // The source of these values (the game speed defaults) is not in the standalone host's image: its read-only data is zero there.
            auto table = game::GameSpeedTable();
            OBL_LOG("autotest: speed-table speedTable0=%g speedTable1=%g speedTable2=%g", table[0], table[1], table[2]);
        } else if (!world || args.size() != 1) {
            OBL_LOG("autotest: FAILED %s (needs a world and one argument)", name.c_str());
        } else if (name == "speed") {
            world::SetGameSpeedFactor(*world, static_cast<float>(std::atof(args[0].c_str())));
            OBL_LOG("autotest: speed speed=%g", world->m_gameSpeed);
        } else {
            int level = std::atoi(args[0].c_str());
            if (level < 0 || level > 2) { // the table has three entries
                OBL_LOG("autotest: FAILED speed-level %d (valid: 0..2)", level);
                return true;
            }
            world::SetGameSpeedLevel(*world, level);
            OBL_LOG("autotest: speed-level level=%d speed=%g", world->m_gameSpeedLevel, world->m_gameSpeed);
        }
    } else {
        return false;
    }
    return true;
}

} // namespace obl::test
