// The Linux obl-sim has no input devices: the Input object (game/ui/input_ops.cpp, shared with the Windows host) runs with its
// null backend, so every frame reads as no key, no button and no cursor (docs/architecture/input.md).

#include "game/ui/input_backend.h"

namespace obl::platform {

ui::InputBackend* PlatformInputBackend()
{
    return nullptr;
}

} // namespace obl::platform
