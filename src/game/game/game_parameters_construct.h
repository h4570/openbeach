#pragma once

#include "game/game/game_parameters.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

// The construction of a parameter list (game_parameters.cpp): the per-entry loop that turns the
// entries of `data/script/gameparameters.pis` into the list of GameParameterNode records. Pure logic on game::GameParameters:
// the game's fatal error and operator new are injected, and game_parameters.cpp keeps the file and the list in game::Context.
namespace obl::game_parameters_construct {

struct Env {
    // The fatal error with its format, key and path; it ends the process, so Construct stops at the first call.
    void (*fatal)(const char* format, const char* key, const char* path);
    // The game's operator new (the node and the 0x100-byte copy of a string value).
    void* (*allocate)(size_t bytes);
};

// Appends one node at the tail. `string` null stores no copy (the node's pointer is null), otherwise a 0x100-byte copy is made.
void Add(
        game::GameParameters& list, const char* name, uint8_t boolean, int integer, uint32_t floatBits, const char* string, const Env& env);

// The converted `rules/game_parameters.json` (obl.game_parameters, game-data.md) as the same list: one node per parameter in
// document order, named with the engine's key (the type letter from the JSON type, b/i/f/s, then the name upper-cased, so
// `play_movie: true` is `bPLAY_MOVIE`). Returns false with `error` and leaves no node on a document the converter cannot write.
bool FromJson(game::GameParameters& list, const nlohmann::ordered_json& document, const Env& env, std::string& error);

} // namespace obl::game_parameters_construct
