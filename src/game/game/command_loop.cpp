#include "game/game/command_loop.h"
#include "game/ui/save_slots.h"

#include "game/game/game_accessors.h"
#include "game/game/command_executor.h"
#include "game/game/game.h"
#include "core/log.h"

#include <string>
#include <utility>

namespace obl::game {
namespace {
command::CommandQueue g_queue;
CommandLoopStats g_stats;
const world_data::World* g_world = nullptr; // the world the tick counter belongs to
TickListener* g_listener = nullptr;

void Drop()
{
    const auto dropped = static_cast<uint32_t>(g_queue.TakePending().size());
    if (dropped) OBL_LOG("command: dropped %u pending command(s) at a world transition", dropped);
    g_stats.dropped += dropped;
    g_stats.pending = 0;
}

void LeaveWorld()
{
    if (g_stats.inWorld && g_listener) g_listener->OnWorldLeft(g_stats.tick);
}
} // namespace

void SubmitCommand(command::Command command)
{
    g_queue.Submit(std::move(command));
    ++g_stats.pending;
}

void ResetCommandLoopTotals()
{
    g_stats.applied = 0;
    g_stats.rejected = 0;
    g_stats.dropped = 0;
    g_stats.lastApplied = false;
}

void SetTickListener(TickListener* listener)
{
    g_listener = listener;
}

void RunCommandTick(game::Game* game, bool stateSwitched)
{
    const world_data::World* world = GetWorld();
    const bool gameplay = game->m_currentState == game->m_states[static_cast<int>(game::GameStateId::WorldTest)].get();
    if (!gameplay || !world || stateSwitched || ui::LoadPendingFor(*game)) {
        Drop();
        LeaveWorld();
        g_stats.inWorld = false;
        g_world = nullptr;
        return;
    }
    if (!g_stats.inWorld || world != g_world) {
        // A new world (start or load): the tick counts from it. Frames outside gameplay already dropped what was meant for the old world;
        // what came since was submitted against this one. A world replaced without leaving gameplay drops the old world's commands here.
        if (g_stats.inWorld) Drop();
        LeaveWorld();
        g_stats.inWorld = true;
        g_stats.tick = 0;
        g_world = world;
        if (g_listener) g_listener->OnWorldStart();
    }
    std::vector<command::Command> commands = g_queue.TakePending();
    if (g_listener) g_listener->BeginTick(g_stats.tick, commands);
    for (const command::Command& command : commands) {
        const CommandResult result = ApplyCommand(command);
        const std::string text = command::Format(command);
        if (result.applied) {
            ++g_stats.applied;
            OBL_LOG("command: @%u applied %s", g_stats.tick, text.c_str());
        } else {
            ++g_stats.rejected;
            OBL_LOG("command: @%u rejected %s (%s)", g_stats.tick, text.c_str(), result.reason);
        }
        g_stats.lastApplied = result.applied;
        if (g_listener) g_listener->OnCommand(g_stats.tick, command, result.applied);
    }
    g_stats.pending = 0;
    ++g_stats.tick;
}

CommandLoopStats GetCommandLoopStats()
{
    return g_stats;
}

} // namespace obl::game
