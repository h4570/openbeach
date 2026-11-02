#include "importer/src/writers/sound_script_convert.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/sound_convert.h"
#include "importer/src/readers/sprite_bank.h"
#include "core/ascii_case.h"
#include "game/ui/scripted_sound.h"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
namespace obl::convert {
namespace {

constexpr uint64_t kMaxScriptBytes = 1u << 20;
constexpr uint64_t kMaxSpriteHeaderBytes = 64u << 20;
constexpr size_t kMaxToken = 51; // the engine's token buffer (ReadToken)

// The engine's record limits; group ids are the source grammar's own range.
constexpr int kMaxSprites = 30;
constexpr int kMaxBuildings = 20;
constexpr int kMaxGroups = 20;

// The one place that knows the source script's opinion keywords: each names the event the game fires for it.
struct KeywordEvent {
    std::string_view keyword;
    const char* event;
};
constexpr KeywordEvent kKeywordEvents[] = {{"Male_StinkyToilet", scripted_sound::event::kMaleStench},
        {"Male_Vomit", scripted_sound::event::kMaleVomit}, {"Male_Vandalism", scripted_sound::event::kMaleVandalism},
        {"Male_Fight", scripted_sound::event::kMaleFight}, {"Male_Topless", scripted_sound::event::kMaleTopless},
        {"Male_WetTShirt", scripted_sound::event::kMaleWetShirt}, {"Male_BuildingQueueClosed", scripted_sound::event::kMaleQueueClosed},
        {"Male_GoodSale", scripted_sound::event::kMalePurchaseGood}, {"Male_BadSale", scripted_sound::event::kMalePurchaseBad},
        {"Female_StinkyToilet", scripted_sound::event::kFemaleStench}, {"Female_Vomit", scripted_sound::event::kFemaleVomit},
        {"Female_Vandalism", scripted_sound::event::kFemaleVandalism}, {"Female_Fight", scripted_sound::event::kFemaleFight},
        {"Female_BuildingQueueClosed", scripted_sound::event::kFemaleQueueClosed},
        {"Female_GoodSale", scripted_sound::event::kFemalePurchaseGood}, {"Female_BadSale", scripted_sound::event::kFemalePurchaseBad}};

std::string GroupName(int id)
{
    return "group_" + std::to_string(id);
}

std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) c = ascii::Lower(c);
    return out;
}

bool Space(char c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

// The source script's reader: whitespace and comments (a '/' at a token boundary runs to the line end) are
// skipped, ';' is a token of its own, a token ends at whitespace, ';' or NUL.
size_t SkipComments(std::string_view text, size_t at)
{
    auto skipSpace = [&](size_t n) {
        while (n < text.size() && Space(text[n])) ++n;
        return n;
    };
    size_t n = skipSpace(at);
    while (n < text.size() && text[n] == '/') {
        while (n < text.size() && text[n] != '\n') ++n;
        n = skipSpace(n);
    }
    return n;
}

struct Cursor {
    std::string_view text;
    size_t at = 0;

    // The next token, or false at the end of the text or on an oversized token.
    bool Read(std::string& token)
    {
        at = SkipComments(text, at);
        token.clear();
        if (at == text.size() || text[at] == '\0') return false;
        if (text[at] == ';') {
            token = ";";
            ++at;
            return true;
        }
        while (at < text.size()) {
            const char ch = text[at];
            if (Space(ch) || ch == ';' || ch == '\0') break;
            token += ch;
            ++at;
        }
        return !token.empty() && token.size() <= kMaxToken;
    }
};

// The source script's grammar, building the document as it goes. Errors are exceptions carrying the
// reason; the caller adds the byte offset.
class Parser {
public:
    Parser(Cursor& cursor, const SoundNames& names, nlohmann::ordered_json& document) : c(cursor), names(names), doc(document)
    {
        doc["crowd"] = nlohmann::ordered_json::object();
        doc["groups"] = nlohmann::ordered_json::object();
        doc["sprite_sounds"] = nlohmann::ordered_json::object();
        doc["building_sounds"] = nlohmann::ordered_json::object();
        doc["events"] = nlohmann::ordered_json::object();
    }

    void Load()
    {
        while ((c.at = SkipComments(c.text, c.at)) < c.text.size()) {
            const auto t = Token();
            if (t == "SpriteSounds")
                Sprite();
            else if (t == "BuildingSounds")
                Building();
            else if (t == "CrowdSound")
                Crowd();
            else if (t == "OpinionSounds")
                Opinion();
            else if (t == "Groups")
                Groups();
            else
                throw std::runtime_error("invalid Sounds.ble command " + t);
        }
        if (doc["crowd"].empty()) doc.erase("crowd");
        // A group the script uses but never limits has limit 0 (its samples do not play); v2 declares every group it references.
        for (const int id : usedGroups)
            if (!doc["groups"].contains(GroupName(id))) doc["groups"][GroupName(id)] = {{"limit", 0}};
    }

private:
    std::string Token()
    {
        std::string t;
        if (!c.Read(t)) throw std::runtime_error("missing or oversized token");
        return t;
    }
    int Int() { return std::atoi(Token().c_str()); }
    double Number() { return std::atof(Token().c_str()); }
    static void Check(bool valid, const char* reason)
    {
        if (!valid) throw std::runtime_error(reason);
    }
    // A sample's name and volume pairs; the sample must be in the bank.
    nlohmann::ordered_json Samples(int count)
    {
        auto sounds = nlohmann::ordered_json::array();
        for (int i = 0; i < count; ++i) {
            const auto sample = Lower(Token());
            Check(names.samples.contains(sample), "sample not found");
            const double volume = Number();
            sounds.push_back({{"sample", sample}, {"volume", volume}});
        }
        return sounds;
    }
    // "Group n" before a count; -1 is none and stays out of the document.
    void OptionalGroup(std::string& t, nlohmann::ordered_json& entry)
    {
        if (t != "Group") return;
        const int group = Int();
        Check(group >= 0 && group < kMaxGroups, "invalid sound group");
        entry["group"] = GroupName(group);
        usedGroups.insert(group);
        t = Token();
    }
    void Sprite()
    {
        Check(++spriteSections <= kMaxSprites, "too many sprites");
        const auto name = Lower(Token());
        const auto found = names.sprites.find(name);
        Check(found != names.sprites.end(), "sprite not found");
        Check(!doc["sprite_sounds"].contains(name), "sprite listed twice");
        auto anims = nlohmann::ordered_json::object();
        for (auto t = Token(); t != "End"; t = Token()) {
            const auto anim = Lower(t);
            Check(std::find(found->second.begin(), found->second.end(), anim) != found->second.end(), "animation not found");
            Check(!anims.contains(anim), "animation listed twice");
            auto entry = nlohmann::ordered_json::object();
            auto count = Token();
            OptionalGroup(count, entry);
            const int n = std::atoi(count.c_str());
            Check(n > 0, "zero sound count");
            entry["sounds"] = Samples(n);
            anims[anim] = std::move(entry);
        }
        doc["sprite_sounds"][name] = std::move(anims);
    }
    void Building()
    {
        for (auto t = Token(); t != "End"; t = Token()) {
            Check(++buildingCount <= kMaxBuildings, "too many buildings");
            const auto name = Lower(t);
            Check(!doc["building_sounds"].contains(name), "building listed twice");
            const auto sample = Lower(Token());
            Check(names.samples.contains(sample), "building sound not found");
            const double volume = Number();
            const bool loop = Int() != 0;
            doc["building_sounds"][name] = {{"sample", sample}, {"volume", volume}, {"loop", loop}};
        }
    }
    void Crowd()
    {
        // The engine compares the command names without regard to case (ascii::EqualsNoCase).
        for (auto t = Token(); t != "End"; t = Token()) {
            const auto command = Lower(t);
            if (command == "maxnumber")
                doc["crowd"]["max_number"] = Number();
            else if (command == "maxvolume")
                doc["crowd"]["max_volume"] = Number();
            else if (command == "fadetime")
                doc["crowd"]["fade_time"] = Number();
            else
                Check(false, "unrecognized CrowdSound command");
        }
    }
    void Opinion()
    {
        for (auto t = Token(); t != "End"; t = Token()) {
            const auto found = std::find_if(std::begin(kKeywordEvents), std::end(kKeywordEvents),
                    [&](const KeywordEvent& k) { return ascii::EqualsNoCase(k.keyword, t); });
            Check(found != std::end(kKeywordEvents), "unrecognized opinion");
            Check(!doc["events"].contains(found->event), "opinion listed twice");
            auto entry = nlohmann::ordered_json::object();
            auto count = Token();
            OptionalGroup(count, entry);
            entry["sounds"] = Samples(std::atoi(count.c_str()));
            doc["events"][found->event] = std::move(entry);
        }
    }
    void Groups()
    {
        for (auto t = Token(); t != "End"; t = Token()) {
            const int id = std::atoi(t.c_str());
            Check(id >= 0 && id < kMaxGroups, "invalid group id");
            Check(!doc["groups"].contains(GroupName(id)), "group listed twice");
            doc["groups"][GroupName(id)] = {{"limit", Int()}};
        }
    }

    Cursor& c;
    const SoundNames& names;
    nlohmann::ordered_json& doc;
    int spriteSections = 0;
    int buildingCount = 0;
    std::set<int> usedGroups;
};

} // namespace

bool ParseSoundScript(std::string_view text, const SoundNames& names, nlohmann::ordered_json& document, std::string& error)
{
    document = {{"format", "obl.sound_script"}, {"version", 2}};
    Cursor cursor{text};
    Parser parser(cursor, names, document);
    try {
        parser.Load();
    } catch (const std::exception& e) {
        error = std::string(e.what()) + " at byte " + std::to_string(cursor.at);
        return false;
    }
    return true;
}

bool ConvertSoundScript(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error)
{
    files = 0;
    try {
        const auto script = FindFile(source, "Data/Sounds/Sounds.ble"), header = FindFile(source, "Data/Sounds/blsnd.hdr"),
                   samples = FindFile(source, "Data/Sounds/blsnd.spf"), sprites = FindFile(source, "Data/Sprites/BLSprite.hdr");
        if (!script || !header || !samples || !sprites) {
            error = "missing installation Data/Sounds/Sounds.ble, blsnd.hdr or blsnd.spf, or Data/Sprites/BLSprite.hdr";
            return false;
        }
        std::vector<uint8_t> bytes;
        if (!ReadBoundedFile(source, *script, kMaxScriptBytes, bytes, error)) return false;
        std::string text(bytes.size(), '\0');
        for (size_t i = 0; i < bytes.size(); ++i) text[i] = static_cast<char>(bytes[i] ^ 0xFF);

        std::vector<uint8_t> index;
        if (!ReadBoundedFile(source, *header, 1u << 20, index, error)) return false;
        const auto samplesSize = source.Size(*samples);
        if (!samplesSize) {
            error = "cannot read " + source.Describe(*samples);
            return false;
        }
        std::vector<SoundSample> entries;
        if (!ParseSampleIndex(index, *samplesSize, entries, error)) {
            error = "blsnd.hdr: " + error;
            return false;
        }
        SoundNames names;
        for (const auto& entry : entries) names.samples.insert(entry.name);

        std::vector<uint8_t> spriteHeader;
        if (!ReadBoundedFile(source, *sprites, kMaxSpriteHeaderBytes, spriteHeader, error)) return false;
        SpriteBank bank;
        if (!ParseSpriteBank(spriteHeader, bank, error)) {
            error = "BLSprite.hdr: " + error;
            return false;
        }
        for (const auto& sprite : bank.sprites) {
            auto& anims = names.sprites[Lower(sprite.name)];
            for (const auto& anim : sprite.anims) anims.push_back(Lower(anim.name));
        }

        nlohmann::ordered_json document;
        if (!ParseSoundScript(text, names, document, error)) {
            error = "Sounds.ble: " + error;
            return false;
        }
        if (!formats::ValidateDocument(schemas, "obl.sound_script", document, error)) return false;
        if (!out.WriteText("sounds/sound_script.json", document.dump(2) + "\n", error)) return false;
        files = 1;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
