#pragma once

#include "game/weather/weather_state.h"
#include "formats/schemas.h"
#include "core/json_schema.h"
#include <nlohmann/json.hpp>
#include <string>

namespace obl::weather {
struct Parameters {
    LightKey lightKeys[24]{};
    uint32_t stateColors[14]{};
};

inline bool ReadParameters(const nlohmann::ordered_json& document, Parameters& out, std::string& error)
{
    if (document.is_discarded()) {
        error = "invalid weather parameters JSON";
        return false;
    }
    json_schema::Registry schemas;
    if (!formats::LoadEmbeddedSchemas(schemas, error) || !formats::ValidateDocument(schemas, "obl.weather_parameters", document, error))
        return false;
    Parameters parsed;
    for (size_t i = 0; i < 24; ++i) {
        const auto& row = document["light_keys"][i];
        if (row[0].get<float>() != static_cast<float>(i)) {
            error = "weather light keys must contain each hour in order";
            return false;
        }
        parsed.lightKeys[i] = {row[0].get<float>(), row[1].get<uint32_t>()};
    }
    for (size_t i = 0; i < 14; ++i) parsed.stateColors[i] = document["state_colors"][i].get<uint32_t>();
    out = parsed;
    return true;
}
} // namespace obl::weather
