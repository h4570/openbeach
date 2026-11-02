#pragma once

namespace obl::object {

struct Lifeguard;
} // namespace obl::object

namespace obl::object {

// The seven helpers the lifeguard script calls through the lifeguard's CallFunction (src/game/objects/staff_lifeguard_fns.h, docs/architecture/objects.md),
// counted as `objects.lifeguard-actions`; a call with an input they cannot handle ends in a fatal error.
void LifeguardFindBeach(object::Lifeguard& lifeguard);
void LifeguardSetDrawPosBuild(object::Lifeguard& lifeguard);
void LifeguardSetDrawPosServe(object::Lifeguard& lifeguard);
void LifeguardFindBeachPoint(object::Lifeguard& lifeguard);
void LifeguardMove2Tower(object::Lifeguard& lifeguard);
void LifeguardMove2SeaEntry(object::Lifeguard& lifeguard);
void LifeguardHeadForShore(object::Lifeguard& lifeguard);

} // namespace obl::object
