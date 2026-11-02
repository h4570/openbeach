#pragma once

#include "importer/src/readers/save/save_body.h"
#include "formats/save_document.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::json_schema {
class Registry;
}

namespace obl::convert {

// The shared obl.save contract (formats/save_document.h) the conversion writes against.
using formats::SaveFloat;
using formats::SaveFloatBits;
using formats::SaveOrigin;

// Every label of a converted section (World, ObjectManager, Player, ZEngine) is either mapped to an `obl.save` field or
// deliberately dropped; a label that is neither fails the conversion, so a new emitter field cannot be lost silently.
bool OldSectionLabelKnown(std::string_view section, std::string_view label);

// The obl.save key (relative to the object, "shark.swim_rect[]") of a class field of object type `type` under its old save label
// (an array element with its index, "m_SwimCircleRect[0]"); empty for a dropped or unrecognized label.
std::string OldObjectFieldKey(uint8_t type, std::string_view label);
// Whether some old label of object type `type` maps onto `key` (a list element as "list[]", a group's records under the group's
// key: "moving.path.valid"). The native class field tables (src/game/objects/object_fields.h) check their keys against it; the
// Moving and Static forms check Mover's and Prop's own records.
bool OldObjectFieldKeyKnown(uint8_t type, std::string_view key);
bool OldMovingFieldKeyKnown(std::string_view key);
bool OldStaticFieldKeyKnown(std::string_view key);

// The same for a record of the moving object save routine ("moving.mass" for "m_fMass"), shared by every moving class.
std::string OldMovingFieldKey(std::string_view label);

std::string OldStaticFieldKey(std::string_view label, uint32_t occurrence = 0);

// Puts `value` at `key` of `out` ("a.b" nests objects, each "[]" takes the next of `indices`; save-conversion.md, "Section
// tables"). False when the key is already set or an index is past the end of its list.
bool PlaceSaveValue(nlohmann::ordered_json& out, std::string_view key, std::span<const uint32_t> indices, nlohmann::ordered_json value);

// Builds the document from a read body and the catalog index. Fails with a named error on an object or building id the
// catalog does not hold, on a grid that is not square, and on duplicate allowed buildings.
bool OldSaveDocument(const OldSaveBody& body, const nlohmann::json& catalogIndex, const SaveOrigin& origin,
        nlohmann::ordered_json& document, std::string& error);

// The inverse of OldSaveDocument for the obl.save bridge (save-load-bridge.md): the document mapped back through the same
// tables to an old body whose records live in `body.storage`; dropped records are rebuilt. EncodeOldSave turns it into the
// bytes the game's loaders read. `origin.language` picks the code page of narrow text.
bool OldSaveBodyFromDocument(const nlohmann::json& document, const nlohmann::json& catalogIndex, const SaveOrigin& origin,
        OldSaveBody& body, std::string& error);

// The document of old save bytes (a slot file or the live world written to memory) against `packRoot`: CheckOldSaveBody,
// the pack identity (formats::ReadSavePackOrigin fills `origin`, its `file` is kept) and catalog/index.json, then OldSaveDocument.
// Not validated yet: the caller may still add fields (formats::WriteOblSaveDocument validates).
bool OldSaveBytesDocument(std::span<const uint8_t> bytes, const std::filesystem::path& packRoot, SaveOrigin& origin,
        nlohmann::ordered_json& document, std::string& error);

// Reads the old save, the pack's catalog/index.json and pack identity (formats::ReadSavePackOrigin), validates the result against
// obl.save and writes `output`.
bool ConvertOldSave(const std::filesystem::path& save, const std::filesystem::path& packRoot, const std::filesystem::path& output,
        const json_schema::Registry& schemas, std::string& error);

// The converter's document step: formats::ValidateOblSaveDocument, then OldSaveBodyFromDocument (defined in save_body.cpp).
bool LoadOblSaveDocument(const nlohmann::json& document, const nlohmann::json& catalogIndex, const SaveOrigin& origin,
        const json_schema::Registry& schemas, OldSaveBody& body, std::vector<std::string>& warnings, std::string& error,
        uint32_t gridSide = formats::kGameGridSide);

} // namespace obl::convert
