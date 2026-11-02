#pragma once

#include "game/game/command.h"

#include <cstdint>
#include <vector>

namespace obl::game {
struct Game; // game/game/game.h
}

namespace obl::game {

// A record or replay session watching the tick loop (testing/replay.cpp; obl-sim later). All calls come from RunCommandTick on the game thread.
class TickListener {
public:
    virtual ~TickListener() = default;
    // A world became playable; its first tick is 0.
    virtual void OnWorldStart() = 0;
    // The world of the running ticks was left (state switch, load, another world) before tick `tick`.
    virtual void OnWorldLeft(uint32_t tick) = 0;
    // The start of `tick`, before its commands: the world is at the frame boundary (checkpoints are taken here). `commands` holds what was
    // submitted since the last tick; a replay replaces it with the recorded commands of this tick.
    virtual void BeginTick(uint32_t tick, std::vector<command::Command>& commands) = 0;
    // A command of `tick` after the executor ran it (rejected commands too: a replay rejects them the same way).
    virtual void OnCommand(uint32_t tick, const command::Command& command, bool applied) = 0;
};
// One listener at a time; nullptr removes it.
void SetTickListener(TickListener* listener);

// The game loop's command queue (docs/architecture/commands.md, "Data flow"). Submit at any time on the game thread; the commands run at
// the start of the next gameplay the game update, before the state's update, in submission order.
void SubmitCommand(command::Command command);

// Called by the game update after a pending state switch and before the state's update. Outside settled gameplay (another state, no world, a
// switch this frame or a pending load) the queue is dropped and the tick counter waits for the next world; in gameplay the pending commands
// run with the current tick, then the tick advances. Paused frames count.
void RunCommandTick(game::Game* game, bool stateSwitched);

struct CommandLoopStats {
    bool inWorld = false; // the last the game update ran in settled gameplay
    uint32_t tick = 0; // The game update calls since the world became playable (the tick the next commands get)
    uint32_t applied = 0; // totals since the process started
    uint32_t rejected = 0;
    uint32_t dropped = 0; // submitted but cleared by a world transition
    uint32_t pending = 0;
    bool lastApplied = false;
};
CommandLoopStats GetCommandLoopStats();
// The totals back to zero (the next script of a headless script list counts like a fresh process); the world state is kept.
void ResetCommandLoopTotals();

} // namespace obl::game
