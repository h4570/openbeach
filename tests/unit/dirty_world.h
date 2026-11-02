#pragma once

#include "game/ui/view_state.h"
#include "game/world/world_state.h"
#include "game/world/world_script_types.h"

#include <cstring>

// Fills the World's plain groups with 0xA5 so a test sees which fields a step sets. The subsystem pointers and the PIS texts own memory and
// stay as constructed (null, empty).
inline void DirtyGroups(obl::world::World& world)
{
    auto fill = [](auto& group) { std::memset(static_cast<void*>(&group), 0xA5, sizeof(group)); };
    fill(static_cast<obl::world::Clock&>(world));
    fill(static_cast<obl::world::Scenario&>(world));
    fill(static_cast<obl::world::Boats&>(world));
    fill(static_cast<obl::world::Power&>(world));
    fill(static_cast<obl::world::Counters&>(world));
    fill(static_cast<obl::world::Finances&>(world));
    world.m_worldScript = reinterpret_cast<obl::world_script::WorldScript*>(static_cast<uintptr_t>(0xA5A5A5A5u));
    world.m_numObjectives = 0xA5A5A5A5u;
}

// The same for the map view's state, which world construction resets.
inline void DirtyView(obl::ui::ViewState& view)
{
    std::memset(static_cast<void*>(&view), 0xA5, sizeof(view));
}
