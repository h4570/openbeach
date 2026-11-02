#pragma once

// The schemas of data/schemas/: the contract of every file of a pack and of native saves, shared by the game and the
// importer (every JSON the importer writes is validated against them).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace obl::json_schema {
class Registry;
}

namespace obl::formats {

// The complete checked-in schema bundle, independent of the executable location and working directory.
bool LoadEmbeddedSchemas(json_schema::Registry& registry, std::string& error);

// Adds every *.schema.json of `dir` to `registry` and checks that all references resolve.
bool LoadSchemaDirectory(const std::filesystem::path& dir, json_schema::Registry& registry, std::string& error);

// data/schemas/ of the checkout: the first directory from `start` upwards that holds data/schemas/pack.schema.json
// (the build tree lies inside the checkout).
std::optional<std::filesystem::path> FindSchemaDirectory(const std::filesystem::path& start);

// Validates `document` against the schema `id`; false with the errors (at most ten) joined into `error`.
bool ValidateDocument(
        const json_schema::Registry& registry, const std::string& id, const nlohmann::ordered_json& document, std::string& error);

} // namespace obl::formats
