#pragma once

namespace obl::platform {

// Installs the native crash-time writer of the game log (core/log.h EmergencyBackend); call before log::Open.
void InitLogEmergency();

} // namespace obl::platform
