#include "game/tables/guest_set_load.h"
#include "catalog_schemas.h"
#include "core/json_schema.h"
#include "core/pack_path.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace obl::guest_set {
namespace {
using nlohmann::json;
using tables::GuestDef;
using tables::WorldParams;

// The engine's pools in WorldParams::m_names (AddNameData): male names from 0, female names from 0x8c, surnames from 0x118.
constexpr size_t kMaleBase = 0, kFemaleBase = 0x8c, kSurnameBase = 0x118, kNameEnd = 0x370;
constexpr size_t kMaxTypes = WorldParams::kMaxGuests;
constexpr size_t kTextKeyCapacity = 0x40; // WorldParams::m_names[..][0x40]

// Index order of the engine's two-value arrays (GuestDef::m_Pref, the seven opinions) and of GuestDef::m_Max (MAX_<name>).
constexpr const char* kPreferenceNames[] = {"alcohol", "amusement", "meals", "party_fun", "snacks", "soft_drinks", "souvenirs",
        "sports_fun", "sunbathe", "swimming", "water_fun", "stroll", "sleep"};
constexpr const char* kOpinionNames[] = {"beauty", "crowding", "attractiveness", "price", "variety", "cleanliness", "safety"};
constexpr const char* kMaxNames[] = {"accommodation", "alcohol", "amusement", "first_aid", "headache", "meals", "party_fun", "shower",
        "snacks", "soft_drinks", "souvenirs", "sports_fun", "sunbathe", "swim_fun", "toilet", "water_sports_fun", "stroll", "leave",
        "vandalise", "fight", "flirt", "umbrellas", "suntan", "intimacy", "sleep", "scared"};
constexpr const char* kDrunkTypeNames[] = {"none", "tired", "flirt", "fight", "vandalise"};

json Read(const std::filesystem::path& path, const json_schema::Registry& schemas)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    json doc = json::parse(file, nullptr, false);
    if (doc.is_discarded()) throw std::runtime_error(path.string() + ": invalid JSON");
    const auto errors = schemas.Validate("obl.guest_set", doc);
    if (!errors.empty()) throw std::runtime_error(path.string() + ": " + errors.front());
    return doc;
}

// The readers copy a guest's text into the engine's fixed buffers (the ferry's keyword, for one), so the file's text must fit them.
constexpr size_t kGuestKeyCapacity = 0x40, kGuestNameCapacity = 0x80, kGuestSpriteCapacity = 0x100; // the NUL included

void Text(std::string& out, const std::string& text, size_t capacity, const std::string& what)
{
    if (text.size() >= capacity) throw std::runtime_error(what + ": engine string capacity exceeded");
    out = text;
}

void CopyText(char* out, size_t capacity, const std::string& text, const std::string& what)
{
    if (text.size() >= capacity) throw std::runtime_error(what + ": engine string capacity exceeded");
    std::memcpy(out, text.c_str(), text.size() + 1);
}

float Float(const json& value)
{
    return static_cast<float>(value.get<double>());
}

void Pair(std::array<float, 2>& out, const json& value)
{
    out[0] = Float(value.at(0));
    out[1] = Float(value.at(1));
}

void Scalar(const json& group, const char* name, float& out)
{
    if (group.contains(name)) out = Float(group[name]);
}

// A group of two-value members, placed by the engine's index order. A missing group or member is 0 (the record starts zeroed).
// The record's lists are sized to the engine's counts when the record is made, so `out` has one entry per name.
template<size_t N> void Pairs(std::vector<std::array<float, 2>>& out, const json& group, const char* const (&names)[N])
{
    if (!group.is_object()) return;
    for (size_t i = 0; i < N; ++i)
        if (group.contains(names[i])) Pair(out[i], group[names[i]]);
}

void Guest(GuestDef& d, int index, const json& type)
{
    d.m_id = index;
    Text(d.m_key, type["key"].get<std::string>(), kGuestKeyCapacity, "guest key");
    Text(d.m_Name, type["name"].get<std::string>(), kGuestNameCapacity, "guest name");
    Text(d.m_Sprite, type["sprite"].get<std::string>(), kGuestSpriteCapacity, "guest sprite");
    d.m_Gender = type["gender"] == "f" ? 1 : 0;
    d.m_DefSprite = type.value("default_sprite", 0);
    d.m_Colour = static_cast<uint8_t>(type.value("colour", 0));
    const std::string drunk = type.value("drunk_type", std::string("none"));
    for (uint8_t code = 0; code < std::size(kDrunkTypeNames); ++code)
        if (drunk == kDrunkTypeNames[code]) d.m_DrunkType = code;
    Scalar(type, "money", d.m_Money);
    Scalar(type, "litter", d.m_Litter);
    Scalar(type, "attractive", d.m_Attractive);
    Scalar(type, "attractive_pref", d.m_AttractivePref);
    Scalar(type, "swim_energy", d.m_SwimEnergy);
    Scalar(type, "swim_dist", d.m_SwimDist);
    Scalar(type, "sunburn_time", d.m_SunburnTime);
    if (type.contains("nights")) {
        d.m_NightsMin = static_cast<uint8_t>(type["nights"][0].get<int>());
        d.m_NightsMax = static_cast<uint8_t>(type["nights"][1].get<int>());
    }
    if (type.contains("sleep_time")) Pair(d.m_SleepTime, type["sleep_time"]);
    if (type.contains("wake_time")) Pair(d.m_WakeTime, type["wake_time"]);
    Pairs(d.m_Pref, type.value("preferences", json::object()), kPreferenceNames);
    Pairs(d.m_OpinionTol, type.value("tolerances", json::object()), kOpinionNames);
    Pairs(d.m_OpinionEffect, type.value("opinion_effects", json::object()), kOpinionNames);
    Pairs(d.m_OpinionStart, type.value("opinion_start", json::object()), kOpinionNames);

    const json start = type.value("start", json::object());
    Scalar(start, "bladder", d.m_StartBladder);
    Scalar(start, "fight", d.m_StartFight);
    Scalar(start, "flirt", d.m_StartFlirt);
    Scalar(start, "ill", d.m_StartIll);
    Scalar(start, "drunk", d.m_StartDrunk);
    Scalar(start, "sunburn", d.m_StartSunburn);
    Scalar(start, "vandalise", d.m_StartVandalise);

    const json decay = type.value("decay", json::object());
    Scalar(decay, "drunk", d.m_DrunkDecay);
    Scalar(decay, "flirt", d.m_FlirtDecay);
    Scalar(decay, "fight", d.m_FightDecay);
    Scalar(decay, "vandalise", d.m_VandaliseDecay);
    Scalar(decay, "ill", d.m_IllDecay);

    const json max = type.value("max", json::object());
    for (size_t i = 0; i < std::size(kMaxNames); ++i)
        if (max.contains(kMaxNames[i])) d.m_Max[i] = max[kMaxNames[i]].get<int>();
}

// Name pools: the text keys of one pool, from `base`, and the pool's count.
void Pool(WorldParams& p, size_t base, size_t capacity, const json& names, int& count, const std::string& what)
{
    if (names.size() > capacity) throw std::runtime_error(what + ": engine name pool capacity exceeded");
    for (size_t i = 0; i < names.size(); ++i) CopyText(p.m_names[base + i], kTextKeyCapacity, names[i].get<std::string>(), what);
    count = static_cast<int>(names.size());
}

// Fills `p` (zeroed) from the document; every check the file needs beyond the schema is here.
void Fill(WorldParams& p, const json& document)
{
    const json& types = document["types"];
    if (types.size() > kMaxTypes) throw std::runtime_error("more than 20 guest types");
    std::set<std::string> keys;
    p.m_guests.resize(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        const json& type = types[i];
        if (!keys.insert(type["key"].get<std::string>()).second) throw std::runtime_error("guest key appears twice");
        Guest(p.m_guests[i], static_cast<int>(i), type);
    }
    p.m_numGuests = static_cast<int>(types.size());

    const json& names = document["names"];
    Pool(p, kMaleBase, kFemaleBase - kMaleBase, names["male"], p.m_nameCounts[0], "male names");
    Pool(p, kFemaleBase, kSurnameBase - kFemaleBase, names["female"], p.m_nameCounts[1], "female names");
    Pool(p, kSurnameBase, kNameEnd - kSurnameBase, names["surnames"], p.m_nameCounts[2], "surnames");
}

} // namespace

bool LoadPack(const std::filesystem::path& pack, const char* set, WorldParams& out, std::string& error)
{
    error.clear();
    const auto file = pack_path::Resolve(pack, "guests/" + std::string(set) + ".json");
    try {
        json_schema::Registry schemas;
        std::string schemaError;
        for (const char* schema : catalog::kSchemas)
            if (!schemas.Add(json::parse(schema), schemaError)) throw std::runtime_error(schemaError);
        const json document = Read(file, schemas);
        // Heap-allocated: a WorldParams is far too large for the stack. Value-initialization zeroes it, like the constructor's reset.
        auto scratch = std::make_unique<WorldParams>();
        Fill(*scratch, document);
        // Only the guest part is replaced: the catalog loaded before the guests stays as it is.
        out.m_numGuests = scratch->m_numGuests;
        out.m_guests = scratch->m_guests;
        std::memcpy(out.m_names, scratch->m_names, sizeof(out.m_names));
        std::memcpy(out.m_nameCounts, scratch->m_nameCounts, sizeof(out.m_nameCounts));
        return true;
    } catch (const std::exception& failure) {
        error = file.string() + ": " + failure.what();
        return false;
    }
}

} // namespace obl::guest_set
