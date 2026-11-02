#pragma once

#include "core/rect.h"
#include "game/ui/map_view.h"
#include "game/ui/sprite.h"

#include <cstdint>

namespace obl::ui {
uint16_t ActorTileDepthOffset(ui::MapView* view, const float* world);
} // namespace obl::ui
