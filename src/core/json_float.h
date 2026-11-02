#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

// The float rule of our JSON formats (docs/architecture/data-formats/principles.md, "Conventions"), shared by the converter
// and the game's save writers so both write the same tokens.
namespace obl::json_float {

// A float as the JSON number of the shortest decimal that reads back to the same float.
nlohmann::ordered_json Number(float value);

// A float given by its bits: a number, or "f32:xxxxxxxx" for NaN and infinities (saves.md, "Exact numbers").
nlohmann::ordered_json Bits(uint32_t bits);

// The bits of a float written by the rule: a number (read as a double, then narrowed) or "f32:xxxxxxxx"; none for anything else.
std::optional<uint32_t> ParseBits(const nlohmann::json& value);

} // namespace obl::json_float
