#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_performers.h"

// The records of the six performer classes (docs/architecture/objects.md, "Save/Load") for the obl.save loader: the class's field table
// (object::VisitFields, object_performers.h), then the Person base. The Env owns what stays game-side: the sprite records
// (`Attached`) and the base.
//   Env::Int/UInt/Float/Bool(label, key, field, hashed)
//   Env::Attached(key, void*& sprite, AttachedMode)   the attached sprite
//   Env::Base(performer)                                   Person
namespace obl::performer {

using object::AttachedMode;

template<class T, class Env> void Fields(T& performer, Env& env)
{
    object::VisitFields(performer, env);
    env.Base(performer);
}

} // namespace obl::performer
