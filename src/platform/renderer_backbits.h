#pragma once

#include "game/ui/renderer_api.h"
#include "game/ui/renderer_types.h"

#include <cstdint>

namespace obl::platform {

// Shared native access to the active surface's pixels.
inline uint16_t* BackBits(ui::Renderer& r)
{
    return obl::ui::renderer::GetBackBufferBits(&r);
}

} // namespace obl::platform
