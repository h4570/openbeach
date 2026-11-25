#include "game/mods/input_routing.h"

#include <array>

namespace obl::mods {

namespace {

std::array<bool, kAreaCount> g_taken{};
TouchHandler g_touchHandler = nullptr;

} // namespace

bool IsTakenOver(Area area)
{
    return g_taken[static_cast<size_t>(area)];
}

void SetTakenOver(Area area, bool taken)
{
    g_taken[static_cast<size_t>(area)] = taken;
}

void SetTouchHandler(TouchHandler handler)
{
    g_touchHandler = handler;
}

bool RouteTouch(const TouchEvent& event)
{
    return g_touchHandler && g_touchHandler(event);
}

} // namespace obl::mods
