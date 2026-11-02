#pragma once

#include "game/game/game_context.h"
#include "game/objects/object_staff.h"

#include <array>
#include <cstring>
#include <iterator>

// The class-wide staff counters that the obl.save writer and loader read and write through the staff members' field tables
// (object::VisitFields, object_staff.h).
namespace obl::staff::lifecycle {

using GameContext = game::Context;

// The save record's class counters (VisitFields' Counter keys); the whistling counter is the one all builders share
// (NumBuildersWhistling()).
constexpr std::array<int32_t GameContext::*, 6> kCounters = {&GameContext::staffBuilderCount, &GameContext::numBuildersWhistling,
        &GameContext::staffCleanerCount, &GameContext::staffMechanicCount, &GameContext::staffRepCount, &GameContext::staffGuardCount};
constexpr const char* kCounterNames[] = {"builder.class_count", "builder.whistling", "cleaner.class_count", "mechanic.class_count",
        "rep.class_count", "security.class_count"};
static_assert(std::size(kCounterNames) == kCounters.size());

// The counter behind a Counter key (the first one for an unknown key).
inline int32_t& CounterValue(const char* key)
{
    size_t i = 0;
    while (i + 1 < kCounters.size() && std::strcmp(key, kCounterNames[i]) != 0) ++i;
    if (std::strcmp(key, kCounterNames[i]) != 0) i = 0;
    return game::GetContext().*kCounters[i];
}

} // namespace obl::staff::lifecycle
