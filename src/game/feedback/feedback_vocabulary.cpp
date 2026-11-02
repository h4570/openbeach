#include "game/feedback/feedback_vocabulary.h"

#include "game/feedback/feedback_state.h"
#include "game/objects/staff_classes.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace obl::feedback {
namespace {

constexpr int kHireTypes = 6; // the hire buttons of the staff browsers

Vocabulary& Storage()
{
    static Vocabulary vocabulary;
    return vocabulary;
}

const nlohmann::json* Array(const nlohmann::json& doc, const char* name)
{
    const auto found = doc.find(name);
    return found != doc.end() && found->is_array() ? &*found : nullptr;
}

bool ReadString(const nlohmann::json& row, const char* name, std::string& out)
{
    const auto found = row.find(name);
    if (!row.is_object() || found == row.end() || !found->is_string()) return false;
    out = found->get<std::string>();
    return !out.empty();
}

bool ReadInt(const nlohmann::json& row, const char* name, int64_t& out)
{
    const auto found = row.find(name);
    if (!row.is_object() || found == row.end() || !found->is_number_integer()) return false;
    out = found->get<int64_t>();
    return true;
}

} // namespace

const Vocabulary& CurrentVocabulary()
{
    return Storage();
}

void SetVocabulary(Vocabulary vocabulary)
{
    Storage() = std::move(vocabulary);
}

bool ParseVocabulary(std::string_view json, Vocabulary& out, std::string& error)
{
    out = {};
    const auto fail = [&](const char* message) {
        out = {};
        error = message;
        return false;
    };
    const auto doc = nlohmann::json::parse(json, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return fail("not valid JSON");
    if (doc.value("format", "") != "obl.feedback_vocabulary" || doc.value("version", 0) != 1) return fail("unsupported format/version");

    const nlohmann::json* keys = Array(doc, "commodity_keys");
    if (!keys || keys->size() != static_cast<size_t>(kFamilies)) return fail("commodity_keys must list one key per commodity family");
    for (const auto& key : *keys) {
        if (!key.is_string() || key.get<std::string>().empty()) return fail("commodity_keys holds an empty or non-text key");
        out.commodityKeys.push_back(key.get<std::string>());
    }

    const nlohmann::json* types = Array(doc, "message_types");
    if (!types) return fail("message_types missing");
    for (const auto& row : *types) {
        MessageType type;
        int64_t number = 0;
        std::string scope;
        if (!ReadString(row, "word", type.word)) return fail("a message type has no word");
        if (ReadInt(row, "type", number) && number >= 0) {
            type.type = static_cast<uint32_t>(number);
        } else if (ReadString(row, "silences", scope) && (scope == "world" || scope == "object")) {
            type.suppressWorld = scope == "world";
            type.suppressObject = scope == "object";
        } else {
            return fail("a message type needs a type number or a silences scope");
        }
        out.messageTypes.push_back(std::move(type));
    }

    const nlohmann::json* missing = Array(doc, "missing_staff");
    if (!missing) return fail("missing_staff missing");
    for (const auto& row : *missing) {
        MissingStaff entry;
        std::string className, typeWord;
        if (!ReadString(row, "class", className) || !ReadString(row, "key", entry.key) || !ReadString(row, "type", typeWord))
            return fail("a missing_staff row needs class, key and type");
        const auto cls = object::StaffClassByName(className);
        if (!cls) return fail("a missing_staff row names no staff class");
        entry.objectClass = static_cast<int>(*cls);
        bool known = false;
        for (const MessageType& type : out.messageTypes)
            if (type.word == typeWord && !type.suppressWorld && !type.suppressObject) {
                entry.type = type.type;
                known = true;
            }
        if (!known) return fail("a missing_staff row names no message type");
        out.missingStaff.push_back(std::move(entry));
    }

    const nlohmann::json* professions = Array(doc, "professions");
    if (!professions) return fail("professions missing");
    for (const auto& row : *professions) {
        Profession profession;
        int64_t hireType = 0;
        if (!ReadString(row, "name", profession.name) || !ReadInt(row, "hire_type", hireType) || hireType < 0 || hireType >= kHireTypes)
            return fail("a profession needs a name and a hire_type of 0..5");
        profession.hireType = static_cast<int>(hireType);
        out.professions.push_back(std::move(profession));
    }
    error.clear();
    return true;
}

} // namespace obl::feedback
