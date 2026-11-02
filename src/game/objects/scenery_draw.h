#pragma once

namespace obl::object {

struct Scenery;
}

namespace obl::object {
// The draw routines a scenery record stores in m_draw / m_drawLight (restoring saved scenery, scenery initialization): C++ functions
// with the field's signature (`Scenery::DrawFunc`). The pointers are neither saved nor hashed, and only Scenery::Draw calls them.
void SceneryDrawPreview(object::Scenery& scenery);
void SceneryDrawLounger(object::Scenery& scenery);
void SceneryDrawBench(object::Scenery& scenery);
void SceneryDrawBurned(object::Scenery& scenery);
void SceneryDrawLight(object::Scenery& scenery);
// The base Draw for a generic sprite (counted like `Base::Draw`) and the no-op light routine.
void SceneryDrawGeneric(object::Scenery& scenery);
void SceneryDrawNothing(object::Scenery& scenery);
} // namespace obl::object
