#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

// A validator for the subset of JSON Schema (draft 2020-12) that the schemas of data/schemas/ use
// (docs/architecture/data-formats/schemas.md). Our own code instead of a library: the converter validates its output with
// it, it builds on the 32-bit Linux CI without another dependency, and a schema that uses a keyword outside the subset is
// refused when it is added instead of the keyword being ignored, so a schema can never pass data it does not check.
namespace obl::json_schema {

class Registry {
public:
    // Adds a schema document under its "$id" (required, unique). Fails, naming the location, when the document uses a
    // keyword the validator does not implement or a keyword value of the wrong kind.
    bool Add(nlohmann::json schema, std::string& error);

    // Every "$ref" of every added document that does not resolve, as "<id>: <ref>"; empty when all resolve.
    std::vector<std::string> CheckRefs() const;

    const nlohmann::json* Find(const std::string& id) const;

    // Validates `instance` against the document `id`. Each error is "<JSON pointer into the instance>: <message>"; the
    // result is empty when the instance is valid.
    std::vector<std::string> Validate(const std::string& id, const nlohmann::json& instance) const;

private:
    struct Where;
    struct Keywords;
    void ValidateNode(const nlohmann::json& schema, const std::string& docId, const nlohmann::json& instance, const Where& where,
            std::vector<std::string>& errors, int depth) const;
    const Keywords& KeywordsOf(const nlohmann::json& schema, const std::string& docId) const;
    const nlohmann::json* Resolve(const std::string& ref, const std::string& docId, std::string& targetDocId) const;
    const std::regex& Regex(const std::string& pattern) const;

    std::map<std::string, nlohmann::json> docs_;
    mutable std::map<std::string, std::unique_ptr<std::regex>> regexes_;
    // The keywords of every schema node met so far, by node: a save's 16k map cells validate against the same item schema,
    // and gathering its keywords and resolving its "$ref" again per cell was most of a Debug load.
    mutable std::map<const nlohmann::json*, std::shared_ptr<Keywords>> keywords_; // shared_ptr: Keywords is complete only in the .cpp
};

} // namespace obl::json_schema
