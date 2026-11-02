#pragma once

#include "game/objects/object_base.h"
#include "game/objects/object_table.h"

#include <cstdint>

// Furniture: the class's methods.
namespace obl::ui {
bool RegisterFurniture();

// Entries for the world's creator and the building.
bool TableSeatGuest(object::Furniture& tableObject, int32_t guest);
void TableAttachToBuilding(object::Furniture& tableObject, int32_t building);
void TableSetOrientation(object::Furniture& tableObject, int32_t orientation);
} // namespace obl::ui
