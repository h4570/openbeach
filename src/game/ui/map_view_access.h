#pragma once

#include "game/game/game_accessors.h"
#include "game/ui/map_view.h"

namespace obl::ui {

inline ui::MapView* GetMapView()
{
    return game::GetContext().mapView;
}
inline void SetMapView(ui::MapView* view)
{
    game::GetContext().mapView = view;
}

} // namespace obl::ui
