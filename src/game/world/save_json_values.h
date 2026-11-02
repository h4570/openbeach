#pragma once

#include "core/json_float.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

// The value rules of the obl.save readers (save-switch.md), the inverse of the writers' and the bridge's: an integer
// record from a JSON integer narrowed to 32 bits (a bool as 0/1, as the binary records hold bools), a float record from the float
// rule (core/json_float.h).
namespace obl::world_save {

inline std::optional<uint32_t> IntegerBits(const nlohmann::json& value)
{
    if (value.is_boolean()) return value.get<bool>() ? 1u : 0u;
    if (!value.is_number_integer()) return std::nullopt;
    return static_cast<uint32_t>(value.get<int64_t>());
}

inline std::optional<uint32_t> FloatBits(const nlohmann::json& value)
{
    return json_float::ParseBits(value);
}

} // namespace obl::world_save
