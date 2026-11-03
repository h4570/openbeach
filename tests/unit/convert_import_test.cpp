// The whole import (importer/src/import.cpp) on a synthetic copy of the game: every step's input is our own bytes, built
// here and with the shared fixtures (synthetic_exe.h, synthetic_footprint_exe.h, synthetic_iso.h). The executable is
// known through ImportOptions::knownExes, so the copy imports end to end without the user's data.

#include "unit_test.h"

#include "formats/map_pack.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "importer/src/source/cd_source.h"
#include "importer/src/writers/font_convert.h"
#include "importer/src/writers/game_parameters_convert.h"
#include "importer/src/writers/guest_set_convert.h"
#include "importer/src/writers/gui_layout_convert.h"
#include "importer/src/writers/image_convert.h"
#include "importer/src/writers/media_convert.h"
#include "importer/src/writers/sound_convert.h"
#include "importer/src/writers/sound_script_convert.h"
#include "importer/src/writers/sprite_convert.h"
#include "importer/src/writers/text_convert.h"
#include "importer/web/web_source.h"
#include "openbeach/importer/import.h"
#include "openbeach/importer/spool.h"
#include "synthetic_cabinet.h"
#include "synthetic_exe.h"
#include "synthetic_footprint_exe.h"
#include "synthetic_iso.h"
#include "synthetic_map.h"
#include "synthetic_sprites.h"

#include "core/json_schema.h"
#include "core/sha256.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace obl::convert;

namespace {

std::vector<uint8_t> Bytes(std::string_view text)
{
    return {text.begin(), text.end()};
}

// The installed form of a script: every byte XOR 0xFF.
std::vector<uint8_t> Encoded(std::string_view text)
{
    std::vector<uint8_t> bytes = Bytes(text);
    for (uint8_t& b : bytes) b ^= 0xFF;
    return bytes;
}

// The executable: the footprint assignment (catalog), then a section with the script vocabulary and a credits table
// (a title row, then rows numbered from 0, then a row that breaks the shape).
std::vector<uint8_t> GameExe()
{
    obl::test::FootprintExe exe;
    constexpr uint32_t kRva = 0x100000; // after the footprint data
    std::vector<uint8_t> data = obl::test::ScriptTableData(136, 108, obl::test::SyntheticExe::kImageBase + kRva);
    data.resize((data.size() + 0xFF) & ~size_t{0xFF}, 0);
    const uint32_t credits[][5] = {{2, 999, 0, 1, 0}, {0, 0, 0xFF0000, 0, 0}, {1, 1, 0x123456, 1, 1}, {0xFFFF, 0, 0, 0, 0}};
    for (const auto& row : credits)
        for (uint32_t v : row) {
            data.resize(data.size() + 4);
            obl::test::PutU32(data, data.size() - 4, v);
        }
    data.resize(data.size() + 0x100, 0);
    exe.AddSection(".rdata", kRva, data, obl::test::SyntheticExe::kDataFlags);
    return exe.bytes;
}

// The sprite archive's build stamp of the European data revision (IdentifyData).
constexpr std::string_view kEuStamp = "2002:08:20 14:19:21";

// A 2 x 2 TGA: color-mapped 8-bit with a 24-bit palette of two entries.
std::vector<uint8_t> Tga()
{
    std::vector<uint8_t> b = {0, 1, 1, 0, 0, 2, 0, 24, 0, 0, 0, 0, 2, 0, 2, 0, 8, 0};
    b.insert(b.end(), {30, 20, 10, 60, 50, 40});
    b.insert(b.end(), {0, 1, 1, 0});
    return b;
}

// A 2 x 2 uncompressed 8-bit palette TIFF holding `pixels`.
std::vector<uint8_t> Tiff(const uint8_t (&pixels)[4])
{
    const uint32_t tags[] = {256, 257, 258, 259, 262, 273, 277, 278, 279};
    const uint32_t values[] = {2, 2, 8, 1, 3, 122, 1, 2, 4};
    std::vector<uint8_t> b(122);
    obl::test::PutU16(b, 0, 0x4949);
    obl::test::PutU16(b, 2, 42);
    obl::test::PutU32(b, 4, 8);
    obl::test::PutU16(b, 8, 9);
    for (size_t i = 0; i < 9; ++i) {
        const size_t e = 10 + i * 12;
        obl::test::PutU16(b, e, static_cast<uint16_t>(tags[i]));
        obl::test::PutU16(b, e + 2, 4);
        obl::test::PutU32(b, e + 4, 1);
        obl::test::PutU32(b, e + 8, values[i]);
    }
    b.insert(b.end(), std::begin(pixels), std::end(pixels));
    return b;
}

// A UTF-16 text table in its installed form: a byte-order mark and `"key" "value"` lines, every byte XOR 0xFF.
std::vector<uint8_t> TextTable(std::string_view lines)
{
    std::vector<uint8_t> bytes = {0xFF ^ 0xFF, 0xFE ^ 0xFF};
    for (char c : lines) {
        bytes.push_back(static_cast<uint8_t>(c) ^ 0xFF);
        bytes.push_back(0xFF);
    }
    return bytes;
}

// One 40-byte record of blsnd.hdr: offset, size, then the NUL-padded name.
void AddSample(std::vector<uint8_t>& index, std::vector<uint8_t>& samples, std::string_view name)
{
    std::vector<uint8_t> wave(64, 0);
    std::memcpy(wave.data(), "RIFF", 4);
    obl::test::PutU32(wave, 4, static_cast<uint32_t>(wave.size() - 8));
    std::memcpy(wave.data() + 8, "WAVE", 4);
    std::vector<uint8_t> record(40, 0);
    obl::test::PutU32(record, 0, static_cast<uint32_t>(samples.size()));
    obl::test::PutU32(record, 4, static_cast<uint32_t>(wave.size()));
    std::memcpy(record.data() + 8, name.data(), name.size());
    index.insert(index.end(), record.begin(), record.end());
    samples.insert(samples.end(), wave.begin(), wave.end());
}

// Files by installation path.
struct Files : std::map<std::string, std::vector<uint8_t>> {
    void Add(const std::string& path, std::vector<uint8_t> bytes) { (*this)[path] = std::move(bytes); }
};

// An installed copy with every file the import reads: one island under all 24 scenarios, the catalog's 48 footprint
// buildings, one sample, one sprite archive, one font and the English text table.
Files Installation(const std::vector<uint8_t>& exe)
{
    Files source;
    source.Add("BeachLife.exe", exe);
    source.Add("BeachLife.ini", Bytes("[Language]\r\nLanguage=0\r\n"));
    source.Add("Data/Script/GameParameters.pis", Bytes("'iDAY_LENGTH_IN_SECONDS' 1200\r\n"));
    source.Add("Data/Script/Object/Hut.ble", Encoded("// a hut\r\n"));

    std::string buildings = "\"COMFAM_BEACH\" \"Beach\"\n\"HOURS_DAY\" \"8\" \"20\"\n";
    for (size_t i = 0; i < obl::test::kFootprintCount; ++i) {
        const std::string key = obl::test::KeyName(i);
        buildings += "\"BUILD_" + key + "\"";
        const std::string width = std::to_string(obl::test::FootprintWidth(i)), height = std::to_string(obl::test::FootprintHeight(i));
        for (const auto& [name, value] : {std::pair<const char*, std::string>{"NAME", "Hut"}, {"SPRITE", "HUT"}, {"GRIDWIDTH", width},
                     {"GRIDHEIGHT", height}, {"BUILDON", "LAND"}, {"CATEGORY", "Other"}, {"COST2BUILD", "50"}})
            buildings += " \"" + key + "_" + name + "\" \"" + value + "\"";
        buildings += "\n";
    }
    source.Add("Data/Script/BuildingList.ble", Encoded(buildings));
    for (const char* guests : {"guests01", "guests02", "guests03", "guests04", "guests05", "guests06", "guests07", "guests08", "guests09",
                 "guests10", "guests11", "guests12", "sandboxe_guests", "sandboxm_guests", "sandboxh_guests"})
        source.Add(
                std::string("Data/Script/") + guests + ".ble", Encoded("\"GUEST_Lad\" \"Lad_GENDER\" \"M\" \"Lad_SPRITE\" \"Walker\"\n"));

    source.Add("Data/Isle/Isle.biz", obl::test::Biz(obl::test::RepeatedBlock(0x34, 0x12, 0xcd, 0xab)));
    source.Add("Data/Isle/isle.dat", obl::test::Grid([](size_t, size_t) { return obl::map_pack::kTerrainFlags[0]; }));
    source.Add("Data/Water/WaterAnim3.wat", obl::test::WaterFile(64));
    source.Add("Data/Isle/height.tga", obl::test::HeightTga(4, 3));
    const std::string island = "'WORLD_NAME' \"Isle\"\r\n'WORLD_MAPFILENAME' \"Data\\Isle\\Isle.biz\"\r\n"
                               "'WORLD_MAPGRID' \"Data/Isle/isle.dat\"\r\n'WORLD_MAPHEIGHTMAP' \"Data/Isle/height.tga\"\r\n"
                               "'WORLD_HEIGHTMAPSCALE' 2.5\r\n'WORLD_WATERANIM' \"Data/Water/WaterAnim3.wat\"\r\n"
                               "'WORLD_DEPTH_PLANE_OFFSET' -7\r\n'WORLD_BUILDINGLIST' \"Data/Script/BuildingList.pis\"\r\n"
                               "'WORLD_PATHTILEINFO' \"data/pathtiles.dat\"\r\n'STARTMAPX' 12\r\n'STARTMAPY' 3\r\n"
                               "'SCENARIO_OBJ_TITLE' \"TITLE\"\r\n'SCENARIO_OBJ_DESCRIPTION' \"DESC\"\r\n"
                               "'SCENARIO_OBJ_OBJECTIVES' \"GOALS\"\r\n'SCENARIO_OBJ_NUMBER' 1\r\n'SCENARIO_OBJ_0' \"GOAL0\"\r\n"
                               "'WORLD_STATUS_TITLE' \"ISLAND\"\r\n'WORLD_STATUS_AREA' \"AREA\"\r\n'WORLD_STATUS_TEMP' \"TEMP\"\r\n"
                               "'WORLD_STATUS_WEATHER' \"WEATHER\"\r\n'WORLD_STATUS_HAZARDS' \"HAZARDS\"\r\n"
                               "'BUILDING_0' \"KEY00\"\r\n'RADAR_IMAGE' \"SCANNER1\"\r\n'RADAR_POSX' 2\r\n'RADAR_POSY' 9\r\n"
                               "'WORLD_PAVEMENT_TILESET' \"PathTile_07\"\r\n'PROGRESS_DIALOG' \"Scenario1Progress\"\r\n";
    for (bool sandbox : {false, true})
        for (int n = 1; n <= 12; ++n) {
            const std::string name = std::string(sandbox ? "SandScenario" : "Scenario") + std::to_string(n);
            const std::string folder = "Data/Scenario/" + name + "/";
            const std::string guests = sandbox ? "sandboxh_guests" : (n < 10 ? "Guests0" : "Guests") + std::to_string(n);
            const std::string pis =
                    island + "'WORLD_SCRIPT' \"" + folder + name + ".lgc\"\r\n'WORLD_GUESTLIST' \"Data/Script/" + guests + ".pis\"\r\n";
            source.Add(folder + name + ".pis", Bytes(pis));
            source.Add(folder + name + ".ble", Encoded("// " + name + "\r\n"));
        }

    source.Add("Data/Script/GUI/GUI.ble", Encoded("// in game\r\n"));
    source.Add("Data/Script/GUI/Intro.ble", Encoded("// intro\r\n"));
    source.Add("Data/Script/GUI/MainMenu.ble", Encoded("// main menu\r\n"));
    source.Add("Data/Script/GUI/gamewon.ble", Encoded("// game won\r\n"));

    std::vector<uint8_t> index, samples;
    AddSample(index, samples, "splash.wav");
    source.Add("Data/Sounds/blsnd.hdr", index);
    source.Add("Data/Sounds/blsnd.spf", samples);
    source.Add("Data/Sounds/Sounds.ble", Encoded("// no scripted sounds\r\n"));

    const auto [spriteHeader, spriteData] = obl::test::Archive("Walker", kEuStamp);
    source.Add("Data/Sprites/BLSprite.hdr", spriteHeader);
    source.Add("Data/Sprites/BLSprite.dat", spriteData);

    source.Add("Data/Sprites/PaletteModifiers.bin", std::vector<uint8_t>(2304, 23));
    source.Add("Data/cursor.tga", Tga());
    source.Add("Data/Screens/menu.tga", Tga());
    for (int i = 0; i < 6; ++i) source.Add("Data/Haze/wibble_0" + std::to_string(i) + ".tif", Tiff({0, 17, 128, 255}));

    std::vector<uint8_t> track(256, 0);
    std::memcpy(track.data(), "ID3", 3);
    source.Add("Data/Soundtrack/beach.mp3", track);
    std::vector<uint8_t> movie(256, 0);
    movie[2] = 0x01;
    movie[3] = 0xBA;
    movie[4] = 0x21;
    source.Add("Data/Movies/EidosMovie.mpg", movie);

    std::vector<uint8_t> metrics(2056, 0);
    obl::test::PutU32(metrics, 0, 12);
    source.Add("Data/Fonts/ArialTexCoord.dat", metrics);
    source.Add("Data/Fonts/Arial.dat", std::vector<uint8_t>(65536, 0));
    source.Add("Data/Fonts/Arial.tiff", Tiff({0, 1, 2, 3}));

    source.Add("Data/text_uk.ble", TextTable("\"KEY00_NAME\" \"Hut\"\n"));
    return source;
}

// The steps of ImportPack in order (import.cpp).
const std::vector<std::string> kSteps = {"gui-tables", "game-parameters", "scripts", "maps", "gui-layouts", "sounds", "images", "media",
        "sprites", "fonts", "catalog", "guests", "texts", "scenarios", "pack"};

struct Imported {
    bool ok = false;
    std::string error;
    std::vector<std::string> steps;
    std::vector<std::pair<uint64_t, uint64_t>> spriteCounts; // (done, total) the count callback reported
    MemorySink pack;
};

Imported Import(const ByteSource& source, std::span<const KnownExe> known, std::optional<std::string> language = std::nullopt)
{
    obl::json_schema::Registry schemas;
    Imported out;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, out.error));
    const auto spool = OpenMemorySpool();
    ImportOptions options;
    options.language = std::move(language);
    options.spool = spool.get();
    options.knownExes = known;
    options.progress = [&](std::string_view step) { out.steps.emplace_back(step); };
    options.count = [&](std::string_view step, uint64_t done, uint64_t total) {
        if (step == "sprites") out.spriteCounts.emplace_back(done, total);
    };
    out.ok = ImportPack(source, out.pack, schemas, options, out.error);
    if (!out.ok) std::printf("  %s\n", out.error.c_str());
    return out;
}

// A memory spool that counts the files extracted into it.
class CountingSpool final : public Spool {
public:
    bool Create(std::string_view name) override
    {
        ++created;
        return inner_->Create(name);
    }
    bool Append(std::string_view name, std::span<const uint8_t> bytes) override { return inner_->Append(name, bytes); }
    bool Read(std::string_view name, uint64_t offset, std::span<uint8_t> out) override { return inner_->Read(name, offset, out); }
    void Remove(std::string_view name) override { inner_->Remove(name); }
    bool Has(std::string_view name) const override { return inner_->Has(name); }

    int created = 0;

private:
    std::unique_ptr<Spool> inner_ = OpenMemorySpool();
};

// The import shared between `shards` instances that meet only through the units they hand over, as the browser's workers do:
// a CD first has its sprite archive extracted once, every shard converts its share, and one more instance imports the whole
// copy from their units. `spool` is shared the way the workers share Blobs.
Imported ImportSharded(
        const ByteSource& source, std::span<const KnownExe> known, std::optional<std::string> language, int shards, Spool* spool)
{
    obl::json_schema::Registry schemas;
    Imported out;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, out.error));
    ImportOptions base;
    base.language = std::move(language);
    base.spool = spool;
    base.knownExes = known;
    if (IsCdRoot(source)) {
        ImportOptions prepare = base;
        prepare.prepareSpool = true;
        MemorySink nothing;
        OBL_REQUIRE(ImportPack(source, nothing, schemas, prepare, out.error));
        OBL_CHECK(nothing.files().empty());
    }
    std::map<std::pair<std::string, size_t>, UnitOutput> units;
    for (int index = 0; index < shards; ++index) {
        const Shard shard{index, shards};
        ImportOptions options = base;
        options.exchange.shard = &shard;
        options.exchange.sink = [&units](std::string_view step, size_t unit, UnitOutput& output) {
            units[{std::string(step), unit}] = std::move(output);
        };
        std::vector<std::string> steps;
        options.progress = [&](std::string_view step) { steps.emplace_back(step); };
        MemorySink nothing;
        OBL_REQUIRE(ImportPack(source, nothing, schemas, options, out.error));
        OBL_CHECK(nothing.files().empty()); // a shard writes no file of the pack
        OBL_CHECK(steps.empty());
    }
    ImportOptions options = base;
    options.exchange.source = [&units](std::string_view step, size_t unit, UnitOutput& output) {
        const auto it = units.find({std::string(step), unit});
        if (it == units.end()) return false;
        output = it->second;
        return true;
    };
    options.progress = [&](std::string_view step) { out.steps.emplace_back(step); };
    options.count = [&](std::string_view step, uint64_t done, uint64_t total) {
        if (step == "sprites") out.spriteCounts.emplace_back(done, total);
    };
    out.ok = ImportPack(source, out.pack, schemas, options, out.error);
    return out;
}

MemorySource SourceOf(const Files& files)
{
    MemorySource source;
    for (const auto& [path, bytes] : files) source.Add(path, bytes);
    return source;
}

} // namespace

OBL_TEST_CASE("convert.import_synthetic_installation")
{
    // Every step runs on our own bytes; the synthetic executable is known only to this test's table.
    const std::vector<uint8_t> exe = GameExe();
    const std::string hash = obl::sha256::Hex(exe); // KnownExe holds a view of it
    const KnownExe known[] = {{hash, ExeBuild::DigitalOct2002}};
    const MemorySource source = SourceOf(Installation(exe));
    const Imported refused = Import(source, {});
    OBL_CHECK(!refused.ok); // the supported builds do not include the synthetic executable
    OBL_CHECK(refused.error.starts_with("pack: "));
    const Imported imported = Import(source, known);
    OBL_REQUIRE(imported.ok);
    OBL_CHECK(imported.steps == kSteps);
    // Each of the two synthetic sprites is reported, which ends the count at 2 of 2.
    OBL_REQUIRE(imported.spriteCounts.size() == 2);
    OBL_CHECK_EQ(imported.spriteCounts.back().first, 2u);
    OBL_CHECK_EQ(imported.spriteCounts.back().second, 2u);
    const auto manifest = imported.pack.Read("pack.json");
    OBL_REQUIRE(manifest.has_value());
    const auto document = nlohmann::ordered_json::parse(manifest->begin(), manifest->end());
    OBL_CHECK_EQ(document["language"], "en");
    OBL_CHECK(!document.contains("source"));
    OBL_CHECK_EQ(document["import"]["reference"].get<std::string>().size(), size_t{64});
    // pack.json lists every other file of the pack.
    OBL_CHECK_EQ(document["files"].size() + 1, imported.pack.files().size());
    for (const char* path : {"gui/credits.json", "rules/game_parameters.json", "catalog/index.json", "fonts/arial.json",
                 "images/cursor.png", "sprites/palette_modifiers.json", "scenarios/s12/scenario.json", "maps/isle/map.json"})
        OBL_CHECK_EQ(imported.pack.Read(path).has_value() ? std::string() : path, std::string());
}

OBL_TEST_CASE("convert.import_synthetic_cd_image")
{
    // The same copy as a CD: the game's files in a cabinet, the executable loose, all in an ISO 9660 image. The CD
    // import gives the installation's pack byte for byte (its language comes from the caller: the CD has no
    // BeachLife.ini in the game's file group).
    const std::vector<uint8_t> exe = GameExe();
    const std::string hash = obl::sha256::Hex(exe); // KnownExe holds a view of it
    const KnownExe known[] = {{hash, ExeBuild::DigitalOct2002}};
    Files installed = Installation(exe);
    const Imported fromInstallation = Import(SourceOf(installed), known);
    OBL_REQUIRE(fromInstallation.ok);
    installed.erase("BeachLife.exe");
    installed.erase("BeachLife.ini");
    const obl::test::SyntheticCabinetFiles cabinet = obl::test::SyntheticCabinet(installed, "Game");
    obl::test::SyntheticIso iso;
    iso.Add("data1.hdr", cabinet.hdr);
    iso.Add("data1.cab", cabinet.cab);
    iso.Add("BeachLife.exe", exe);
    const std::vector<uint8_t> image = iso.Build();
    std::string error;
    const auto disc = web::OpenIsoSource(
            image.size(),
            [&](uint64_t offset, std::span<uint8_t> out) {
                if (offset > image.size() || out.size() > image.size() - offset) return false;
                std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
                return true;
            },
            error);
    OBL_REQUIRE(disc != nullptr);
    OBL_REQUIRE(IsCdRoot(*disc));
    OBL_CHECK(!Import(*disc, known).ok); // no language: the CD's tree has no BeachLife.ini
    const Imported fromCd = Import(*disc, known, "en");
    OBL_REQUIRE(fromCd.ok);
    OBL_CHECK(fromCd.steps == kSteps);
    OBL_CHECK(fromCd.pack.files() == fromInstallation.pack.files());
}

OBL_TEST_CASE("convert.import_in_shards_equals_import_alone")
{
    // The pack (pack.json and both stamps included), the steps and the counts are the same for one instance and for several
    // that share the units of the maps and the sprites; a CD's sprite archive is extracted once, not once per shard.
    const std::vector<uint8_t> exe = GameExe();
    const std::string hash = obl::sha256::Hex(exe); // KnownExe holds a view of it
    const KnownExe known[] = {{hash, ExeBuild::DigitalOct2002}};
    Files installed = Installation(exe);
    const MemorySource source = SourceOf(installed);
    const Imported alone = Import(source, known);
    OBL_REQUIRE(alone.ok);
    for (int shards : {1, 2, 3, 8}) {
        CountingSpool spool;
        const Imported shared = ImportSharded(source, known, std::nullopt, shards, &spool);
        OBL_REQUIRE(shared.ok);
        OBL_CHECK(shared.pack.files() == alone.pack.files());
        OBL_CHECK(shared.steps == alone.steps);
        OBL_CHECK(shared.spriteCounts == alone.spriteCounts);
    }

    installed.erase("BeachLife.exe");
    installed.erase("BeachLife.ini");
    const obl::test::SyntheticCabinetFiles cabinet = obl::test::SyntheticCabinet(installed, "Game");
    obl::test::SyntheticIso iso;
    iso.Add("data1.hdr", cabinet.hdr);
    iso.Add("data1.cab", cabinet.cab);
    iso.Add("BeachLife.exe", exe);
    const std::vector<uint8_t> image = iso.Build();
    std::string error;
    const auto disc = web::OpenIsoSource(
            image.size(),
            [&](uint64_t offset, std::span<uint8_t> out) {
                if (offset > image.size() || out.size() > image.size() - offset) return false;
                std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
                return true;
            },
            error);
    OBL_REQUIRE(disc != nullptr);
    const Imported fromCd = Import(*disc, known, "en");
    OBL_REQUIRE(fromCd.ok);
    for (int shards : {2, 3}) {
        CountingSpool spool;
        const Imported shared = ImportSharded(*disc, known, "en", shards, &spool);
        OBL_REQUIRE(shared.ok);
        OBL_CHECK(shared.pack.files() == fromCd.pack.files());
        OBL_CHECK(shared.steps == fromCd.steps);
    }

    // A file too large for memory is extracted once into a shared spool: a source that keeps it leaves it for the next to adopt,
    // and the next neither extracts it again nor removes it.
    CountingSpool spool;
    CdSourceLimits limits;
    limits.memory = 0;
    limits.keepSpooled = true;
    std::vector<uint8_t> first(24), second(24);
    {
        const auto extracting = OpenCdSource(*disc, spool, error, limits);
        OBL_REQUIRE(extracting != nullptr);
        PrepareSpriteData(*extracting, error);
        OBL_CHECK_EQ(spool.created, 1);
        OBL_REQUIRE(extracting->Read(*FindFile(*extracting, "Data/Sprites/BLSprite.dat"), 0, first));
    }
    limits.keepSpooled = false;
    {
        const auto adopting = OpenCdSource(*disc, spool, error, limits);
        OBL_REQUIRE(adopting != nullptr);
        OBL_REQUIRE(adopting->Read(*FindFile(*adopting, "Data/Sprites/BLSprite.dat"), 0, second));
    }
    OBL_CHECK_EQ(spool.created, 1);
    OBL_CHECK(first == second);
}

OBL_TEST_CASE("convert.import_refusals_name_their_step")
{
    const std::vector<uint8_t> exe = GameExe();
    const std::string hash = obl::sha256::Hex(exe);
    const KnownExe known[] = {{hash, ExeBuild::DigitalOct2002}};
    Files files = Installation(exe);
    files.erase("BeachLife.ini");
    const Imported noLanguage = Import(SourceOf(files), known);
    OBL_CHECK(!noLanguage.ok);
    OBL_CHECK(noLanguage.error.find("choose the language") != std::string::npos);
    OBL_CHECK(noLanguage.steps.empty()); // refused before the first step writes anything

    // A language the copy has no table for is refused up front, not answered with another language's texts.
    const Imported noTable = Import(SourceOf(Installation(exe)), known, "fr");
    OBL_CHECK(!noTable.ok);
    OBL_CHECK(noTable.error.find("no texts in the language \"fr\"") != std::string::npos);
    OBL_CHECK(noTable.steps.empty());

    // A CD without a spool has nowhere to keep large files.
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    MemorySource disc;
    disc.Add("data1.hdr", {});
    MemorySink pack;
    OBL_CHECK(!ImportPack(disc, pack, schemas, {}, error));
    OBL_CHECK_EQ(error, std::string("a CD import needs a spool"));

    // An empty folder fails at the first step, naming the executable.
    const Imported empty = Import(MemorySource{}, known, "en");
    OBL_CHECK(!empty.ok);
    OBL_CHECK(empty.error.starts_with("gui-tables: BeachLife.exe not found"));
}

OBL_TEST_CASE("convert.steps_survive_any_missing_or_cut_file")
{
    // Discovery and bounds of the quick steps: each of their files missing or cut in half gives a refusal or a whole
    // result, never a crash. The heavy steps (a debug import takes minutes) have their own refusal cases.
    obl::json_schema::Registry schemas;
    std::string loadError;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, loadError));
    Files whole = Installation(GameExe());
    std::erase_if(whole, [](const auto& file) {
        return file.first == "BeachLife.exe" || file.first.starts_with("Data/Scenario") || file.first.starts_with("Data/Isle") ||
                file.first.starts_with("Data/Water") || file.first == "Data/Sprites/BLSprite.dat";
    });
    int refusals = 0;
    const auto run = [&](const Files& files) {
        const MemorySource source = SourceOf(files);
        MemorySink out;
        int count = 0;
        std::string error;
        const bool results[] = {ConvertGameParameters(source, out, schemas, error), ConvertGuiLayouts(source, out, schemas, count, error),
                ConvertSamples(source, out, count, error), ConvertSoundScript(source, out, schemas, count, error),
                ConvertImages(source, out, schemas, count, error), ConvertMedia(source, out, count, error),
                ConvertFonts(source, out, schemas, count, error), ConvertGuestSets(source, out, schemas, count, error),
                ConvertTexts(source, out, schemas, count, error)};
        refusals += static_cast<int>(std::count(std::begin(results), std::end(results), false));
    };
    for (const auto& [path, bytes] : whole) {
        Files missing = whole;
        missing.erase(path);
        run(missing);
        Files cut = whole;
        cut[path].resize(bytes.size() / 2);
        run(cut);
    }
    OBL_CHECK(refusals > 0);
}
