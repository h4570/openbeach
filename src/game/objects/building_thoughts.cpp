#include "core/char16.h"
#include "game/objects/building_methods.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_thoughts.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"

#include <cstdint>

namespace obl::object {

namespace {

using object::Building;

struct Counters {
    test::CallCounter counter;
    explicit Counters(const char* name) : counter(name) {}
};

Counters g_add("objects.building-add-thought");
Counters g_remove("objects.building-remove-thoughts-of");

} // namespace

void BuildingAddThought(Building* building, Char16* text, int32_t guestId)
{
    g_add.counter.Count();
    AddBuildingThought(building->m_ThoughtList, text, guestId);
}

void BuildingRemoveThoughtsOf(Building* building, int32_t objectId)
{
    g_remove.counter.Count();
    RemoveBuildingThoughts(building->m_ThoughtList, objectId);
}

} // namespace obl::object
