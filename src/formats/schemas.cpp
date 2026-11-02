#include "formats/schemas.h"

#include "core/json_schema.h"
#include "embedded_schemas.gen.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <vector>

namespace obl::formats {

bool LoadEmbeddedSchemas(json_schema::Registry& registry, std::string& error)
{
    for (const char* text : kEmbeddedSchemas) {
        auto schema = nlohmann::json::parse(text, nullptr, false);
        if (schema.is_discarded()) {
            error = "embedded schema: not valid JSON";
            return false;
        }
        if (!registry.Add(std::move(schema), error)) return false;
    }
    if (auto missing = registry.CheckRefs(); !missing.empty()) {
        error = "unresolved embedded schema reference " + missing.front();
        return false;
    }
    return true;
}

bool LoadSchemaDirectory(const std::filesystem::path& dir, json_schema::Registry& registry, std::string& error)
{
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() > 12 && name.ends_with(".schema.json")) files.push_back(entry.path());
    }
    if (ec || files.empty()) {
        error = "no schemas in " + dir.string();
        return false;
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        nlohmann::json schema = nlohmann::json::parse(in, nullptr, false);
        if (schema.is_discarded()) {
            error = file.string() + ": not valid JSON";
            return false;
        }
        std::string reason;
        if (!registry.Add(std::move(schema), reason)) {
            error = file.string() + ": " + reason;
            return false;
        }
    }
    if (auto missing = registry.CheckRefs(); !missing.empty()) {
        error = "unresolved schema reference " + missing.front();
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> FindSchemaDirectory(const std::filesystem::path& start)
{
    std::error_code ec;
    for (auto dir = std::filesystem::absolute(start, ec); !ec && !dir.empty(); dir = dir.parent_path()) {
        const auto candidate = dir / "data" / "schemas";
        if (std::filesystem::exists(candidate / "pack.schema.json", ec)) return candidate;
        if (dir == dir.parent_path()) break;
    }
    return std::nullopt;
}

bool ValidateDocument(
        const json_schema::Registry& registry, const std::string& id, const nlohmann::ordered_json& document, std::string& error)
{
    // The validator takes the sorted-key json; the writers use ordered_json to keep a readable field order.
    const auto errors = registry.Validate(id, nlohmann::json::parse(document.dump()));
    if (errors.empty()) return true;
    error = id + " schema:";
    for (size_t i = 0; i < errors.size() && i < 10; ++i) error += "\n  " + errors[i];
    if (errors.size() > 10) error += "\n  (" + std::to_string(errors.size() - 10) + " more)";
    return false;
}

} // namespace obl::formats
