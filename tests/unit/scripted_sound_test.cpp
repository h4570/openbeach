#include "unit_test.h"
#include "game/ui/scripted_sound.h"
#include "importer/src/readers/sprite_bank.h"
#include "game/ui/sound_bank.h"
#include "importer/src/writers/sound_convert.h"
#include "importer/src/writers/sound_script_convert.h"
#include "formats/schemas.h"
#include "temp_dir.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace {
using namespace obl;
bool CaseEqual(const std::string& a, const char* b)
{
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    if (a.size() != std::strlen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
struct Host : scripted_sound::Host {
    std::vector<void*> allocations;
    std::map<std::string, std::vector<std::string>> sprites{{"Test", {"Idle", "Speak", "Unused"}}};
    const std::vector<std::string>* current = nullptr;
    audio::SoundBank* bank = nullptr;
    std::vector<std::string> samples;
    int acquired = 0, released = 0;
    bool missingSound = false;
    ~Host()
    {
        for (auto* p : allocations) std::free(p);
    }
    void* Allocate(size_t n) override
    {
        auto* p = std::malloc(n ? n : 1);
        std::memset(p, 0xa5, n);
        allocations.push_back(p);
        return p;
    }
    bool AcquireSprite(const char* name, std::vector<std::string>& anims) override
    {
        auto it = std::find_if(sprites.begin(), sprites.end(), [&](const auto& entry) { return CaseEqual(entry.first, name); });
        if (it == sprites.end()) return false;
        current = &it->second;
        anims = *current;
        ++acquired;
        return true;
    }
    void ReleaseSprite() override
    {
        ++released;
        current = nullptr;
    }
    bool HasSound(const char* name) override
    {
        samples.emplace_back(name);
        return !missingSound && (!bank || bank->Contains((std::string(name) + ".wav").c_str()));
    }
};
audio::ScriptedSound Initial()
{
    audio::ScriptedSound s;
    std::memset(&s, 0x5a, sizeof(s));
    s.m_numSprites = s.m_numBuildings = s.m_numEvents = s.m_numGroups = 0;
    s.m_events = nullptr;
    s.m_groupLimits = nullptr;
    s.m_maxVolume = 0.75f;
    s.m_maxNumber = 50;
    s.m_fadeTime = 0.2f;
    return s;
}
constexpr const char* kDocument = R"({"format": "obl.sound_script", "version": 2,
  "crowd": {"max_number": 55, "max_volume": 1.0, "fade_time": 0.3},
  "groups": {"steps": {"limit": 3}, "voices": {"limit": 2}, "ambient": {"limit": 7}},
  "sprite_sounds": {"Test": {"Idle": {"group": "ambient", "sounds": [{"sample": "a", "volume": 0.5}, {"sample": "b", "volume": 1}]},
                             "Speak": {"sounds": [{"sample": "c", "volume": 0.25}]}}},
  "building_sounds": {"JACUZZI": {"sample": "water", "volume": 0.75, "loop": true},
                      "CLUB": {"sample": "music", "volume": 1, "loop": false}},
  "events": {"guest_female_vomit": {"group": "voices", "sounds": [{"sample": "a", "volume": 0.4}, {"sample": "b", "volume": 0.6}]},
             "guest_male_fight": {"sounds": []}, "pack_own_event": {"sounds": [{"sample": "c", "volume": 1}]}}})";
struct RuntimeFixture : Host, scripted_sound::RuntimeHost {
    std::vector<void*> freed;
    std::vector<std::string> played;
    float volume = 0, position[3]{};
    int limit = 0, group = 0, starts = 0, stops = 0, id = 0;
    bool loop = false;
    const char* objectName = "jacuzzi";
    int AnimationCount(const char* name) override
    {
        std::vector<std::string> animations;
        bool ok = AcquireSprite(name, animations);
        OBL_CHECK(ok);
        return static_cast<int>(animations.size());
    }
    void ReleaseSprite() override { Host::ReleaseSprite(); }
    void Free(void* p) override
    {
        auto it = std::find(allocations.begin(), allocations.end(), p);
        OBL_REQUIRE(it != allocations.end());
        freed.push_back(p);
        std::free(p);
        allocations.erase(it);
    }
    void Play(const char* name, float* xyz, float v, int l, int g) override
    {
        played.emplace_back(name);
        volume = v;
        limit = l;
        group = g;
        std::copy(xyz, xyz + 3, position);
        xyz[0] = -999;
    }
    const char* ObjectName(int objectId) override { return objectId == -1 ? nullptr : objectName; }
    void Start(const char* name, int objectId, float v, bool looping) override
    {
        played.emplace_back(name);
        id = objectId;
        volume = v;
        loop = looping;
        ++starts;
    }
    void Stop(int objectId) override
    {
        id = objectId;
        ++stops;
    }
};
} // namespace
OBL_TEST_CASE("scripted_sound.construct_preserves_unused_tables_and_destruct_frees_once")
{
    auto expected = Initial();
    audio::ScriptedSound s;
    std::memset(&s, 0x5a, sizeof(s));
    OBL_CHECK(scripted_sound::Construct(s) == &s);
    OBL_CHECK(std::memcmp(&s, &expected, sizeof(s)) == 0);
    RuntimeFixture h;
    std::string error;
    OBL_REQUIRE(scripted_sound::LoadDocument(s, kDocument, h, error));
    const size_t allocated = h.allocations.size();
    scripted_sound::Destruct(s, h);
    OBL_CHECK(h.allocations.empty());
    OBL_CHECK_EQ(h.freed.size(), allocated);
    OBL_CHECK(s.m_events == nullptr && s.m_groupLimits == nullptr);
    OBL_CHECK_EQ(s.m_numEvents + s.m_numGroups + s.m_numSprites + s.m_numBuildings, 0);
    OBL_CHECK_EQ(h.acquired, 2);
    OBL_CHECK_EQ(h.released, 2);
    scripted_sound::Destruct(s, h);
    OBL_CHECK_EQ(h.freed.size(), allocated);
}
OBL_TEST_CASE("scripted_sound.play_round_robin_group_limits_and_position_copy")
{
    RuntimeFixture h;
    auto s = Initial();
    std::string error;
    OBL_REQUIRE(scripted_sound::LoadDocument(s, kDocument, h, error));
    float xyz[3] = {1, 2, 3};
    OBL_CHECK(!scripted_sound::PlaySpriteSound(s, "missing", 0, nullptr, h));
    OBL_CHECK(!scripted_sound::PlaySpriteSound(s, "TEST", 2, nullptr, h));
    OBL_CHECK(h.played.empty());
    for (int i = 0; i < 5; ++i) {
        OBL_CHECK(scripted_sound::PlaySpriteSound(s, "tEsT", 0, xyz, h));
        OBL_CHECK_EQ(h.played.back(), i % 2 ? "b" : "a");
        OBL_CHECK_EQ(h.volume, i % 2 ? 1.0f : .5f);
        OBL_CHECK_EQ(s.m_sprites[0].m_next[0], (i + 1) % 2);
        OBL_CHECK_EQ(h.limit, 7);
        OBL_CHECK_EQ(h.group, 2); // the third group of the document
        OBL_CHECK_EQ(xyz[0], 1.0f);
        OBL_CHECK_EQ(h.position[1], 2.0f);
        OBL_CHECK_EQ(h.position[2], 3.0f);
    }
    OBL_CHECK(scripted_sound::PlaySpriteSound(s, "Test", 1, xyz, h));
    OBL_CHECK_EQ(h.played.back(), "c");
    OBL_CHECK_EQ(h.limit, -1);
    OBL_CHECK_EQ(h.group, -1);
    OBL_CHECK_EQ(s.m_sprites[0].m_next[1], 0);
    // First matching record wins, even when its animation is empty.
    s.m_sprites[1] = s.m_sprites[0];
    s.m_numSprites = 2;
    s.m_sprites[0].m_counts[0] = 0;
    OBL_CHECK(!scripted_sound::PlaySpriteSound(s, "Test", 0, xyz, h));
}
OBL_TEST_CASE("scripted_sound.building_dispatch_missing_first_match_and_unconditional_stop")
{
    RuntimeFixture h;
    auto s = Initial();
    std::string error;
    OBL_REQUIRE(scripted_sound::LoadDocument(s, kDocument, h, error));
    scripted_sound::StartBuildingSound(s, -1, h);
    OBL_CHECK_EQ(h.starts, 0);
    h.objectName = "missing";
    scripted_sound::StartBuildingSound(s, 42, h);
    OBL_CHECK_EQ(h.starts, 0);
    h.objectName = "JaCuZzI";
    scripted_sound::StartBuildingSound(s, 42, h);
    OBL_CHECK_EQ(h.starts, 1);
    OBL_CHECK_EQ(h.played.back(), "water");
    OBL_CHECK_EQ(h.id, 42);
    OBL_CHECK_EQ(h.volume, .75f);
    OBL_CHECK(h.loop);
    s.m_buildings[1].m_name = s.m_buildings[0].m_name;
    scripted_sound::StartBuildingSound(s, 43, h);
    OBL_CHECK_EQ(h.played.back(), "water");
    h.objectName = "CLUB";
    s.m_buildings[1].m_name = const_cast<char*>("CLUB");
    scripted_sound::StartBuildingSound(s, 44, h);
    OBL_CHECK_EQ(h.played.back(), "music");
    OBL_CHECK(!h.loop);
    scripted_sound::StopBuildingSound(-1, h);
    OBL_CHECK_EQ(h.id, -1);
    OBL_CHECK_EQ(h.stops, 1);
}
OBL_TEST_CASE("scripted_sound.document_builds_groups_and_events_in_order")
{
    Host h;
    auto s = Initial();
    std::string error;
    OBL_REQUIRE(scripted_sound::LoadDocument(s, kDocument, h, error));
    OBL_CHECK_EQ(h.acquired, 1);
    OBL_CHECK_EQ(h.released, 1);
    OBL_CHECK_EQ(s.m_numSprites, 1);
    OBL_CHECK_EQ(s.m_numBuildings, 2);
    OBL_CHECK_EQ(s.m_maxNumber, 55.0f);
    OBL_CHECK_EQ(s.m_fadeTime, .3f);
    OBL_REQUIRE(s.m_numGroups == 3);
    OBL_CHECK_EQ(s.m_groupLimits[0], 3);
    OBL_CHECK_EQ(s.m_groupLimits[2], 7);
    OBL_CHECK_EQ(s.m_sprites[0].m_counts[2], 0);
    OBL_CHECK_EQ(s.m_sprites[0].m_groups[2], -1);
    OBL_CHECK_EQ(s.m_buildings[0].m_loop, 1);
    OBL_CHECK_EQ(s.m_buildings[1].m_loop, 0);
    OBL_REQUIRE(s.m_numEvents == 3);
    const auto* vomit = scripted_sound::FindEvent(s, "guest_female_vomit");
    OBL_REQUIRE(vomit != nullptr);
    OBL_CHECK_EQ(vomit->m_group, 1);
    OBL_CHECK_EQ(vomit->m_count, 2);
    OBL_CHECK_EQ(std::string(vomit->m_sounds[1]), "b");
    OBL_CHECK_EQ(vomit->m_volumes[0], .4f);
    OBL_CHECK_EQ(scripted_sound::FindEvent(s, "guest_male_fight")->m_group, -1);
    // Events are the document's: a pack's own name loads, and a name it does not bind is not found.
    OBL_CHECK(scripted_sound::FindEvent(s, "pack_own_event") != nullptr);
    OBL_CHECK(scripted_sound::FindEvent(s, "guest_male_vomit") == nullptr);
}

OBL_TEST_CASE("scripted_sound.groups_have_no_ceiling")
{
    std::string groups, events;
    for (int i = 0; i < 64; ++i) groups += (i ? ",\"g" : "\"g") + std::to_string(i) + "\": {\"limit\": " + std::to_string(i) + "}";
    const std::string text = R"({"format": "obl.sound_script", "version": 2, "groups": {)" + groups +
            R"(}, "sprite_sounds": {}, "building_sounds": {}, "events": {"e": {"group": "g63", "sounds": []}}})";
    Host h;
    auto s = Initial();
    std::string error;
    OBL_REQUIRE(scripted_sound::LoadDocument(s, text, h, error));
    OBL_CHECK_EQ(s.m_numGroups, 64);
    OBL_CHECK_EQ(s.m_groupLimits[63], 63);
    OBL_CHECK_EQ(s.m_events[0].m_group, 63);
}

OBL_TEST_CASE("scripted_sound.document_matches_names_without_case")
{
    Host h;
    auto s = Initial();
    std::string error;
    OBL_REQUIRE(scripted_sound::LoadDocument(s, R"({"format": "obl.sound_script", "version": 2,
        "sprite_sounds": {"test": {"speak": {"sounds": [{"sample": "c", "volume": 1}]}}},
        "building_sounds": {}, "events": {}})",
            h, error));
    OBL_CHECK_EQ(s.m_numSprites, 1);
    OBL_CHECK_EQ(s.m_sprites[0].m_counts[1], 1);
    OBL_CHECK_EQ(s.m_sprites[0].m_groups[1], -1);
    OBL_CHECK_EQ(s.m_maxNumber, 50.0f); // no crowd key: the defaults stay
}

OBL_TEST_CASE("scripted_sound.document_refuses_bad_input")
{
    const char* const bad[] = {R"({"format": "other", "version": 2})", R"({"format": "obl.sound_script", "version": 1})",
            R"({"format": "obl.sound_script", "version": 2, "sprite_sounds": {"Missing": {}}, "building_sounds": {}, "events": {}})",
            R"({"format": "obl.sound_script", "version": 2, "sprite_sounds": {"Test": {"Missing": {"sounds": [{"sample": "a", "volume": 1}]}}},
                "building_sounds": {}, "events": {}})",
            R"({"format": "obl.sound_script", "version": 2, "sprite_sounds": {"Test": {"Idle": {"sounds": []}}},
                "building_sounds": {}, "events": {}})",
            R"({"format": "obl.sound_script", "version": 2, "sprite_sounds": {"Test": {"Idle": {"group": "none", "sounds": [{"sample": "a", "volume": 1}]}}},
                "building_sounds": {}, "events": {}})",
            R"({"format": "obl.sound_script", "version": 2, "sprite_sounds": {}, "building_sounds": {},
                "events": {"e": {"group": "none", "sounds": []}}})",
            R"({"format": "obl.sound_script", "version": 2, "sprite_sounds": {}, "building_sounds": {}, "events": {)"};
    for (const char* text : bad) {
        Host host;
        auto out = Initial();
        std::string error;
        OBL_CHECK(!scripted_sound::LoadDocument(out, text, host, error));
        OBL_CHECK(!error.empty());
        OBL_CHECK_EQ(host.acquired, host.released);
    }
    Host missing;
    missing.missingSound = true;
    auto out = Initial();
    std::string error;
    OBL_CHECK(!scripted_sound::LoadDocument(out, kDocument, missing, error));
    OBL_CHECK_EQ(error, "sound not found");
    Host full;
    auto crowded = Initial();
    crowded.m_numSprites = 30;
    OBL_CHECK(!scripted_sound::LoadDocument(crowded, kDocument, full, error));
}
OBL_TEST_CASE("data.scripted_sound.installation_sounds_and_sprite_headers")
{
    auto root = std::filesystem::path(OBL_GAME_DIR);
    std::ifstream file(root / "Data/Sounds/Sounds.ble", std::ios::binary);
    std::ifstream headers(root / "Data/Sprites/BLSprite.hdr", std::ios::binary);
    if (!file || !headers) OBL_SKIP("installation Sounds.ble and BLSprite.hdr required");
    // The names the engine's bank holds come from the converted samples of the installation (converter/sounds.md).
    const test::TempPath tmpSamples("obl-scripted-sound-samples");
    int files = 0;
    std::string convertError;
    OBL_REQUIRE(convert::ConvertSamples(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(tmpSamples.path), files, convertError));
    audio::SoundBank bank;
    OBL_REQUIRE(bank.Open(tmpSamples.path / "sounds"));
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(headers), {});
    convert::SpriteBank sprites;
    OBL_REQUIRE(convert::ParseSpriteBank(bytes, sprites, convertError));
    Host host;
    host.sprites.clear();
    host.bank = &bank;
    for (const auto& header : sprites.sprites) {
        std::vector<std::string> anims;
        for (const auto& a : header.anims) anims.emplace_back(a.name);
        host.sprites.emplace(header.name, std::move(anims));
    }
    auto s = Initial();
    std::string error;
    const test::TempPath tmpScript("obl-scripted-sound-script");
    auto schemaDir = formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(schemaDir);
    json_schema::Registry schemas;
    OBL_REQUIRE(formats::LoadSchemaDirectory(*schemaDir, schemas, error));
    OBL_REQUIRE(convert::ConvertSoundScript(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(tmpScript.path), schemas, files, error));
    std::ifstream script(tmpScript.path / "sounds" / "sound_script.json", std::ios::binary);
    const std::string text(std::istreambuf_iterator<char>(script), {});
    bool ok = scripted_sound::LoadDocument(s, text, host, error);
    OBL_CHECK_EQ(error, "");
    OBL_REQUIRE(ok);
    OBL_CHECK_EQ(s.m_numSprites, 17);
    OBL_CHECK_EQ(s.m_numBuildings, 6);
    OBL_CHECK_EQ(s.m_numEvents, 16);
    OBL_CHECK_EQ(s.m_numGroups, 10); // nine limited, one used without a limit
    OBL_CHECK_EQ(s.m_maxNumber, 55.0f);
    OBL_CHECK_EQ(s.m_maxVolume, 1.0f);
    OBL_CHECK_EQ(s.m_fadeTime, .3f);
    OBL_CHECK_EQ(host.acquired, s.m_numSprites);
    OBL_CHECK_EQ(host.released, s.m_numSprites);
    OBL_CHECK_EQ(host.samples.size(), size_t{967});
    int definedAnimations = 0;
    for (int i = 0; i < s.m_numSprites; ++i) {
        const auto& record = s.m_sprites[i];
        auto entry = std::find_if(
                host.sprites.begin(), host.sprites.end(), [&](const auto& item) { return CaseEqual(item.first, record.m_name); });
        OBL_REQUIRE(entry != host.sprites.end());
        for (size_t a = 0; a < entry->second.size(); ++a)
            if (record.m_counts[a] > 0) ++definedAnimations;
    }
    OBL_CHECK_EQ(definedAnimations, 210);
}
