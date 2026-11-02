#pragma once

#include "core/json_schema.h"

#include <nlohmann/json.hpp>

#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace obl::convert {

// Reads `.pis` text with the engine's reader (game/tables/pis.h) and applies the rules of the game parameter table's
// construction and lookup: the first letter of a key is its type (`b` bool, `i` integer, `f` float, `s` string) and must be
// lowercase; a `b` entry is true only for the string "true"; `i` truncates the float like FloatToInt; `f` keeps the reader's
// float. A repeated exact key keeps its first value. The JSON name is the key without its letter, lowercased. Refused: an
// unrecognized or upper-case type letter, a value of the wrong kind, a string without its closing quote, a name that is not a
// content key, and two different keys that name the same parameter.
bool ParseGameParameters(std::string_view text, nlohmann::ordered_json& parameters, std::string& error);

// Converts `Data/Script/GameParameters.pis` of the installation into `<out>/rules/game_parameters.json`, validated.
bool ConvertGameParameters(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, std::string& error);

} // namespace obl::convert
