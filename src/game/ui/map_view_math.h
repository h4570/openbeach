#pragma once

#include "game/game/game_accessors.h"
#include "game/ui/map_view.h"
#include "game/ui/map_math.h"
#include "game/ui/map_view_access.h"

namespace obl::ui {

struct TerrainView {
    map_math::HeightMap map{};
    map_math::Terrain terrain{};

    explicit TerrainView(const ui::MapView* view);
    TerrainView(const TerrainView&) = delete;
    TerrainView& operator=(const TerrainView&) = delete;
};

} // namespace obl::ui
