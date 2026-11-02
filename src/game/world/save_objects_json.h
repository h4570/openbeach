#pragma once

#include "game/world/world_save_json.h"

#include <nlohmann/json.hpp>

#include <string>

// The `objects` entries of obl.save written from the live objects (docs/architecture/data-formats/save-switch.md), family by
// family. Keys and value rules are the converter's (Objects() and the object tables of importer/src/readers/save/save_writer.cpp); the class fields
// come from each class's VisitFields, whose entries carry their obl.save keys.
namespace obl::object {
struct Base;
} // namespace obl::object

namespace obl::world_save {

// Whether WriteObjectJson writes the object's class yet (the families done so far; the others stay mapped from the binary records).
bool HasObjectWriter(const object::Base& object);

// One `objects` entry: id, kind, content key, the engine name when it differs, position, the creation name when it differs, then
// the class fields, the shared static or moving layer and the base with its sprite. False with `error` naming the field on a value
// obl.save cannot hold (a bool above 1) or on a class without a writer.
bool WriteObjectJson(const object::Base& object, const JsonContext& context, nlohmann::ordered_json& out, std::string& error);

} // namespace obl::world_save
