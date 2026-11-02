#include "unit_test.h"
#include "formats/map_pack.h"
#include "core/json_schema.h"
#include "formats/schemas.h"
#include "importer/src/writers/map_convert.h"
#include "openbeach/importer/pack_sink.h"
#include "synthetic_map.h"

#include <cstring>

// Synthetic map sources for the strict decoders of map_decode.cpp: no installation needed. The installed-data comparison
// against the engine loaders stays in convert_map_test.cpp (data.).
using namespace obl::convert;
namespace {
using obl::test::Biz;
using obl::test::Grid;
using obl::test::HeightTga;
using obl::test::PutU32;
using obl::test::RepeatedBlock;
using obl::test::WaterFile;

std::vector<uint8_t> Bytes(std::string_view text, bool encoded = false)
{
    std::vector<uint8_t> b(text.begin(), text.end());
    if (encoded)
        for (auto& byte : b) byte ^= 0xff;
    return b;
}

// Two scenarios on one island (a campaign one with a height map and a water animation, its sandbox with the same
// references), the catalog's scenery and the scripts naming props.
obl::convert::MemorySource MapSource()
{
    MemorySource source;
    source.Add("Data/Script/BuildingList.ble",
            Bytes("\"SCENERY_ROCK\" \"ROCK_NAME\" \"Stone\" \"ROCK_SPRITE\" \"ROCK\" \"ROCK_GRIDWIDTH\" \"1\" "
                  "\"ROCK_GRIDHEIGHT\" \"1\" \"ROCK_BUILDON\" \"LAND\"\n",
                    true));
    const std::string pis = "'WORLD_MAPFILENAME' \"Data\\Isle\\Isle.biz\"\r\n'WORLD_MAPGRID' \"Data/Isle/isle.dat\"\r\n"
                            "'WORLD_MAPHEIGHTMAP' \"Data/Isle/height.tga\"\r\n'WORLD_HEIGHTMAPSCALE' 2.5\r\n"
                            "'WORLD_WATERANIM' \"Data/Water/WaterAnim3.wat\"\r\n'WORLD_DEPTH_PLANE_OFFSET' -7\r\n";
    source.Add("Data/Scenario/Scenario1/Scenario1.pis", Bytes(pis + "'WORLD_SCRIPT' \"Data/Scenario/Scenario1/Scenario1.lgc\"\r\n"));
    source.Add("Data/Scenario/Scenario1/Scenario1.ble", Bytes("Create Scenery Rock 1 2 3 BigRock;\n", true));
    source.Add("Data/Scenario/SandScenario1/SandScenario1.pis",
            Bytes(pis + "'WORLD_SCRIPT' \"Data/Scenario/SandScenario1/SandScenario1.lgc\"\r\n"));
    source.Add("Data/Scenario/SandScenario1/SandScenario1.ble", Bytes("Create Scenery rock -1 0 Pebble;\n", true));
    source.Add("Data/Isle/Isle.biz", Biz(RepeatedBlock(0x34, 0x12, 0xcd, 0xab)));
    source.Add("Data/Isle/isle.dat", Grid([](size_t x, size_t) { return obl::map_pack::kTerrainFlags[x % 7]; }));
    source.Add("Data/Isle/height.tga", HeightTga(4, 3));
    source.Add("Data/Water/WaterAnim3.wat", WaterFile(64));
    return source;
}

nlohmann::ordered_json Document(const MemorySink& sink, std::string_view path)
{
    const auto bytes = sink.Read(path);
    OBL_REQUIRE(bytes.has_value());
    return nlohmann::ordered_json::parse(bytes->begin(), bytes->end());
}
} // namespace

OBL_TEST_CASE("convert.map_terrain_transposes_and_indexes_flags")
{
    const auto bytes = Grid([](size_t x, size_t y) { return obl::map_pack::kTerrainFlags[(x + 2 * y) % 7]; });
    std::vector<uint8_t> indices;
    std::string error;
    OBL_REQUIRE(DecodeMapTerrain(bytes, indices, error));
    OBL_REQUIRE(indices.size() == 128 * 128);
    OBL_CHECK(indices[0] == 0);
    OBL_CHECK(indices[1] == 1); // x = 1, y = 0
    OBL_CHECK(indices[128] == 2); // x = 0, y = 1
    OBL_CHECK(indices[3 * 128 + 5] == (5 + 6) % 7);

    auto bad = bytes;
    PutU32(bad, (7 * 128 + 9) * 4, 0x12345678);
    OBL_CHECK(!DecodeMapTerrain(bad, indices, error));
    OBL_CHECK(error.find("unrecognized terrain flag") != std::string::npos);
    OBL_CHECK(error.find("7,9") != std::string::npos);
    OBL_CHECK(!DecodeMapTerrain(std::span(bytes).first(bytes.size() - 4), indices, error));
}

OBL_TEST_CASE("convert.map_block_decodes_bounded_refpack")
{
    const auto bytes = RepeatedBlock(0x34, 0x12, 0xcd, 0xab);
    std::vector<uint16_t> words;
    std::string error;
    OBL_REQUIRE(DecodeMapBlock(bytes, words, error));
    OBL_REQUIRE(words.size() == 32768);
    OBL_CHECK(words[0] == 0x1234);
    OBL_CHECK(words[1] == 0xabcd);
    OBL_CHECK(words[32766] == 0x1234);
    OBL_CHECK(words[32767] == 0xabcd);

    auto header = bytes;
    header[1] = 0xfa;
    OBL_CHECK(!DecodeMapBlock(header, words, error));
    OBL_CHECK(error == "invalid map RefPack header");
    auto size = bytes;
    size[4] = 1;
    OBL_CHECK(!DecodeMapBlock(size, words, error));
    OBL_CHECK(error == "map RefPack block must decode to 65536 bytes");
    // Without the end command, and with a trailing byte after it.
    OBL_CHECK(!DecodeMapBlock(std::span(bytes).first(bytes.size() - 1), words, error));
    OBL_CHECK(error == "missing RefPack end");
    auto trailing = bytes;
    trailing.push_back(0);
    OBL_CHECK(!DecodeMapBlock(trailing, words, error));
    OBL_CHECK(error == "invalid RefPack end");
    // A backreference before any output, and a command cut short.
    const std::vector<uint8_t> early = {0x10, 0xfb, 0x01, 0x00, 0x00, 0x00, 0x00};
    OBL_CHECK(!DecodeMapBlock(early, words, error));
    OBL_CHECK(error == "invalid RefPack backreference");
    const std::vector<uint8_t> cut = {0x10, 0xfb, 0x01, 0x00, 0x00, 0xc0, 0x00};
    OBL_CHECK(!DecodeMapBlock(cut, words, error));
    OBL_CHECK(error == "truncated RefPack command");
}

OBL_TEST_CASE("convert.map_water_fills_fifteen_frames_of_four_masks")
{
    const auto bytes = WaterFile(64);
    std::vector<uint8_t> sheet;
    std::string error;
    OBL_REQUIRE(DecodeMapWater(bytes, sheet, error));
    OBL_REQUIRE(sheet.size() == 1920 * 512);
    // Runs alternate set/clear 64 pixels at a time: row 0 of mask 0 starts set, the second half of the row is clear.
    OBL_CHECK(sheet[0] == 255);
    OBL_CHECK(sheet[64] == 0);
    OBL_CHECK(sheet[14 * 128] == 255); // frame 14
    OBL_CHECK(sheet[128 * 1920 + 64] == 0); // mask 1, row 0

    auto size = bytes;
    PutU32(size, 0, 1);
    OBL_CHECK(!DecodeMapWater(size, sheet, error));
    OBL_CHECK(error == "invalid water animation size");
    auto range = bytes;
    PutU32(range, 4 + 3 * 4, 1);
    OBL_CHECK(!DecodeMapWater(range, sheet, error));
    OBL_CHECK(error == "invalid water frame range");
    auto shortFrame = bytes;
    PutU32(shortFrame, 64, 3);
    OBL_CHECK(!DecodeMapWater(shortFrame, sheet, error));
    OBL_CHECK(error == "water frame does not fill four masks");
}

OBL_TEST_CASE("convert.map_height_reads_indexed_tga_samples")
{
    const auto bytes = HeightTga(4, 3);
    int width = 0, height = 0;
    std::vector<uint8_t> samples;
    std::string error;
    OBL_REQUIRE(DecodeMapHeight(bytes, width, height, samples, error));
    OBL_CHECK(width == 4);
    OBL_CHECK(height == 3);
    OBL_REQUIRE(samples.size() == 12);

    auto truecolor = bytes;
    truecolor[2] = 2;
    OBL_CHECK(!DecodeMapHeight(truecolor, width, height, samples, error));
    OBL_CHECK(error == "unsupported height TGA layout");
    auto interleaved = bytes;
    interleaved[17] = 0x10;
    OBL_CHECK(!DecodeMapHeight(interleaved, width, height, samples, error));
    OBL_CHECK(!DecodeMapHeight(HeightTga(1, 3), width, height, samples, error));
    OBL_CHECK(error == "invalid height TGA");
}

OBL_TEST_CASE("convert.map_prop_aliases_take_the_last_name")
{
    std::vector<MapPropAlias> aliases;
    std::string error;
    OBL_REQUIRE(ParseMapPropAliases("// Create Scenery Ignored 1 2 3 Comment;\n"
                                    "Create Scenery PalmTree 10 20 1 Number 3 Palm1;\n"
                                    "Create Scenery Rock random BigRock Rock2;\n"
                                    "Create Scenery Bush -1 0 Shrub;\n"
                                    "Create Guest Someone 1 2 3 NotProp;\n",
            aliases, error));
    OBL_REQUIRE(aliases.size() == 3);
    OBL_CHECK(aliases[0].name == "Palm1");
    OBL_CHECK(aliases[0].definition == "PalmTree");
    OBL_CHECK(aliases[1].name == "Rock2");
    OBL_CHECK(aliases[2].name == "Shrub");

    OBL_CHECK(!ParseMapPropAliases("Create Scenery PalmTree 1 2 3 Palm", aliases, error));
    OBL_CHECK(error == "unterminated Create Scenery");
    OBL_CHECK(!ParseMapPropAliases("Create Scenery ;", aliases, error));
    OBL_CHECK(error == "missing scenery definition");
    OBL_CHECK(!ParseMapPropAliases("Create Scenery PalmTree 1 2 3 ;", aliases, error));
    OBL_CHECK(error == "empty scenery name");
    OBL_CHECK(!ParseMapPropAliases("Create Scenery PalmTree 1 2 3 Number", aliases, error));
}

OBL_TEST_CASE("convert.maps_convert_a_synthetic_installation")
{
    std::string error;
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    const auto source = MapSource();
    MemorySink out;
    int files = 0;
    OBL_REQUIRE(ConvertMaps(source, out, schemas, files, error));
    // One island (the sandbox shares it), one water sheet, the prop lookup.
    OBL_CHECK_EQ(files, 8);
    for (const char* file : {"maps/isle/map.json", "maps/isle/terrain.png", "maps/isle/height.png", "maps/isle/color.png",
                 "maps/isle/depth.png", "maps/water/3.png", "maps/water/3.json", "maps/props.json"})
        OBL_CHECK(out.Read(file).has_value());
    const auto map = Document(out, "maps/isle/map.json");
    OBL_CHECK_EQ(map["event_area"], nlohmann::ordered_json::array({2884, 5054, 3951, 6962}));
    OBL_CHECK_EQ(map["height"], "height.png");
    OBL_CHECK_EQ(map["height_scale"], 2.5);
    OBL_CHECK_EQ(map["depth_plane_offset"], -7);
    OBL_CHECK_EQ(map["depth_slant_offset"], 16);
    OBL_CHECK_EQ(map["water_anim"], "water/3");
    // Catalog names alias their definition; script names follow, each definition listed once.
    const auto props = Document(out, "maps/props.json")["props"];
    OBL_CHECK_EQ(props["stone"], nlohmann::ordered_json::array({"rock"}));
    OBL_CHECK_EQ(props["bigrock"], nlohmann::ordered_json::array({"rock"}));
    OBL_CHECK_EQ(props["pebble"], nlohmann::ordered_json::array({"rock"}));
}

OBL_TEST_CASE("convert.maps_without_height_map")
{
    // No WORLD_MAPHEIGHTMAP: a flat island without height.png (the empty path must not find the source root).
    std::string error;
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    auto source = MapSource();
    for (const char* name : {"Scenario1", "SandScenario1"})
        source.Add(std::string("Data/Scenario/") + name + "/" + name + ".pis",
                Bytes("'WORLD_MAPFILENAME' \"Data/Isle/Isle.biz\"\r\n'WORLD_MAPGRID' \"Data/Isle/isle.dat\"\r\n'WORLD_SCRIPT' "
                      "\"Data/Scenario/" +
                        std::string(name) + "/" + name + ".lgc\"\r\n"));
    MemorySink out;
    int files = 0;
    OBL_REQUIRE(ConvertMaps(source, out, schemas, files, error));
    OBL_CHECK(!out.Read("maps/isle/height.png").has_value());
    const auto map = Document(out, "maps/isle/map.json");
    OBL_CHECK(!map.contains("height"));
    OBL_CHECK_EQ(map["height_scale"], 0);
}

OBL_TEST_CASE("convert.maps_refuse_conflicts_and_unlisted_scenery")
{
    std::string error;
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    int files = 0;
    {
        auto source = MapSource();
        source.Add("Data/Scenario/SandScenario1/SandScenario1.pis",
                Bytes("'WORLD_MAPFILENAME' \"Data/Isle/Isle.biz\"\r\n'WORLD_MAPGRID' \"Data/Isle/isle.dat\"\r\n"
                      "'WORLD_MAPHEIGHTMAP' \"Data/Isle/none.tga\"\r\n"
                      "'WORLD_SCRIPT' \"Data/Scenario/SandScenario1/SandScenario1.lgc\"\r\n"));
        MemorySink out;
        OBL_CHECK(!ConvertMaps(source, out, schemas, files, error));
        OBL_CHECK_EQ(error, "conflicting map references: isle");
    }
    {
        auto source = MapSource();
        source.Add("Data/Scenario/Scenario1/Scenario1.ble", Bytes("Create Scenery Palm 1 2 3 Tree;\n", true));
        MemorySink out;
        OBL_CHECK(!ConvertMaps(source, out, schemas, files, error));
        OBL_CHECK(error.ends_with(" scenery: Palm"));
    }
    {
        auto source = MapSource();
        source.Add("Data/Scenario/Scenario2/scenario1.PIS", Bytes(""));
        MemorySink out;
        OBL_CHECK(!ConvertMaps(source, out, schemas, files, error));
        OBL_CHECK_EQ(error, "duplicate scenario file: scenario1");
    }
    {
        auto source = MapSource();
        source.Add("Data/Water/WaterAnim3.wat", {1, 2, 3});
        MemorySink out;
        OBL_CHECK(!ConvertMaps(source, out, schemas, files, error));
    }
    {
        MemorySource source;
        source.Add("Data/Script/BuildingList.ble", Bytes("", true));
        MemorySink out;
        OBL_CHECK(!ConvertMaps(source, out, schemas, files, error));
        OBL_CHECK_EQ(error, "missing Data/Scenario");
    }
}
