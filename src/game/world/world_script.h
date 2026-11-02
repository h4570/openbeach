#pragma once

#include "game/world/world_state.h"
#include "game/messages/message_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"

#include <cstdint>

namespace obl::world {

// world::GetScriptValue, world::SetScriptValue, EvalCondition and WorldScriptExecute: the scenario script's
// access to the World and its command interpreter, plus the state machine of game/world_script_machine.cpp.
bool RegisterWorldScriptDrivers();

// The C++ query itself, for the interpreter and the tests. `data` is the second half of the parsed token (an index for
// Flag<n>/Var<n>/ScenarioObj<n>, a float for numbers, the text for strings and building names); keys it does not know come
// back as {key, data}. Calls the game's own object counting and build-job helpers, so it needs a running world.
script::ScriptValue GetScriptValue(layout::World& world, int key, const int32_t* data);

// EvalCondition: the `If ( a <op> b)` test of the scenario script. `op` is the parser's number (0 `&&`, 1 `||`,
// 2 `>`, 3 `<`, 4 `==`, 5 `<=`, 6 `>=`, 7 `!=`; anything else is false), a right key of 0x110 means a bare `If ( a )` and 0x36 a
// quoted text (string `==`/`!=` only). Comparisons follow the flag tests, including their NaN behavior. Pure query:
// besides the values GetScriptValue caches it changes nothing.
bool EvalCondition(layout::World& world, int32_t leftKey, int32_t* leftData, int32_t rightKey, int32_t* rightData, int32_t op);

// world::SetScriptValue: the write side. `index` is the parsed token's data (an index for Flag<n>/Var<n>/ScenarioObj<n>, a
// name for BeerActive/DefaultBeer/EventActive), `type`/`valueBits` the evaluated right-hand side (same kinds as ScriptValue).
// Keys it does not know are ignored.
void SetScriptValue(layout::World& world, int key, const void* index, int type, int32_t valueBits);

// The scenario script's `ChangeState <state>;` without a delay, for the tests: posts the same MT_STATECHANGE. Returns the state's index (-1
// when the script has no such state, -2 without a script); nothing is posted while the world's silence byte silences ChangeState (-3).
int32_t ChangeWorldScriptState(layout::World& world, const char* state);

// The interpreter and the state machine (game/world_script_machine.cpp); the WorldScript entry points forward to them
// and each counts its `world.script-<method>` counter. ExecuteCommand runs one command node on the current World.
void WorldScriptExecute(script::ScriptCommand& command);
void WorldScriptRunOnUpdate(world_script::WorldScript& script);
void WorldScriptRunOnExit(world_script::WorldScript& script);
void WorldScriptEnterState(world_script::WorldScript& script, uint32_t state);
void WorldScriptSetState(world_script::WorldScript& script, uint32_t state);
bool WorldScriptDispatch(world_script::WorldScript& script, messages::Message message);
int32_t WorldScriptFindState(const world_script::WorldScript& script, const char* name);
void WorldScriptStartNewGame(world_script::WorldScript& script, float span);
void WorldScriptStartLoadedGame(world_script::WorldScript& script);

} // namespace obl::world
