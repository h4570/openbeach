#include "importer/src/writers/guest_set_convert.h"
#include "core/ascii_case.h"
#include "game/objects/guest_sets.h"
#include "importer/src/writers/catalog_writer.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace obl::convert {
namespace {

constexpr uint64_t kMaxGuestBytes = 1u << 20;

// The engine's tables (tables::WorldParams and GuestDef): guest records, the name pools and their text key slots.
constexpr size_t kMaxGuests = 20;
constexpr size_t kMaxMaleNames = 0x8c;
constexpr size_t kMaxFemaleNames = 0x118 - 0x8c;
constexpr size_t kMaxSurnames = 0x370 - 0x118;
constexpr size_t kMaxKeyLength = 0x3f; // GuestDef::m_key[0x40], the NUL included
constexpr size_t kMaxNameLength = 0x7f; // GuestDef::m_Name[0x80]
constexpr size_t kMaxSpriteLength = 0xff; // GuestDef::m_Sprite[0x100]
constexpr size_t kMaxTextKeyLength = 0x3f; // WorldParams::m_names[..][0x40]

using Pair = std::array<float, 2>;

// One guest type as the engine's GuestDef holds it, field by field; the document names them in its own terms.
struct Guest {
    std::string key;
    std::string name; // NAME: a text key, or empty when the names come from the pools
    std::string sprite;
    bool hasGender = false;
    bool female = false;
    int defaultSprite = 0;
    uint8_t colour = 0;
    uint8_t drunkType = 0; // 0 none, 1 tired, 2 flirt (HORNY), 3 fight, 4 vandalise: the engine's codes
    uint8_t nightsMin = 0;
    uint8_t nightsMax = 0;
    float money = 0;
    float litter = 0;
    float attractive = 0;
    float attractivePref = 0;
    float swimEnergy = 0;
    float swimDist = 0;
    float sunburnTime = 0;
    float drunkDecay = 0;
    float flirtDecay = 0;
    float fightDecay = 0;
    float vandaliseDecay = 0;
    float illDecay = 0;
    float startBladder = 0;
    float startFight = 0;
    float startFlirt = 0;
    float startIll = 0;
    float startDrunk = 0;
    float startSunburn = 0;
    float startVandalise = 0;
    std::array<Pair, 13> preferences{};
    std::array<Pair, 7> tolerances{};
    std::array<Pair, 7> opinionEffects{};
    std::array<Pair, 7> opinionStart{};
    Pair sleepTime{};
    Pair wakeTime{};
    std::array<int, 26> max{};
};

struct GuestSetData {
    std::vector<Guest> guests;
    std::vector<std::string> surnames;
    std::vector<std::string> male;
    std::vector<std::string> female;
};

// A keyword of a two-value field and the document name of its pair (the engine's kGuestRanges, same index order).
struct PairField {
    const char* keyword;
    const char* name;
};
// Preferences keep the engine's order: m_Pref[0..12] (ALCOHOL ... SLEEP).
constexpr PairField kPreferenceFields[] = {{"ALCOHOLPREF", "alcohol"}, {"AMUSEMENTPREF", "amusement"}, {"MEALSPREF", "meals"},
        {"PARTYFUNPREF", "party_fun"}, {"SNACKSPREF", "snacks"}, {"SOFTDRINKSPREF", "soft_drinks"}, {"SOUVENIRSPREF", "souvenirs"},
        {"SPORTSFUNPREF", "sports_fun"}, {"SUNBATHEPREF", "sunbathe"}, {"SWIMMINGPREF", "swimming"}, {"WATERFUNPREF", "water_fun"},
        {"STROLLPREF", "stroll"}, {"SLEEPPREF", "sleep"}};
// The seven opinions share one order: beauty, crowding, totty (attractiveness), price, variety, cleanliness, safety.
constexpr PairField kToleranceFields[] = {{"BEAUTYTOL", "beauty"}, {"CROWDINGTOL", "crowding"}, {"TOTTYTOL", "attractiveness"},
        {"PRICETOL", "price"}, {"VARIETYTOL", "variety"}, {"CLEANLINESSTOL", "cleanliness"}, {"SAFETYTOL", "safety"}};
constexpr PairField kEffectFields[] = {{"BEAUTY_EFFECT", "beauty"}, {"CROWDING_EFFECT", "crowding"}, {"TOTTY_EFFECT", "attractiveness"},
        {"PRICE_EFFECT", "price"}, {"VARIETY_EFFECT", "variety"}, {"CLEANLINESS_EFFECT", "cleanliness"}, {"SAFETY_EFFECT", "safety"}};
constexpr PairField kOpinionStartFields[] = {{"BEAUTY_START", "beauty"}, {"CROWDING_START", "crowding"}, {"TOTTY_START", "attractiveness"},
        {"PRICE_START", "price"}, {"VARIETY_START", "variety"}, {"CLEANLINESS_START", "cleanliness"}, {"SAFETY_START", "safety"}};

// A single float field, by the member it fills.
struct FloatField {
    const char* keyword;
    float Guest::* member;
};
constexpr FloatField kFloatFields[] = {{"MONEY", &Guest::money}, {"LITTER", &Guest::litter}, {"ATTRACTIVE", &Guest::attractive},
        {"ATTRACTIVE_PREF", &Guest::attractivePref}, {"SWIM_ENERGY", &Guest::swimEnergy}, {"SWIM_DIST", &Guest::swimDist},
        {"SUNBURN_TIME", &Guest::sunburnTime}, {"DRUNKDECAY", &Guest::drunkDecay}, {"FLIRTDECAY", &Guest::flirtDecay},
        {"FIGHTDECAY", &Guest::fightDecay}, {"VANDALISEDECAY", &Guest::vandaliseDecay}, {"ILL_DECAY", &Guest::illDecay},
        {"START_BLADDER", &Guest::startBladder}, {"START_FIGHT", &Guest::startFight}, {"START_FLIRT", &Guest::startFlirt},
        {"START_ILL", &Guest::startIll}, {"START_DRUNK", &Guest::startDrunk}, {"START_SUNBURN", &Guest::startSunburn},
        {"START_VANDALISE", &Guest::startVandalise}};

// MAX_<keyword> is an integer per action, in the engine's index order (GuestObject's desires).
struct MaxField {
    const char* keyword;
    const char* name;
};
constexpr MaxField kMaxFields[] = {{"ACCOMODATION", "accommodation"}, {"ALCOHOL", "alcohol"}, {"AMUSEMENT", "amusement"},
        {"FIRSTAID", "first_aid"}, {"HEADACHE", "headache"}, {"MEALS", "meals"}, {"PARTYFUN", "party_fun"}, {"SHOWER", "shower"},
        {"SNACKS", "snacks"}, {"SOFTDRINKS", "soft_drinks"}, {"SOUVENIRS", "souvenirs"}, {"SPORTSFUN", "sports_fun"},
        {"SUNBATHE", "sunbathe"}, {"SWIMFUN", "swim_fun"}, {"TOILET", "toilet"}, {"WATERSPORTSFUN", "water_sports_fun"},
        {"STROLL", "stroll"}, {"LEAVE", "leave"}, {"VANDALISE", "vandalise"}, {"FIGHT", "fight"}, {"FLIRT", "flirt"},
        {"UMBRELLAS", "umbrellas"}, {"SUNTAN", "suntan"}, {"HARDSEX", "intimacy"}, {"SLEEP", "sleep"}, {"SCARED", "scared"}};

constexpr const char* kDrunkTypeNames[] = {"none", "tired", "flirt", "fight", "vandalise"};

struct Token {
    std::string text;
    size_t at; // byte offset of its opening quote
};

// Printable keys and sprite names are lowercase content keys (the schema's `key`); text keys are the schema's `text_key`.
std::string Lower(std::string_view text)
{
    std::string lower(text);
    for (char& c : lower) c = ascii::Lower(c);
    return lower;
}

bool IsContentKey(std::string_view text)
{
    if (text.empty() || text.size() > kMaxKeyLength) return false;
    for (char c : text)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

bool IsTextKey(std::string_view text)
{
    if (text.empty() || text.size() > kMaxTextKeyLength) return false;
    for (char c : text)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-' || c == ' '))
            return false;
    return true;
}

// The engine stores a float32 of atof's double; the bits decide whether a member is written (a -0.0 is written).
bool Nonzero(float value)
{
    return std::bit_cast<uint32_t>(value) != 0;
}

// A [low, high] pair as the catalog writes its floats: the shortest decimal of each float32.
nlohmann::ordered_json FloatPair(const Pair& pair)
{
    return nlohmann::ordered_json::array({FloatNumber(pair[0]), FloatNumber(pair[1])});
}

bool Fail(std::string& error, size_t at, const std::string& message)
{
    error = "byte " + std::to_string(at) + ": " + message;
    return false;
}

// The engine's own quoting (Process): every byte outside quotes is ignored, so the file is the sequence of quoted tokens.
bool Tokenize(std::string_view text, std::vector<Token>& tokens, std::string& error)
{
    size_t open = text.find('"');
    while (open != std::string_view::npos) {
        const size_t close = text.find('"', open + 1);
        if (close == std::string_view::npos) return Fail(error, open, "odd number of quotes");
        tokens.push_back({std::string(text.substr(open + 1, close - open - 1)), open});
        open = text.find('"', close + 1);
    }
    return true;
}

// The engine's ScanKeyword: the keyword ends at the first '_' (JETTY and SECURITY keep theirs). False when the token has none,
// then the whole token is the keyword and its argument is the next token.
bool SplitKeyword(const std::string& token, std::string& keyword, std::string& field)
{
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] == '_' && keyword != "JETTY" && keyword != "SECURITY") {
            field = token.substr(i + 1);
            return true;
        }
        keyword.push_back(token[i]);
    }
    return false;
}

Guest* FindGuest(GuestSetData& set, const std::string& key)
{
    for (Guest& guest : set.guests)
        if (guest.key == key) return &guest;
    return nullptr;
}

// The pair a two-value field fills (the engine's kGuestRanges), null when the keyword is not one.
Pair* FindPair(Guest& guest, std::string_view field)
{
    for (size_t i = 0; i < std::size(kPreferenceFields); ++i)
        if (field == kPreferenceFields[i].keyword) return &guest.preferences[i];
    for (size_t i = 0; i < std::size(kToleranceFields); ++i)
        if (field == kToleranceFields[i].keyword) return &guest.tolerances[i];
    for (size_t i = 0; i < std::size(kEffectFields); ++i)
        if (field == kEffectFields[i].keyword) return &guest.opinionEffects[i];
    for (size_t i = 0; i < std::size(kOpinionStartFields); ++i)
        if (field == kOpinionStartFields[i].keyword) return &guest.opinionStart[i];
    if (field == "SLEEPTIME") return &guest.sleepTime;
    if (field == "WAKETIME") return &guest.wakeTime;
    return nullptr;
}

// A float the engine reads with atof and stores as float32; a value that does not fit a float is refused.
bool ParseFloat(const std::string& text, size_t at, float& value, std::string& error)
{
    value = static_cast<float>(std::atof(text.c_str()));
    if (!std::isfinite(value)) return Fail(error, at, "value \"" + text + "\" is not a finite number");
    return true;
}

// One single-value field of a guest record (the engine's AddGuestData). NAMESET is read and ignored, as the engine does.
bool SetField(Guest& guest, std::string_view field, const std::string& value, size_t at, std::string& error)
{
    if (field == "GENDER") {
        if (value != "M" && value != "F") return Fail(error, at, "GENDER is not M or F");
        guest.hasGender = true;
        guest.female = value == "F";
        return true;
    }
    if (field == "DRUNK_TYPE") {
        if (value == "TIRED")
            guest.drunkType = 1;
        else if (value == "HORNY")
            guest.drunkType = 2;
        else if (value == "FIGHT")
            guest.drunkType = 3;
        else if (value == "VANDALISE")
            guest.drunkType = 4;
        else
            return Fail(error, at, "DRUNK_TYPE is not TIRED, HORNY, FIGHT or VANDALISE");
        return true;
    }
    if (field == "DRUNKTYPE") {
        // The engine's raw byte of the same codes; anything else would be a drunk type the game does not have.
        guest.drunkType = static_cast<uint8_t>(std::atoi(value.c_str()));
        if (guest.drunkType > 4) return Fail(error, at, "DRUNKTYPE is not a drunk type 0..4");
        return true;
    }
    if (field == "NAMESET") return true;
    if (field == "NAME") {
        if (value.size() > kMaxNameLength) return Fail(error, at, "NAME is longer than the engine's 127 characters");
        guest.name = value;
        return true;
    }
    if (field == "SPRITE") {
        if (value.size() > kMaxSpriteLength) return Fail(error, at, "SPRITE is longer than the engine's 255 characters");
        guest.sprite = value;
        return true;
    }
    if (field == "DEF_SPRITE") {
        guest.defaultSprite = std::atoi(value.c_str());
        return true;
    }
    if (field == "COLOUR") {
        guest.colour = static_cast<uint8_t>(std::atoi(value.c_str()));
        return true;
    }
    if (field == "NIGHTS_MIN") {
        guest.nightsMin = static_cast<uint8_t>(std::atoi(value.c_str()));
        return true;
    }
    if (field == "NIGHTS_MAX") {
        guest.nightsMax = static_cast<uint8_t>(std::atoi(value.c_str()));
        return true;
    }
    for (const FloatField& entry : kFloatFields) {
        if (field != entry.keyword) continue;
        return ParseFloat(value, at, guest.*entry.member, error);
    }
    for (size_t i = 0; i < std::size(kMaxFields); ++i) {
        if (field.substr(0, 4) != "MAX_" || field.substr(4) != kMaxFields[i].keyword) continue;
        guest.max[i] = std::atoi(value.c_str());
        return true;
    }
    return Fail(error, at, "unrecognized guest field " + std::string(field));
}

// Adds a name to one of the three pools, refused past the engine's room for it.
bool AddName(GuestSetData& set, std::string_view keyword, const std::string& text, size_t at, std::string& error)
{
    if (!IsTextKey(text)) return Fail(error, at, std::string(keyword) + " value " + text + " is not a text key");
    auto& pool = keyword == "MALENAME" ? set.male : keyword == "FEMALENAME" ? set.female : set.surnames;
    const size_t capacity = keyword == "MALENAME" ? kMaxMaleNames : keyword == "FEMALENAME" ? kMaxFemaleNames : kMaxSurnames;
    if (pool.size() == capacity) return Fail(error, at, std::string(keyword) + " pool is full (" + std::to_string(capacity) + " names)");
    pool.push_back(text);
    return true;
}

// Checks what the engine would not load the same way once the file is read: a GENDER for each guest, a content key and sprite
// name (lowercase), no key twice in any case (the catalog's lowercase keys must stay apart).
bool CheckGuests(const GuestSetData& set, std::string& error)
{
    if (set.guests.size() > kMaxGuests) {
        error = "more than 20 guest types";
        return false;
    }
    std::vector<std::string> keys;
    for (const Guest& guest : set.guests) {
        if (!guest.hasGender) {
            error = "guest " + guest.key + " has no GENDER";
            return false;
        }
        const std::string key = Lower(guest.key);
        if (!IsContentKey(key)) {
            error = "guest key " + guest.key + " is not a content key";
            return false;
        }
        for (const std::string& other : keys)
            if (other == key) {
                error = "guest key " + guest.key + " appears twice";
                return false;
            }
        keys.push_back(key);
        if (!IsContentKey(Lower(guest.sprite))) {
            error = "guest " + guest.key + " has the sprite \"" + guest.sprite + "\" which is not a content key";
            return false;
        }
        if (guest.defaultSprite < 0) {
            error = "guest " + guest.key + " has a negative DEF_SPRITE";
            return false;
        }
    }
    return true;
}

// The JSON of one guest type. Scalars always appear; a member of a group that is zero is left out (the document's rule).
nlohmann::ordered_json GuestJson(const Guest& guest)
{
    nlohmann::ordered_json type;
    type["key"] = Lower(guest.key);
    type["name"] = guest.name;
    type["gender"] = guest.female ? "f" : "m";
    type["sprite"] = Lower(guest.sprite);
    type["default_sprite"] = guest.defaultSprite;
    type["colour"] = guest.colour;
    type["drunk_type"] = kDrunkTypeNames[guest.drunkType];
    type["money"] = FloatNumber(guest.money);
    type["litter"] = FloatNumber(guest.litter);
    type["attractive"] = FloatNumber(guest.attractive);
    type["attractive_pref"] = FloatNumber(guest.attractivePref);
    type["swim_energy"] = FloatNumber(guest.swimEnergy);
    type["swim_dist"] = FloatNumber(guest.swimDist);
    type["sunburn_time"] = FloatNumber(guest.sunburnTime);
    type["nights"] = nlohmann::ordered_json::array({guest.nightsMin, guest.nightsMax});
    type["sleep_time"] = FloatPair(guest.sleepTime);
    type["wake_time"] = FloatPair(guest.wakeTime);

    const auto pairs = [](const auto& fields, const auto& values) {
        nlohmann::ordered_json group = nlohmann::ordered_json::object();
        for (size_t i = 0; i < std::size(fields); ++i)
            if (Nonzero(values[i][0]) || Nonzero(values[i][1])) group[fields[i].name] = FloatPair(values[i]);
        return group;
    };
    type["preferences"] = pairs(kPreferenceFields, guest.preferences);
    type["tolerances"] = pairs(kToleranceFields, guest.tolerances);
    type["opinion_effects"] = pairs(kEffectFields, guest.opinionEffects);
    type["opinion_start"] = pairs(kOpinionStartFields, guest.opinionStart);

    nlohmann::ordered_json start = nlohmann::ordered_json::object();
    const std::pair<const char*, float> starts[] = {{"bladder", guest.startBladder}, {"fight", guest.startFight},
            {"flirt", guest.startFlirt}, {"ill", guest.startIll}, {"drunk", guest.startDrunk}, {"sunburn", guest.startSunburn},
            {"vandalise", guest.startVandalise}};
    for (const auto& [name, value] : starts)
        if (Nonzero(value)) start[name] = FloatNumber(value);
    type["start"] = start;

    nlohmann::ordered_json decay = nlohmann::ordered_json::object();
    const std::pair<const char*, float> decays[] = {{"drunk", guest.drunkDecay}, {"flirt", guest.flirtDecay}, {"fight", guest.fightDecay},
            {"vandalise", guest.vandaliseDecay}, {"ill", guest.illDecay}};
    for (const auto& [name, value] : decays)
        if (Nonzero(value)) decay[name] = FloatNumber(value);
    type["decay"] = decay;

    nlohmann::ordered_json max = nlohmann::ordered_json::object();
    for (size_t i = 0; i < std::size(kMaxFields); ++i)
        if (guest.max[i] != 0) max[kMaxFields[i].name] = guest.max[i];
    type["max"] = max;
    return type;
}

} // namespace

bool ParseGuestSet(std::string_view text, nlohmann::ordered_json& document, std::string& error)
{
    std::vector<Token> tokens;
    if (!Tokenize(text, tokens, error)) return false;

    GuestSetData set;
    for (size_t i = 0; i < tokens.size();) {
        const Token& token = tokens[i];
        std::string keyword, field;
        if (!SplitKeyword(token.text, keyword, field)) {
            // Name pools: the keyword stands alone and its argument is the next token.
            if (i + 1 >= tokens.size()) return Fail(error, token.at, "keyword " + keyword + " has no value");
            const std::string& value = tokens[i + 1].text;
            if (keyword != "SURNAME" && keyword != "MALENAME" && keyword != "FEMALENAME")
                return Fail(error, token.at, "unrecognized keyword " + keyword);
            if (!AddName(set, keyword, value, tokens[i + 1].at, error)) return false;
            i += 2;
            continue;
        }
        if (keyword == "GUEST") {
            if (FindGuest(set, field)) return Fail(error, token.at, "guest " + field + " appears twice");
            if (field.size() > kMaxKeyLength) return Fail(error, token.at, "guest key " + field + " is longer than 63 characters");
            Guest guest;
            guest.key = field;
            set.guests.push_back(guest);
            ++i;
            continue;
        }
        Guest* guest = FindGuest(set, keyword);
        if (!guest) return Fail(error, token.at, "field " + field + " of " + keyword + ", which has no GUEST_ line before it");
        if (Pair* pair = FindPair(*guest, field)) {
            // Two values, low and high (the engine's AddGuestFloatRange).
            if (i + 2 >= tokens.size()) return Fail(error, token.at, "field " + field + " needs two values");
            if (!ParseFloat(tokens[i + 1].text, tokens[i + 1].at, (*pair)[0], error)) return false;
            if (!ParseFloat(tokens[i + 2].text, tokens[i + 2].at, (*pair)[1], error)) return false;
            i += 3;
            continue;
        }
        if (i + 1 >= tokens.size()) return Fail(error, token.at, "field " + field + " has no value");
        if (!SetField(*guest, field, tokens[i + 1].text, tokens[i + 1].at, error)) return false;
        i += 2;
    }

    if (!CheckGuests(set, error)) return false;
    nlohmann::ordered_json guests = nlohmann::ordered_json::array();
    for (const Guest& guest : set.guests) guests.push_back(GuestJson(guest));
    nlohmann::ordered_json names;
    names["surnames"] = set.surnames;
    names["male"] = set.male;
    names["female"] = set.female;

    document = nlohmann::ordered_json::object();
    document["format"] = "obl.guest_set";
    document["version"] = 2;
    document["types"] = guests;
    document["names"] = names;
    return true;
}

bool ConvertGuestSets(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error)
{
    files = 0;
    try {
        for (const guest_sets::Set& set : guest_sets::kSets) {
            const auto path = FindFile(source, std::string("Data/Script/") + set.stem + ".ble");
            if (!path) {
                error = std::string("missing installation Data/Script/") + set.stem + ".ble";
                return false;
            }
            std::vector<uint8_t> bytes;
            if (!ReadBoundedFile(source, *path, kMaxGuestBytes, bytes, error)) return false;
            std::string text(bytes.size(), '\0');
            for (size_t i = 0; i < bytes.size(); ++i) text[i] = static_cast<char>(bytes[i] ^ 0xFF);

            nlohmann::ordered_json document;
            if (!ParseGuestSet(text, document, error)) {
                error = std::string(set.stem) + ".ble: " + error;
                return false;
            }
            if (!formats::ValidateDocument(schemas, "obl.guest_set", document, error)) return false;
            if (!out.WriteText("guests/" + std::string(set.name) + ".json", document.dump(2) + "\n", error)) return false;
            ++files;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
