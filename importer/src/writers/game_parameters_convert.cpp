#include "importer/src/writers/game_parameters_convert.h"

#include "core/ascii_case.h"
#include "core/float_to_int.h"
#include "importer/src/readers/pis.h"
#include "importer/src/writers/catalog_writer.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace obl::convert {

using nlohmann::ordered_json;

namespace {

constexpr uint64_t kMaxPisBytes = 1u << 20;

// A content key: lowercase letters, digits and '_', not empty.
bool IsContentKey(const std::string& name)
{
    if (name.empty()) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// The engine's node slots: the key (with its type letter) and a string value are copied into 0x100-byte buffers with their NUL.
constexpr size_t kMaxSlotText = 0xff;

} // namespace

bool ParseGameParameters(std::string_view text, ordered_json& parameters, std::string& error)
{
    // The engine's own reader (game/tables/pis.h) splits the entries, so comments, separators and quoting are the game's.
    std::string buffer(text);
    const auto entries = pis::ParseEntries(buffer.data(), static_cast<int>(buffer.size()));
    parameters = ordered_json::object();
    std::set<std::string> exactKeys;
    std::map<std::string, std::string> keyOfName; // JSON name -> the exact key it came from
    for (const auto& entry : entries) {
        const std::string& key = entry.key;
        // The engine switches on the first byte exactly: an upper-case type letter is a fatal error there.
        if (key.size() < 2 || std::string_view("bifs").find(key[0]) == std::string_view::npos) {
            error = "key '" + key + "' does not start with a lowercase type letter (b, i, f or s)";
            return false;
        }
        if (key.size() > kMaxSlotText) {
            error = "key '" + key + "' does not fit the engine's name slot";
            return false;
        }
        // The engine's lookup compares whole keys and answers the first node, so a repeated key keeps its first value.
        if (!exactKeys.insert(key).second) continue;
        std::string name = key.substr(1);
        for (char& c : name) {
            if (c >= 'a' && c <= 'z') {
                error = "key '" + key + "' has a lower-case letter after its type letter";
                return false;
            }
            c = ascii::Lower(c);
        }
        if (!IsContentKey(name)) {
            error = "key '" + key + "' is not a name the converter can write";
            return false;
        }
        const auto [owner, inserted] = keyOfName.emplace(name, key);
        if (!inserted) {
            error = "keys '" + owner->second + "' and '" + key + "' both name " + name;
            return false;
        }

        switch (key[0]) {
        case 'b':
            // The engine stores true only for the string "true"; any other string, or no string, is false.
            if (entry.type != pis::String) {
                error = "key '" + key + "' is a bool and needs a quoted value";
                return false;
            }
            parameters[name] = entry.hasString && entry.string == "true";
            break;
        case 'i':
            if (entry.type != pis::Number) {
                error = "key '" + key + "' is an integer and needs a number";
                return false;
            }
            parameters[name] = obl::FloatToIntWrap(entry.number);
            break;
        case 'f':
            if (entry.type != pis::Number) {
                error = "key '" + key + "' is a float and needs a number";
                return false;
            }
            // JSON has no infinity: nlohmann would write null, which the schema refuses with a vaguer message.
            if (!std::isfinite(entry.number)) {
                error = "key '" + key + "' is not a finite float";
                return false;
            }
            parameters[name] = FloatNumber(entry.number);
            break;
        default: // 's'
            if (entry.type != pis::String || !entry.hasString) {
                error = "key '" + key + "' is a string and needs a quoted value";
                return false;
            }
            if (entry.string.size() > kMaxSlotText) {
                error = "string of '" + key + "' does not fit the engine's value slot";
                return false;
            }
            parameters[name] = entry.string;
            break;
        }
    }
    return true;
}

bool ConvertGameParameters(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, std::string& error)
{
    try {
        const auto script = FindFile(source, "Data/Script/GameParameters.pis");
        if (!script) {
            error = "missing installation Data/Script/GameParameters.pis";
            return false;
        }
        std::vector<uint8_t> bytes;
        if (!ReadBoundedFile(source, *script, kMaxPisBytes, bytes, error)) return false;
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        ordered_json parameters;
        if (!ParseGameParameters(text, parameters, error)) {
            error = "GameParameters.pis: " + error;
            return false;
        }
        ordered_json document;
        document["format"] = "obl.game_parameters";
        document["version"] = 1;
        document["parameters"] = parameters;
        if (!formats::ValidateDocument(schemas, "obl.game_parameters", document, error)) return false;

        if (!out.WriteText("rules/game_parameters.json", document.dump(2) + "\n", error)) return false;
        const auto weather = ordered_json::parse(
                R"weather({"format": "obl.weather_parameters", "version": 2, "light_keys": [[0, 4934500], [1, 4934500], [2, 4934500], [3, 4934500], [4, 4934500], [5, 7627876], [6, 10386788], [7, 13145700], [8, 14334359], [9, 15523019], [10, 16777215], [11, 16777215], [12, 16777215], [13, 16777215], [14, 16777215], [15, 16777215], [16, 16777215], [17, 15523019], [18, 14334359], [19, 13145700], [20, 10386788], [21, 7627876], [22, 4934500], [23, 4934500]], "state_colors": [0, 0, 1579024, 3158048, 5263424, 0, 16777215, 16777215, 16777215, 16777215, 16777215, 16777215, 16777215, 16777215]})weather");
        if (!formats::ValidateDocument(schemas, "obl.weather_parameters", weather, error)) return false;
        return out.WriteText("rules/weather_parameters.json", weather.dump(2) + "\n", error);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
