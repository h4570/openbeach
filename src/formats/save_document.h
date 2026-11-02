#pragma once

#include "formats/save_states.h"

// The `obl.save` document contract shared by the game's native save load/save and the importer's old-save conversion
// (docs/architecture/data-formats/saves.md): the pack identity a save names, the f32 encoding, and the checks before a
// document loads (save-load-bridge.md, "Checks"): the schema, then what a schema cannot say (map size, unique object ids,
// object references, pack identity). A refusal names the field; the game loads nothing from a refused document.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace obl::json_schema {
class Registry;
}

namespace obl::formats {

// What the document names besides the save itself: the pack the keys come from and the converted file.
struct SaveOrigin {
    std::string file; // old slot file name, `converted_from.file`
    std::string packId = "beachlife-2002";
    std::string contentVersion = "1.0.0";
    std::string packHash; // 64 hex digits: SHA-256 of the pack's pack.json
    // The pack's default text language (pack.json `language`): narrow text of an old save is Windows-1250 for "pl",
    // Windows-1252 otherwise.
    std::string language;
    // The pack's text table text/<language>.json, first value per key: the title key's text becomes `title_text`.
    std::map<std::string, std::string, std::less<>> texts;
    // The pack's object state names (scripts/object_vocabulary.json) by id; `base.state` is written by name.
    SaveObjectStates objectStates;
};

// An f32 by its bits: the shortest round-tripping number, `-0.0` kept, and "f32:xxxxxxxx" for NaN and infinities.
nlohmann::ordered_json SaveFloat(uint32_t bits);
// The bits of an f32 as SaveFloat writes it; nullopt for any other value.
std::optional<uint32_t> SaveFloatBits(const nlohmann::json& value);

// The pack identity of `packRoot` for saves: id, content version, language from pack.json, the SHA-256 of pack.json and
// the layered text table of that language. Mods do not replace pack identity.
bool ReadSavePackOrigin(const std::filesystem::path& packRoot, SaveOrigin& origin, std::string& error);

// Validates `document` against obl.save and writes it to `output` (parent directories created).
bool WriteOblSaveDocument(const nlohmann::ordered_json& document, const std::filesystem::path& output, const json_schema::Registry& schemas,
        std::string& error);

// The side of the square cell grid every island map of the game has (128 x 128 cells); the map loader reads exactly the
// cells of the scenario's map, so a document with another size cannot load.
constexpr uint32_t kGameGridSide = 128;

struct OblSaveChecks {
    std::vector<std::string> refusals; // the document must not load
    std::vector<std::string> warnings; // it loads, with a log line (another build of the same pack)
};

// The semantic checks of a schema-valid document. `gridSide` 0 accepts any square grid (synthetic test saves).
OblSaveChecks CheckOblSaveDocument(const nlohmann::json& document, const SaveOrigin& origin, uint32_t gridSide = kGameGridSide);

// The game's document step: schema `obl.save`, then CheckOblSaveDocument. False with the first refusal in `error`;
// warnings are appended to `warnings`.
bool ValidateOblSaveDocument(const nlohmann::json& document, const SaveOrigin& origin, const json_schema::Registry& schemas,
        std::vector<std::string>& warnings, std::string& error, uint32_t gridSide = kGameGridSide);

} // namespace obl::formats
