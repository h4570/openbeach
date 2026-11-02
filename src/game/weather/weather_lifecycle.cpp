#include "core/pack_path.h"
#include "game/game/diagnostics.h"
#include <fstream>
#include "game/weather/weather_state.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/map_view_access.h"
#include "game/weather/weather.h"
#include "game/ui/map_view.h"
#include "game/world/world_state.h"
#include "core/call_counter.h"
#include "game/weather/weather_lifecycle.h"
#include "game/weather/weather_save.h"
#include "game/weather/weather_update.h"
#include "game/world/save_table_reader.h"

#include <cstddef>
#include <cstdint>

// Native lifetime and the obl.save load for src/game/weather/.
namespace obl::weather {
namespace {

using ui::MapView;
using weather::RainDropBuffer;
using weather::Weather;
using weather::WeatherEntry;

test::CallCounter g_answeredConstruct("weather.construct");
test::CallCounter g_answeredDestruct("weather.destruct");
// Compatibility reports for retired, unreachable lifecycle entries.
test::CallCounter g_clearGlobal("weather.clear-global");
test::CallCounter g_dequeDestruct("weather.schedule-destruct");
test::CallCounter g_lightKeyConstruct("weather.light-key-construct");

RainDropBuffer& Drops()
{
    return runtime::RainDrops();
}

class LifecycleEnv {
public:
    void SetGlobal(Weather* weather) { game::SetWeather(weather); }
    void RandomizeRainDrops()
    {
        const MapView* view = ui::GetMapView();
        if (!view) return;
        weather::RandomizeRainDrops(Drops(), view->m_viewWidth, view->m_viewHeight, [] { return game::Rand(); });
    }
    void UpdateLighting(Weather& weather) { runtime::UpdateLighting(&weather); }

    void Delete(void* block) { ::operator delete(block); }
    void ResetShared() { game::SetWeather(nullptr); }
};

// ---- the obl.save section (src/game/weather/weather_save.h) ----------------------------------------------------------------------

// The obl.save reader with the bitmap step: a saved bolt makes its lightning bitmap again.
class JsonReader : public world_save::TableReader<JsonReader> {
public:
    using TableReader::TableReader;
    void CreateLightningBolt(Weather& weather) { runtime::CreateLightningBolt(&weather); }
};

} // namespace

namespace runtime {
bool LoadJson(Weather* weatherState, const nlohmann::json& in, std::string& error)
{
    JsonReader reader("Weather", nullptr, in, error);
    if (reader.Ok()) weather::LoadFields(*weatherState, reader);
    return reader.Ok();
}

Weather* Create()
{
    g_answeredConstruct.Count();
    Parameters parameters;
    std::string error;
    const char* path = "rules/weather_parameters.json";
    std::ifstream file(pack_path::Resolve(pack_path::Selected(), path), std::ios::binary);
    const auto document = file ? nlohmann::ordered_json::parse(file, nullptr, false) : nlohmann::ordered_json();
    if (!ReadParameters(document, parameters, error)) {
        game::FatalError("Cannot load %s (%s). Import the game data again", path, error.c_str());
        return nullptr;
    }
    auto* weatherState = new Weather();
    LifecycleEnv env;
    return weather::ConstructWeather(*weatherState, env, parameters);
}
void Destroy(Weather* weatherState)
{
    g_answeredDestruct.Count();
    LifecycleEnv env;
    weather::DestructWeather(*weatherState, env);
    delete weatherState;
}
} // namespace runtime

} // namespace obl::weather
