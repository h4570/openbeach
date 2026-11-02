#pragma once

#include "game/world/world_load_json.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

// The obl.save sections after `world` read into the live state (docs/architecture/data-formats/save-switch.md), one
// reader per section, the inverse of save_sections_json.h. A missing optional key gives the bridge's default (0, false, empty
// text, empty lists), so a native load and a bridge load of one document leave the same state. Each returns false with `error`
// naming the key on a value the section cannot hold.
namespace obl::player {
struct Player;
} // namespace obl::player
namespace obl::ui {
struct MapView;
} // namespace obl::ui
namespace obl::repair {
struct Manager;
} // namespace obl::repair
namespace obl::cleaning {
struct Manager;
} // namespace obl::cleaning
namespace obl::messages {
struct Handler;
} // namespace obl::messages
namespace obl::feedback {
struct Manager;
} // namespace obl::feedback
namespace obl::ferry {
struct Manager;
} // namespace obl::ferry

namespace obl::world_save {

// `player`: the name (UTF-16, up to its first NUL and at most 64 units) and the cash.
bool ReadPlayerJson(const nlohmann::json& in, player::Player& player, std::string& error);

// `view`: the fields of the map view load. The burn queue's entries go to `burnQueue` in order: the caller appends them to the
// view's deque and brings the camera in line (MapView::UpdateCamera), as the map view load does.
bool ReadViewJson(const nlohmann::json& in, ui::MapView& view, std::vector<int32_t>& burnQueue, std::string& error);

// The manager sections over the modules' LoadFields and the shared key tables (save_table_reader.h): `managers.repair`,
// `managers.cleaning`, `managers.messages`, `managers.feedback` (texts in the pack's code page) and `managers.ferry` (a missing
// `next_boat` is no cursor).
bool ReadRepairJson(const nlohmann::json& in, repair::Manager& manager, std::string& error);
bool ReadCleaningJson(const nlohmann::json& in, cleaning::Manager& manager, std::string& error);
bool ReadMessagesJson(const nlohmann::json& in, messages::Handler& handler, std::string& error);
bool ReadFeedbackJson(const nlohmann::json& in, const ReadContext& context, feedback::Manager& manager, std::string& error);
bool ReadFerryJson(const nlohmann::json& in, const ReadContext& context, ferry::Manager& manager, std::string& error);

// `managers.events`, `managers.weather` and `managers.construction` load through derived readers on the game side (their LoadFields
// need the scenario's schedules and the objects); these only check the values on scratch state, without the post-steps.
bool CheckEventsJson(const nlohmann::json& in, std::string& error);
bool CheckWeatherJson(const nlohmann::json& in, std::string& error);
bool CheckConstructionJson(const nlohmann::json& in, std::string& error);

} // namespace obl::world_save
