#include "unit_test.h"
#include "game/ui/scripted_sound.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/sound_convert.h"
#include "importer/src/writers/sound_script_convert.h"
#include "temp_dir.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

obl::json_schema::Registry Schemas()
{
    auto dir = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(dir);
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadSchemaDirectory(*dir, schemas, error));
    return schemas;
}

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// The names a script may use, built the way the converter builds them (lowercase), for invented and installed scripts.
SoundNames Names(std::vector<std::string> samples, std::map<std::string, std::vector<std::string>> sprites)
{
    SoundNames names;
    for (const auto& sample : samples) names.samples.insert(Lower(sample));
    for (auto& [sprite, anims] : sprites) {
        auto& list = names.sprites[Lower(sprite)];
        for (const auto& anim : anims) list.push_back(Lower(anim));
    }
    return names;
}

// An independent reading of the source script for the installation test: whitespace-separated tokens, '/' comments, sections
// closed by End. Lowercased names; ids stay as written.
struct ScriptEntry {
    int group = -1;
    std::vector<std::pair<std::string, float>> sounds;
};
struct SourceScript {
    std::map<std::string, std::map<std::string, ScriptEntry>> sprites;
    std::map<std::string, std::pair<std::string, float>> buildings;
    std::map<std::string, ScriptEntry> opinions;
    std::map<int, int> groups;
};

std::vector<std::string> Tokens(const std::string& text)
{
    std::vector<std::string> tokens;
    size_t at = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            ++at;
        } else if (c == '/') {
            while (at < text.size() && text[at] != '\n') ++at;
        } else {
            const size_t end = text.find_first_of(" \r\n\t", at);
            tokens.push_back(text.substr(at, end == std::string::npos ? end : end - at));
            at = end == std::string::npos ? text.size() : end;
        }
    }
    return tokens;
}

SourceScript ReadSource(const std::string& text)
{
    const auto tokens = Tokens(text);
    SourceScript script;
    size_t at = 0;
    auto entry = [&](ScriptEntry& e) {
        if (tokens[at] == "Group") {
            e.group = std::atoi(tokens[at + 1].c_str());
            at += 2;
        }
        const int count = std::atoi(tokens[at++].c_str());
        for (int i = 0; i < count; ++i, at += 2) e.sounds.emplace_back(Lower(tokens[at]), float(std::atof(tokens[at + 1].c_str())));
    };
    while (at < tokens.size()) {
        const auto command = tokens[at++];
        if (command == "SpriteSounds") {
            auto& sprite = script.sprites[Lower(tokens[at++])];
            while (tokens[at] != "End") entry(sprite[Lower(tokens[at++])]);
        } else if (command == "OpinionSounds") {
            while (tokens[at] != "End") entry(script.opinions[tokens[at++]]);
        } else if (command == "BuildingSounds") {
            for (; tokens[at] != "End"; at += 4)
                script.buildings[Lower(tokens[at])] = {Lower(tokens[at + 1]), float(std::atof(tokens[at + 2].c_str()))};
        } else if (command == "Groups") {
            for (; tokens[at] != "End"; at += 2) script.groups[std::atoi(tokens[at].c_str())] = std::atoi(tokens[at + 1].c_str());
        } else {
            while (tokens[at] != "End") ++at;
        }
        ++at;
    }
    return script;
}

std::string ErrorOf(const SoundNames& names, const std::string& text)
{
    nlohmann::ordered_json document;
    std::string error;
    return ParseSoundScript(text, names, document, error) ? std::string() : error;
}

} // namespace

OBL_TEST_CASE("convert.sound_script_parses_the_grammar")
{
    // Comments, a Group before the count, an opinion in file order and the crowd settings.
    const auto names = Names({"Vomit_1", "splash", "bar_loop", "cheer"}, {{"Gail", {"D_STAND_VOMIT", "D_BOB"}}});
    const std::string text = "// comment line\nCrowdSound MaxNumber 55 MaxVolume 1.0 FadeTime 0.3 End\n"
                             "Groups 0 3 1 2 End\n"
                             "SpriteSounds Gail\n  d_stand_vomit 1 vomit_1 1.0\n  D_BOB Group 1 1 SPLASH 0.8\nEnd\n"
                             "BuildingSounds BeachBar bar_loop 0.5 1 End\n"
                             "OpinionSounds Female_GoodSale Group 2 1 cheer 1 Male_Vomit 0 End\n";
    nlohmann::ordered_json document;
    std::string error;
    OBL_REQUIRE(ParseSoundScript(text, names, document, error));
    OBL_CHECK(document["crowd"]["max_number"] == 55.0);
    OBL_CHECK(document["crowd"]["fade_time"] == 0.3);
    OBL_CHECK(document["groups"]["group_1"]["limit"] == 2);
    OBL_CHECK(document["sprite_sounds"]["gail"]["d_bob"]["group"] == "group_1");
    OBL_CHECK(document["sprite_sounds"]["gail"]["d_bob"]["sounds"][0]["sample"] == "splash");
    OBL_CHECK(document["sprite_sounds"]["gail"]["d_stand_vomit"]["sounds"][0]["sample"] == "vomit_1");
    OBL_CHECK(document["building_sounds"]["beachbar"]["loop"] == true);
    OBL_CHECK(document["events"]["guest_female_purchase_good"]["group"] == "group_2");
    OBL_CHECK(document["events"]["guest_male_vomit"]["sounds"].empty());
    // Group 2 is used but never limited: it is declared with limit 0, which is how it behaves.
    OBL_CHECK(document["groups"]["group_2"]["limit"] == 0);
}

OBL_TEST_CASE("convert.sound_script_refuses_bad_scripts")
{
    const auto names = Names({"hit", "bell"}, {{"Gail", {"D_BOB"}}});
    OBL_CHECK(!ErrorOf(names, "Unrecognized End").empty());
    OBL_CHECK(!ErrorOf(names, "SpriteSounds Shaz End").empty());
    OBL_CHECK(!ErrorOf(names, "SpriteSounds Gail D_NOPE 1 hit 1 End").empty());
    OBL_CHECK(!ErrorOf(names, "SpriteSounds Gail D_BOB 1 missing 1 End").empty());
    OBL_CHECK(!ErrorOf(names, "SpriteSounds Gail D_BOB 0 End").empty());
    OBL_CHECK(!ErrorOf(names, "SpriteSounds Gail D_BOB 1 hit 1 End SpriteSounds Gail D_BOB 1 hit 1 End").empty());
    OBL_CHECK(!ErrorOf(names, "BuildingSounds Beach hit 1.0 1 Beach bell 1.0 0 End").empty());
    OBL_CHECK(!ErrorOf(names, "OpinionSounds Male_Crying 1 hit 1 End").empty());
    OBL_CHECK(!ErrorOf(names, "OpinionSounds Male_Vomit Group 25 1 hit 1 End").empty());
    OBL_CHECK(!ErrorOf(names, "OpinionSounds Male_Vomit 0 Male_Vomit 0 End").empty());
    OBL_CHECK(!ErrorOf(names, "Groups 0 2 0 3 End").empty());
    OBL_CHECK(!ErrorOf(names, "CrowdSound Volume 1 End").empty());
    OBL_CHECK(!ErrorOf(names, "SpriteSounds Gail D_BOB 1 hit").empty());
}

OBL_TEST_CASE("data.convert_sound_script_matches_source")
{
    // The converted document says what an independent reading of the same text says: names, volumes (as floats), groups, and every
    // source keyword bound to the event the game fires for it, position for position.
    const auto script = FindInstalledFile(OBL_GAME_DIR, "Data/Sounds/Sounds.ble");
    const auto index = FindInstalledFile(OBL_GAME_DIR, "Data/Sounds/blsnd.hdr");
    const auto samples = FindInstalledFile(OBL_GAME_DIR, "Data/Sounds/blsnd.spf");
    const auto sprites = FindInstalledFile(OBL_GAME_DIR, "Data/Sprites/BLSprite.hdr");
    if (!script || !index || !samples || !sprites) OBL_SKIP("no installation sound script");
    const obl::test::TempPath tmp_out("obl-sound-script-test-installation");
    const auto& out = tmp_out.path;
    int files = 0;
    std::string error;
    OBL_REQUIRE(ConvertSoundScript(
            *obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(out), Schemas(), files, error));
    OBL_CHECK(files == 1);
    std::ifstream in(out / "sounds" / "sound_script.json", std::ios::binary);
    const auto doc = nlohmann::ordered_json::parse(std::string((std::istreambuf_iterator<char>(in)), {}));

    std::ifstream scriptFile(*script, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(scriptFile)), {});
    for (char& c : text) c = static_cast<char>(static_cast<unsigned char>(c) ^ 0xff);
    const auto source = ReadSource(text);

    auto group = [](int id) { return "group_" + std::to_string(id); };
    auto same = [&](const nlohmann::ordered_json& entry, const ScriptEntry& expected) {
        OBL_CHECK((entry.contains("group") ? entry["group"].get<std::string>() : std::string()) ==
                (expected.group == -1 ? std::string() : group(expected.group)));
        OBL_REQUIRE(entry["sounds"].size() == expected.sounds.size());
        for (size_t k = 0; k < expected.sounds.size(); ++k) {
            OBL_CHECK(entry["sounds"][k]["sample"].get<std::string>() == expected.sounds[k].first);
            OBL_CHECK(float(entry["sounds"][k]["volume"].get<double>()) == expected.sounds[k].second);
        }
    };
    for (const auto& [id, limit] : source.groups) OBL_CHECK(doc["groups"][group(id)]["limit"] == limit);
    OBL_CHECK(doc["sprite_sounds"].size() == source.sprites.size());
    for (const auto& [sprite, anims] : source.sprites) {
        OBL_CHECK(doc["sprite_sounds"][sprite].size() == anims.size());
        for (const auto& [anim, entry] : anims) same(doc["sprite_sounds"][sprite][anim], entry);
    }
    OBL_CHECK(doc["building_sounds"].size() == source.buildings.size());
    for (const auto& [building, sound] : source.buildings) {
        OBL_CHECK(doc["building_sounds"][building]["sample"] == sound.first);
        OBL_CHECK(float(doc["building_sounds"][building]["volume"].get<double>()) == sound.second);
    }
    using namespace obl::scripted_sound::event;
    const std::map<std::string, const char*> events = {{"Male_StinkyToilet", kMaleStench}, {"Male_Vomit", kMaleVomit},
            {"Male_Vandalism", kMaleVandalism}, {"Male_Fight", kMaleFight}, {"Male_Topless", kMaleTopless},
            {"Male_WetTShirt", kMaleWetShirt}, {"Male_BuildingQueueClosed", kMaleQueueClosed}, {"Male_GoodSale", kMalePurchaseGood},
            {"Male_BadSale", kMalePurchaseBad}, {"Female_StinkyToilet", kFemaleStench}, {"Female_Vomit", kFemaleVomit},
            {"Female_Vandalism", kFemaleVandalism}, {"Female_Fight", kFemaleFight}, {"Female_BuildingQueueClosed", kFemaleQueueClosed},
            {"Female_GoodSale", kFemalePurchaseGood}, {"Female_BadSale", kFemalePurchaseBad}};
    OBL_CHECK(doc["events"].size() == source.opinions.size());
    for (const auto& [keyword, entry] : source.opinions) same(doc["events"][events.at(keyword)], entry);
    // The engine dispatched by position before v2, so the file must list the keywords in event order: this proves name dispatch equal.
    std::vector<std::string> order;
    for (const auto& token : Tokens(text))
        if (events.contains(token)) order.push_back(events.at(token));
    size_t position = 0;
    for (const auto& [name, entry] : doc["events"].items()) OBL_CHECK(order[position++] == name);
}
