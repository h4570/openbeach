#pragma once

#include "game/events/event_state.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace obl::disaster {
bool RegisterEventTestDrivers();

namespace event_facade {

// The manager is created with new and freed with delete; it is not on the game heap. Create binds the global to it.
disaster::Manager* Create();
void Destroy(disaster::Manager* manager);
bool LoadJson(disaster::Manager* manager, const nlohmann::json& in, std::string& error);
// The manager the global names (null outside a world): the script's day offset and its disaster schedule go through it.
disaster::Manager* Current();
// Create and Destroy set the current manager (game::Context) through this; a scratch manager (the Save/Load checks) does not bind it.
void SetCurrent(disaster::Manager* value);

void AddEvent(disaster::Manager* manager, disaster::Event event);
void Update(disaster::Manager* manager);

} // namespace event_facade
} // namespace obl::disaster
