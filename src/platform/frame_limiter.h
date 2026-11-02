#pragma once

namespace obl::platform {

// Caps rendering at [Display] MaxFps using SDL time. Simulation time advances independently.
bool ConfigureFrameLimiter();

// The browser cannot wait (docs/architecture/browser-build.md): its main loop runs at the display rate and skips the
// frames that would come before MaxFps allows the next one. True with no limit.
bool BrowserFrameDue();

} // namespace obl::platform
