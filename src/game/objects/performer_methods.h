#pragma once

#include "game/objects/object_base.h"

namespace obl::object {

// Performer creation calls SetVariant and AttachToBuilding; each method records its own diagnostic call count.
using PerformerMethodFunc = void (*)(object::Base* performer, int argument);
struct PerformerMethods {
    PerformerMethodFunc setVariant;
    PerformerMethodFunc attachToBuilding;
};
extern const PerformerMethods kAerobicMethods, kCabaretMethods, kCatwalkMethods, kSingerMethods, kWeightLifterMethods, kWetTShirtMethods;
} // namespace obl::object
