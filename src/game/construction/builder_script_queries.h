#pragma once

namespace obl::object {
struct Builder;
}

namespace obl::construction {

// ConstructionManager (docs/specs/construction.md): construction::CheckForFemaleGuests and construction::FindNearestValidTile, the two
// helpers the builder script calls, as native functions over the C++ in src/game/construction/builder_script_actions.h (each counts its `.calls`
// counter and construction.builder-script-queries). The builder's call table calls them directly; their image addresses are not installed.
void CheckForFemaleGuests(object::Builder* builder);
void FindNearestValidTile(object::Builder* builder);

} // namespace obl::construction
