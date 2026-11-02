#include "formats/save_document.h"
#include "formats/cell_state.h"

#include "core/json_float.h"
#include "core/json_schema.h"
#include "core/pack_path.h"
#include "core/sha256.h"
#include "formats/schemas.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <map>

namespace obl::formats {
namespace {
using nlohmann::json;

// The ObjectManager's id list holds 500 building slots, then 1500 guest slots, then 16500 for the other objects
// (m_dwObjectIDList, 18500 entries); the bridge rebuilds it from the document's object order.
constexpr size_t kGroupSlots[] = {500, 1500, 16500};
constexpr const char* kGroupNames[] = {"buildings", "guests", "other objects"};

int GroupOf(const json& object)
{
    const std::string kind = object["kind"];
    return kind == "building" ? 0 : kind == "guest" ? 1 : 2;
}

bool IsBuilder(const json& object)
{
    return object["kind"] == "staff" && object.value("class", "") == "builder";
}

const json* Find(const json& scope, std::initializer_list<const char*> path)
{
    const json* at = &scope;
    for (const char* key : path) {
        const auto it = at->find(key);
        if (it == at->end()) return nullptr;
        at = &*it;
    }
    return at;
}

class Checker {
public:
    Checker(const json& d, const SaveOrigin& origin, uint32_t gridSide, OblSaveChecks& out)
        : m_d(d), m_origin(origin), m_gridSide(gridSide), m_out(out)
    {}

    void Run()
    {
        Packs();
        Map();
        Ids();
        References();
    }

private:
    void Refuse(std::string why) { m_out.refusals.push_back(std::move(why)); }

    // The packs the save names: the installed base pack must be one of them, and no other pack is installed yet
    // (saves.md: a missing pack fails with its name, a different build of the same pack warns).
    void Packs()
    {
        bool base = false;
        for (const json& pack : m_d["header"]["packs"]) {
            const std::string id = pack["id"];
            if (id != m_origin.packId) {
                Refuse("header.packs: the save needs pack " + id + ", which is not installed");
                continue;
            }
            base = true;
            const std::string version = pack["content_version"], hash = pack["pack_hash"];
            if (version != m_origin.contentVersion)
                m_out.warnings.push_back(
                        "header.packs: saved with " + id + " " + version + ", the installed pack is " + m_origin.contentVersion);
            else if (hash != m_origin.packHash)
                m_out.warnings.push_back("header.packs: " + id + " was saved with another pack.json (" + hash + ")");
        }
        if (!base) Refuse("header.packs: does not name the installed pack " + m_origin.packId);
    }

    void Map()
    {
        const json& map = m_d["map"];
        const int64_t w = map["cells"][0], h = map["cells"][1];
        const std::string size = std::to_string(w) + " x " + std::to_string(h);
        if (w <= 0 || w != h) return Refuse("map.cells: " + size + " is not a square grid");
        if (m_gridSide && w != int64_t(m_gridSide))
            return Refuse("map.cells: " + size + ", the game's grid is " + std::to_string(m_gridSide) + " x " + std::to_string(m_gridSide));
        if (map["heights"].size() != uint64_t(w * h))
            Refuse("map.heights: " + std::to_string(map["heights"].size()) + " entries for " + size + " cells");
        for (const CellField& field : CellFields()) {
            uint64_t cells = 0;
            for (const json& run : map["cell_state"][field.name]) cells += run[0].get<uint64_t>();
            if (cells != uint64_t(w * h))
                Refuse("map.cell_state." + std::string(field.name) + ": " + std::to_string(cells) + " cells for " + size + " cells");
        }
        // Optional in the schema, but the grid load reads one record per area of the island's map, a count only the map knows.
        if (!map.contains("areas_open")) Refuse("map.areas_open: missing (one open flag per area of the island)");
    }

    // Unique ids below the manager's next free id; buildings first, then guests, then the rest, each group within its
    // slots of the id list.
    void Ids()
    {
        const json* next = Find(m_d, {"managers", "objects", "next_id"}); // absent: the bridge defaults it past the ids
        size_t groups[3] = {};
        int last = 0;
        for (const json& object : m_d["objects"]) {
            const int64_t id = object["id"];
            const int group = GroupOf(object);
            const std::string what = "objects: " + object["kind"].get<std::string>() + " " + std::to_string(id);
            if (group < last) Refuse(what + " after the " + kGroupNames[last] + " (buildings, guests, then the rest)");
            last = std::max(last, group);
            if (++groups[group] == kGroupSlots[group] + 1)
                Refuse("objects: more than " + std::to_string(kGroupSlots[group]) + " " + kGroupNames[group]);
            if (id < 0 || (next && id >= next->get<int64_t>()))
                Refuse(what + ": id outside 0.." + (next ? std::to_string(next->get<int64_t>()) : std::string("2^31")));
            if (!m_ids.emplace(id, &object).second) Refuse("objects: duplicate id " + std::to_string(id));
        }
    }

    // `value` names a live object (-1 is none everywhere); `test`/`kind` narrow the class. `removed`: an id of no object is
    // allowed too (an object that outlived its removed owner).
    void Reference(const std::string& where, const json& value, bool (*test)(const json&) = nullptr, const char* kind = nullptr,
            bool removed = false)
    {
        const int64_t id = value;
        if (id == -1) return;
        const auto it = m_ids.find(id);
        if (it == m_ids.end()) return removed ? void() : Refuse(where + ": no object " + std::to_string(id));
        if (test && !test(*it->second))
            Refuse(where + ": object " + std::to_string(id) + " is a " + (*it->second)["kind"].get<std::string>() + ", not a " + kind);
    }

    void References()
    {
        const auto building = +[](const json& o) { return o["kind"] == "building"; };
        const auto guest = +[](const json& o) { return o["kind"] == "guest"; };
        for (const json& object : m_d["objects"]) {
            const int64_t id = object["id"];
            const std::string at = "objects[id " + std::to_string(id) + "].";
            if (const json* b = Find(object, {"building"})) {
                for (const char* part : {"queue", "events"}) {
                    const json* owner = Find(*b, {part, "owner"});
                    if (owner && *owner != id)
                        Refuse(at + "building." + part + ".owner: " + owner->dump() + " is not the building's own id");
                }
                if (const json* queue = Find(*b, {"queue", "guests"}))
                    for (const json& g : *queue) Reference(at + "building.queue.guests", g, guest, "guest");
                if (const json* slots = Find(*b, {"events", "slots"}))
                    for (const json& slot : *slots)
                        for (const char* list : {"actors", "guests"})
                            if (const json* refs = Find(slot, {list}))
                                for (const json& ref : *refs) Reference(at + "building.events.slots[]." + list, ref);
            }
            // The tables of a removed building stay in the world for a while, holding its id (furniture_paths.txt saves
            // a few frames after remove-building): the game resolves the id to no building, and the old save kept it too.
            if (const json* owner = Find(object, {"table", "owner"})) Reference(at + "table.owner", *owner, building, "building", true);
        }
        const auto jobs = [&](const char* manager, const char* list, const char* key, bool (*test)(const json&), const char* kind,
                                  bool removed) {
            if (const json* entries = Find(m_d, {"managers", manager, list}))
                for (const json& entry : *entries)
                    Reference("managers." + std::string(manager) + "." + list + "[]." + key, entry[key], test, kind, removed);
        };
        // Repair and cleaning jobs outlive a removed object too (the managers look the id up when they serve the job).
        for (const char* list : {"building_jobs", "scenery_jobs", "active_jobs"}) jobs("repair", list, "object", nullptr, nullptr, true);
        for (const char* list : {"jobs", "active_jobs"}) jobs("cleaning", list, "object", nullptr, nullptr, true);
        jobs("construction", "jobs", "building", building, "building", false);
        jobs("construction", "builders", "staff", IsBuilder, "builder", false);
    }

    const json& m_d;
    const SaveOrigin& m_origin;
    uint32_t m_gridSide;
    OblSaveChecks& m_out;
    std::map<int64_t, const json*> m_ids;
};
} // namespace

nlohmann::ordered_json SaveFloat(uint32_t bits)
{
    return json_float::Bits(bits);
}

std::optional<uint32_t> SaveFloatBits(const nlohmann::json& value)
{
    if (value.is_number()) return std::bit_cast<uint32_t>(static_cast<float>(value.get<double>()));
    if (!value.is_string()) return std::nullopt;
    const std::string& text = value.get_ref<const std::string&>();
    if (text.size() != 12 || !text.starts_with("f32:")) return std::nullopt;
    return static_cast<uint32_t>(std::stoul(text.substr(4), nullptr, 16));
}

namespace {
std::optional<nlohmann::json> ReadJsonFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    nlohmann::json value = nlohmann::json::parse(in, nullptr, false);
    if (value.is_discarded() || !value.is_object()) return std::nullopt;
    return value;
}
} // namespace

bool ReadSavePackOrigin(const std::filesystem::path& packRoot, SaveOrigin& origin, std::string& error)
{
    const std::filesystem::path manifestPath = packRoot / "pack.json";
    const auto manifest = ReadJsonFile(manifestPath);
    const auto hash = sha256::OfFile(manifestPath);
    if (!manifest || !hash) {
        error = "cannot read " + manifestPath.string() + " (run obl-convert pack first)";
        return false;
    }
    const auto field = [&](const char* key) {
        const auto it = manifest->find(key);
        return it != manifest->end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    origin.packId = field("id");
    origin.contentVersion = field("content_version");
    origin.language = field("language");
    if (origin.packId.empty() || origin.contentVersion.empty() || origin.language.empty()) {
        error = manifestPath.string() + ": no id, content_version or language";
        return false;
    }
    origin.packHash = *hash;
    const std::filesystem::path textPath = pack_path::Resolve(packRoot, "text/" + origin.language + ".json");
    const auto table = ReadJsonFile(textPath);
    if (!table || !table->contains("entries") || !(*table)["entries"].is_array()) {
        error = "cannot read " + textPath.string() + " (the text table of the pack's language)";
        return false;
    }
    origin.texts.clear();
    for (const auto& entry : (*table)["entries"])
        if (entry.is_array() && entry.size() == 2 && entry[0].is_string() && entry[1].is_string())
            origin.texts.emplace(entry[0].get<std::string>(), entry[1].get<std::string>()); // the first value wins
    origin.objectStates.clear();
    if (const auto vocabulary = ReadJsonFile(pack_path::Resolve(packRoot, "scripts/object_vocabulary.json")))
        if (const auto states = vocabulary->find("states"); states != vocabulary->end() && states->is_array())
            for (const auto& state : *states) origin.objectStates.emplace(state.value("id", -1), state.value("name", std::string()));
    return true;
}

bool WriteOblSaveDocument(const nlohmann::ordered_json& document, const std::filesystem::path& output, const json_schema::Registry& schemas,
        std::string& error)
{
    if (!ValidateDocument(schemas, "obl.save", document, error)) return false;
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output, std::ios::binary);
    out << document.dump(1) << '\n';
    if (!out) {
        error = "cannot write " + output.string();
        return false;
    }
    return true;
}

OblSaveChecks CheckOblSaveDocument(const json& document, const SaveOrigin& origin, uint32_t gridSide)
{
    OblSaveChecks out;
    Checker(document, origin, gridSide, out).Run();
    return out;
}

bool ValidateOblSaveDocument(const json& document, const SaveOrigin& origin, const json_schema::Registry& schemas,
        std::vector<std::string>& warnings, std::string& error, uint32_t gridSide)
{
    // The schema first: the checks and the mapping read the required keys without testing for them.
    if (const auto errors = schemas.Validate("obl.save", document); !errors.empty()) {
        error = "obl.save schema: " + errors.front();
        if (errors.size() > 1) error += " (and " + std::to_string(errors.size() - 1) + " more)";
        return false;
    }
    OblSaveChecks checks = CheckOblSaveDocument(document, origin, gridSide);
    warnings.insert(warnings.end(), checks.warnings.begin(), checks.warnings.end());
    if (!checks.refusals.empty()) {
        error = checks.refusals.front();
        if (checks.refusals.size() > 1) error += " (and " + std::to_string(checks.refusals.size() - 1) + " more)";
        return false;
    }
    return true;
}

} // namespace obl::formats
