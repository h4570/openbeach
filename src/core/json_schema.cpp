#include "core/json_schema.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>
#include <utility>

namespace obl::json_schema {

namespace {

using nlohmann::json;

// Deep enough for every real schema; a reference cycle without progress stops here instead of overflowing the stack.
constexpr int kMaxDepth = 64;

const std::set<std::string> kAnnotations = {"$schema", "$id", "$comment", "title", "description", "examples", "default"};
const std::set<std::string> kTypes = {"null", "boolean", "object", "array", "number", "integer", "string"};
const std::set<std::string> kCountKeywords = {"minItems", "maxItems", "minLength", "maxLength", "minProperties", "maxProperties"};
const std::set<std::string> kBoundKeywords = {"minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum"};
const std::set<std::string> kSchemaMapKeywords = {"$defs", "properties", "patternProperties"};
const std::set<std::string> kSchemaKeywords = {"additionalProperties", "propertyNames", "items"};
const std::set<std::string> kSchemaListKeywords = {"prefixItems", "oneOf", "anyOf", "allOf"};

// JSON pointer escaping of one reference token (RFC 6901).
std::string Escape(const std::string& token)
{
    std::string out;
    for (char c : token) {
        if (c == '~')
            out += "~0";
        else if (c == '/')
            out += "~1";
        else
            out += c;
    }
    return out;
}

bool CompilesAsRegex(const std::string& pattern)
{
    try {
        std::regex compiled(pattern, std::regex::ECMAScript);
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

bool CheckSchema(const json& schema, const std::string& where, std::string& error);

bool CheckSchemaList(const json& value, const std::string& where, std::string& error)
{
    if (!value.is_array() || value.empty()) {
        error = where + ": expected a non-empty array of schemas";
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i)
        if (!CheckSchema(value[i], where + "/" + std::to_string(i), error)) return false;
    return true;
}

bool CheckSchema(const json& schema, const std::string& where, std::string& error)
{
    if (schema.is_boolean()) return true;
    if (!schema.is_object()) {
        error = where + ": a schema must be an object or a boolean";
        return false;
    }
    for (const auto& [key, value] : schema.items()) {
        const std::string at = where + "/" + Escape(key);
        bool ok = true;
        if (kAnnotations.count(key)) {
            ok = (key != "$id" && key != "$schema" && key != "$comment") || value.is_string();
        } else if (kSchemaMapKeywords.count(key)) {
            ok = value.is_object();
            if (ok)
                for (const auto& [name, sub] : value.items()) {
                    if (key == "patternProperties" && !CompilesAsRegex(name)) {
                        error = at + ": invalid pattern '" + name + "'";
                        return false;
                    }
                    if (!CheckSchema(sub, at + "/" + Escape(name), error)) return false;
                }
        } else if (kSchemaKeywords.count(key)) {
            if (!CheckSchema(value, at, error)) return false;
        } else if (kSchemaListKeywords.count(key)) {
            if (!CheckSchemaList(value, at, error)) return false;
        } else if (key == "$ref") {
            ok = value.is_string() && value.get<std::string>().find('#') != std::string::npos;
        } else if (key == "type") {
            if (value.is_string()) {
                ok = kTypes.count(value.get<std::string>()) != 0;
            } else {
                ok = value.is_array() && !value.empty();
                if (ok)
                    for (const auto& t : value) ok = ok && t.is_string() && kTypes.count(t.get<std::string>()) != 0;
            }
        } else if (key == "enum") {
            ok = value.is_array() && !value.empty();
        } else if (key == "const") {
            ok = true;
        } else if (key == "required") {
            ok = value.is_array();
            if (ok)
                for (const auto& name : value) ok = ok && name.is_string();
        } else if (kCountKeywords.count(key)) {
            ok = value.is_number_unsigned();
        } else if (kBoundKeywords.count(key)) {
            ok = value.is_number();
        } else if (key == "pattern") {
            ok = value.is_string() && CompilesAsRegex(value.get<std::string>());
        } else if (key == "uniqueItems") {
            ok = value.is_boolean();
        } else {
            error = at + ": keyword not supported by the validator";
            return false;
        }
        if (!ok) {
            error = at + ": invalid value";
            return false;
        }
    }
    return true;
}

void CollectRefs(const json& schema, std::vector<std::string>& refs)
{
    if (schema.is_object()) {
        for (const auto& [key, value] : schema.items()) {
            if (key == "$ref" && value.is_string())
                refs.push_back(value.get<std::string>());
            else if (key != "const" && key != "enum" && key != "examples" && key != "default")
                CollectRefs(value, refs);
        }
    } else if (schema.is_array()) {
        for (const auto& item : schema) CollectRefs(item, refs);
    }
}

bool HasType(const json& instance, const std::string& type)
{
    if (type == "null") return instance.is_null();
    if (type == "boolean") return instance.is_boolean();
    if (type == "object") return instance.is_object();
    if (type == "array") return instance.is_array();
    if (type == "number") return instance.is_number();
    if (type == "string") return instance.is_string();
    // "integer" is a number without a fractional part, so 1.0 counts as in the specification.
    if (instance.is_number_integer()) return true;
    if (!instance.is_number_float()) return false;
    const double value = instance.get<double>();
    return std::isfinite(value) && std::floor(value) == value;
}

size_t CodePoints(const std::string& text)
{
    size_t count = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++count;
    return count;
}

std::string Brief(const json& value)
{
    std::string text = value.dump();
    if (text.size() > 40) text = text.substr(0, 37) + "...";
    return text;
}

} // namespace

bool Registry::Add(nlohmann::json schema, std::string& error)
{
    if (!schema.is_object() || !schema.contains("$id") || !schema["$id"].is_string()) {
        error = "schema without a string \"$id\"";
        return false;
    }
    const std::string id = schema["$id"].get<std::string>();
    if (docs_.count(id)) {
        error = id + ": duplicate \"$id\"";
        return false;
    }
    if (!CheckSchema(schema, id + "#", error)) return false;
    docs_.emplace(id, std::move(schema));
    keywords_.clear(); // a ref that did not resolve before may resolve now
    return true;
}

std::vector<std::string> Registry::CheckRefs() const
{
    std::vector<std::string> unresolved;
    for (const auto& [id, doc] : docs_) {
        std::vector<std::string> refs;
        CollectRefs(doc, refs);
        for (const auto& ref : refs) {
            std::string target;
            if (!Resolve(ref, id, target)) unresolved.push_back(id + ": " + ref);
        }
    }
    return unresolved;
}

const nlohmann::json* Registry::Find(const std::string& id) const
{
    auto it = docs_.find(id);
    return it == docs_.end() ? nullptr : &it->second;
}

const nlohmann::json* Registry::Resolve(const std::string& ref, const std::string& docId, std::string& targetDocId) const
{
    const size_t hash = ref.find('#');
    if (hash == std::string::npos) return nullptr;
    targetDocId = hash == 0 ? docId : ref.substr(0, hash);
    const json* doc = Find(targetDocId);
    if (!doc) return nullptr;
    const std::string pointer = ref.substr(hash + 1);
    if (pointer.empty()) return doc;
    try {
        const json::json_pointer ptr(pointer);
        return doc->contains(ptr) ? &doc->at(ptr) : nullptr;
    } catch (const json::exception&) {
        return nullptr;
    }
}

const std::regex& Registry::Regex(const std::string& pattern) const
{
    auto& slot = regexes_[pattern];
    if (!slot) slot = std::make_unique<std::regex>(pattern, std::regex::ECMAScript);
    return *slot;
}

// The keywords ValidateNode reads, null when the schema leaves one out.
struct Registry::Keywords {
    const json* ref = nullptr;
    const json* type = nullptr;
    const json* constant = nullptr;
    const json* options = nullptr;
    const json* minimum = nullptr;
    const json* maximum = nullptr;
    const json* exclusiveMinimum = nullptr;
    const json* exclusiveMaximum = nullptr;
    const json* minLength = nullptr;
    const json* maxLength = nullptr;
    const json* pattern = nullptr;
    const json* minItems = nullptr;
    const json* maxItems = nullptr;
    const json* uniqueItems = nullptr;
    const json* prefixItems = nullptr;
    const json* items = nullptr;
    const json* minProperties = nullptr;
    const json* maxProperties = nullptr;
    const json* required = nullptr;
    const json* properties = nullptr;
    const json* patternProperties = nullptr;
    const json* additionalProperties = nullptr;
    const json* propertyNames = nullptr;
    const json* allOf = nullptr;
    const json* anyOf = nullptr;
    const json* oneOf = nullptr;
    const json* refTarget = nullptr; // what `ref` resolves to, in refDocId
    std::string refDocId;
};

const Registry::Keywords& Registry::KeywordsOf(const json& schema, const std::string& docId) const
{
    static constexpr std::pair<std::string_view, const json * Keywords::*> kNames[] = {
            {"$ref", &Registry::Keywords::ref},
            {"type", &Registry::Keywords::type},
            {"const", &Registry::Keywords::constant},
            {"enum", &Registry::Keywords::options},
            {"minimum", &Registry::Keywords::minimum},
            {"maximum", &Registry::Keywords::maximum},
            {"exclusiveMinimum", &Registry::Keywords::exclusiveMinimum},
            {"exclusiveMaximum", &Registry::Keywords::exclusiveMaximum},
            {"minLength", &Registry::Keywords::minLength},
            {"maxLength", &Registry::Keywords::maxLength},
            {"pattern", &Registry::Keywords::pattern},
            {"minItems", &Registry::Keywords::minItems},
            {"maxItems", &Registry::Keywords::maxItems},
            {"uniqueItems", &Registry::Keywords::uniqueItems},
            {"prefixItems", &Registry::Keywords::prefixItems},
            {"items", &Registry::Keywords::items},
            {"minProperties", &Registry::Keywords::minProperties},
            {"maxProperties", &Registry::Keywords::maxProperties},
            {"required", &Registry::Keywords::required},
            {"properties", &Registry::Keywords::properties},
            {"patternProperties", &Registry::Keywords::patternProperties},
            {"additionalProperties", &Registry::Keywords::additionalProperties},
            {"propertyNames", &Registry::Keywords::propertyNames},
            {"allOf", &Registry::Keywords::allOf},
            {"anyOf", &Registry::Keywords::anyOf},
            {"oneOf", &Registry::Keywords::oneOf},
    };
    std::shared_ptr<Keywords>& slot = keywords_[&schema];
    if (slot) return *slot;
    slot = std::make_shared<Keywords>();
    for (const auto& [key, value] : schema.items())
        for (const auto& [name, member] : kNames)
            if (key == name) (*slot).*member = &value;
    if (slot->ref) slot->refTarget = Resolve(slot->ref->get<std::string>(), docId, slot->refDocId);
    return *slot;
}

// A position in the instance, as a chain to the root: the JSON pointer is built only for an error.
struct Registry::Where {
    const Where* parent = nullptr;
    const std::string* key = nullptr; // an object member, else `index` of an array
    size_t index = 0;

    std::string Pointer() const
    {
        if (!parent) return {};
        return parent->Pointer() + "/" + (key ? Escape(*key) : std::to_string(index));
    }
};

std::vector<std::string> Registry::Validate(const std::string& id, const nlohmann::json& instance) const
{
    std::vector<std::string> errors;
    const json* schema = Find(id);
    if (!schema) {
        errors.push_back(": no schema with $id " + id);
        return errors;
    }
    ValidateNode(*schema, id, instance, Where{}, errors, 0);
    return errors;
}

void Registry::ValidateNode(const nlohmann::json& schema, const std::string& docId, const nlohmann::json& instance, const Where& where,
        std::vector<std::string>& errors, int depth) const
{
    auto fail = [&](const std::string& message) { errors.push_back(where.Pointer() + ": " + message); };
    if (depth > kMaxDepth) {
        fail("schema nesting too deep (reference cycle?)");
        return;
    }
    if (schema.is_boolean()) {
        if (!schema.get<bool>()) fail("no value is allowed here");
        return;
    }
    // One pass over the schema's few keys instead of a lookup per keyword: validating a save's 16k map cells spent most of its
    // Debug time in those lookups.
    const Keywords& k = KeywordsOf(schema, docId);

    if (const json* it = k.ref) {
        if (!k.refTarget)
            fail("unresolved $ref " + it->get<std::string>());
        else
            ValidateNode(*k.refTarget, k.refDocId, instance, where, errors, depth + 1);
    }

    if (const json* it = k.type) {
        bool ok = false;
        if (it->is_string())
            ok = HasType(instance, it->get<std::string>());
        else
            for (const auto& t : *it) ok = ok || HasType(instance, t.get<std::string>());
        if (!ok) {
            fail("expected type " + (it->is_string() ? it->get<std::string>() : it->dump()) + ", got " + Brief(instance));
            return; // the other keywords would only repeat the mismatch
        }
    }
    if (const json* it = k.constant; it && *it != instance) fail("expected " + Brief(*it) + ", got " + Brief(instance));
    if (const json* it = k.options) {
        bool found = false;
        for (const auto& option : *it) found = found || option == instance;
        if (!found) fail("value " + Brief(instance) + " is not one of " + Brief(*it));
    }

    if (instance.is_number()) {
        const double value = instance.get<double>();
        if (const json* it = k.minimum; it && value < it->get<double>()) fail("value " + Brief(instance) + " below minimum " + Brief(*it));
        if (const json* it = k.maximum; it && value > it->get<double>()) fail("value " + Brief(instance) + " above maximum " + Brief(*it));
        if (const json* it = k.exclusiveMinimum; it && value <= it->get<double>())
            fail("value " + Brief(instance) + " not above " + Brief(*it));
        if (const json* it = k.exclusiveMaximum; it && value >= it->get<double>())
            fail("value " + Brief(instance) + " not below " + Brief(*it));
    }

    if (instance.is_string()) {
        const std::string& text = instance.get_ref<const std::string&>();
        const size_t length = CodePoints(text);
        if (const json* it = k.minLength; it && length < it->get<size_t>()) fail("string shorter than " + Brief(*it));
        if (const json* it = k.maxLength; it && length > it->get<size_t>()) fail("string longer than " + Brief(*it));
        if (const json* it = k.pattern; it && !std::regex_search(text, Regex(it->get<std::string>())))
            fail("string " + Brief(instance) + " does not match " + it->get<std::string>());
    }

    if (instance.is_array()) {
        const size_t size = instance.size();
        if (const json* it = k.minItems; it && size < it->get<size_t>()) fail("fewer than " + Brief(*it) + " items");
        if (const json* it = k.maxItems; it && size > it->get<size_t>()) fail("more than " + Brief(*it) + " items");
        if (const json* it = k.uniqueItems; it && it->get<bool>())
            for (size_t i = 0; i < size; ++i)
                for (size_t j = i + 1; j < size; ++j)
                    if (instance[i] == instance[j]) fail("items " + std::to_string(i) + " and " + std::to_string(j) + " are equal");
        size_t prefix = 0;
        if (const json* it = k.prefixItems) {
            prefix = std::min(size, it->size());
            for (size_t i = 0; i < prefix; ++i) ValidateNode((*it)[i], docId, instance[i], Where{&where, nullptr, i}, errors, depth + 1);
        }
        if (const json* it = k.items)
            for (size_t i = prefix; i < size; ++i) ValidateNode(*it, docId, instance[i], Where{&where, nullptr, i}, errors, depth + 1);
    }

    if (instance.is_object()) {
        const size_t size = instance.size();
        if (const json* it = k.minProperties; it && size < it->get<size_t>()) fail("fewer than " + Brief(*it) + " properties");
        if (const json* it = k.maxProperties; it && size > it->get<size_t>()) fail("more than " + Brief(*it) + " properties");
        if (const json* it = k.required)
            for (const auto& name : *it)
                if (!instance.contains(name.get<std::string>())) fail("missing property \"" + name.get<std::string>() + "\"");
        const json* properties = k.properties;
        const json* patterns = k.patternProperties;
        const json* additional = k.additionalProperties;
        const json* names = k.propertyNames;
        for (const auto& [key, value] : instance.items()) {
            const Where at{&where, &key};
            if (names) ValidateNode(*names, docId, json(key), at, errors, depth + 1);
            bool matched = false;
            if (properties)
                if (auto p = properties->find(key); p != properties->end()) {
                    matched = true;
                    ValidateNode(*p, docId, value, at, errors, depth + 1);
                }
            if (patterns)
                for (const auto& [pattern, sub] : patterns->items())
                    if (std::regex_search(key, Regex(pattern))) {
                        matched = true;
                        ValidateNode(sub, docId, value, at, errors, depth + 1);
                    }
            if (!matched && additional) {
                if (additional->is_boolean() && !additional->get<bool>())
                    errors.push_back(at.Pointer() + ": unrecognized property");
                else
                    ValidateNode(*additional, docId, value, at, errors, depth + 1);
            }
        }
    }

    if (const json* it = k.allOf)
        for (const auto& sub : *it) ValidateNode(sub, docId, instance, where, errors, depth + 1);
    for (const auto& [keyword, it] : {std::pair{"anyOf", k.anyOf}, std::pair{"oneOf", k.oneOf}}) {
        if (!it) continue;
        size_t matches = 0;
        std::string firstError;
        for (const auto& sub : *it) {
            std::vector<std::string> branch;
            ValidateNode(sub, docId, instance, where, branch, depth + 1);
            if (branch.empty())
                ++matches;
            else if (firstError.empty())
                firstError = branch.front();
        }
        if (matches == 0)
            fail(std::string("matches none of ") + keyword + " (first branch: " + firstError + ")");
        else if (std::string(keyword) == "oneOf" && matches > 1)
            fail("matches " + std::to_string(matches) + " branches of oneOf");
    }
}

} // namespace obl::json_schema
