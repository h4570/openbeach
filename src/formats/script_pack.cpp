#include "formats/script_pack.h"

#include "core/ascii_case.h"
#include "core/pack_path.h"

#include <fstream>
#include <iterator>
#include <vector>

namespace obl::script_pack {
namespace {

constexpr std::string_view kObjectHeader = "#obl-script 1 object\n";
constexpr std::string_view kWorldHeader = "#obl-script 1 world\n";

std::string Lower(std::string_view text)
{
    std::string result(text);
    for (char& c : result) c = ascii::Lower(c);
    return result;
}

// The parts of an engine path between its separators; empty parts (a doubled or leading separator) are dropped.
std::vector<std::string> Parts(std::string_view path)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// `<name>.lgc` with a non-empty name: the name, lowercased, or "" otherwise.
std::string LogicName(const std::string& file)
{
    const std::string lower = Lower(file);
    if (!lower.ends_with(".lgc") || lower.size() <= 4) return "";
    return lower.substr(0, lower.size() - 4);
}

} // namespace

std::string_view Header(Kind kind)
{
    return kind == Kind::Object ? kObjectHeader : kWorldHeader;
}

bool ScenarioId(std::string_view folder, std::string& id)
{
    const std::string lower = Lower(folder);
    const auto allDigits = [](std::string_view digits) {
        if (digits.empty()) return false;
        for (char c : digits)
            if (c < '0' || c > '9') return false;
        return true;
    };
    if (lower.starts_with("sandscenario") && allDigits(std::string_view(lower).substr(12))) {
        id = "sandbox" + lower.substr(12);
        return true;
    }
    if (lower.starts_with("scenario") && allDigits(std::string_view(lower).substr(8))) {
        id = "s" + lower.substr(8);
        return true;
    }
    return false;
}

std::string PackPath(std::string_view enginePath, Kind kind)
{
    const auto parts = Parts(enginePath);
    if (parts.size() != 4) return "";
    if (kind == Kind::Object) {
        // data/script/object/<name>.lgc
        if (Lower(parts[0]) != "data" || Lower(parts[1]) != "script" || Lower(parts[2]) != "object") return "";
        const std::string name = LogicName(parts[3]);
        return name.empty() ? "" : "scripts/object/" + name + ".obs";
    }
    // Data/Scenario/<folder>/<folder>.lgc
    if (Lower(parts[0]) != "data" || Lower(parts[1]) != "scenario") return "";
    std::string id;
    if (!ScenarioId(parts[2], id) || LogicName(parts[3]) != Lower(parts[2])) return "";
    return "scenarios/" + id + "/world.wsc";
}

bool ReadScript(const std::filesystem::path& file, Kind kind, std::string& text, std::string& error)
{
    // The hint is the one the game prints when the pack is missing (docs/build/game-data.md, "No fallback").
    const std::string_view header = Header(kind);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = file.string() + ": cannot open (Import the game data again)";
        return false;
    }
    const std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (content.compare(0, header.size(), header) != 0) {
        error = file.string() + ": first line is not " + std::string(header.substr(0, header.size() - 1)) + " (Import the game data again)";
        return false;
    }
    text = content.substr(header.size());
    if (text.empty()) {
        error = file.string() + ": script is empty (Import the game data again)";
        return false;
    }
    return true;
}

bool ReadPackScript(const std::filesystem::path& packDir, std::string_view enginePath, Kind kind, std::string& text, std::string& error)
{
    const std::string relative = PackPath(enginePath, kind);
    if (relative.empty()) {
        error = std::string(enginePath) + ": not a script path of this kind";
        return false;
    }
    return ReadScript(pack_path::Resolve(packDir, relative), kind, text, error);
}

} // namespace obl::script_pack
