#pragma once

#include <nlohmann/json.hpp>

// The slot arrays of obl.save (docs/architecture/data-formats/saves.md, "Records and lists"): a save file holds named records
// and lists without their unused tail; the serializers and the old-record mapping work on fixed-size arrays, one per field.
// The game and the converter both pass through these two steps, so the file form has one definition.
namespace obl::json_schema {
class Registry;
}

namespace obl::formats {

// Fixed arrays to the file form: the last step before a document is validated and written.
void SlotsToRecords(nlohmann::ordered_json& document);
// Gives every object the state blocks of its kind and each missing field its schema `default` (saves.md, "Defaults").
// Runs on a validated document, before RecordsToSlots.
void FillSaveDefaults(nlohmann::json& document, const json_schema::Registry& schemas);
// The file form to fixed arrays: the first step after validation. A missing list or record reads as its unused value.
void RecordsToSlots(nlohmann::json& document);

} // namespace obl::formats
