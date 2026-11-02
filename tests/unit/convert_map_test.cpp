#include "unit_test.h"
#include "game/tables/catalog_load.h"
#include "game/world/world_script_types.h"
#include "game/map/grid_lifecycle.h"
#include "core/image_decode.h"
#include "core/json_schema.h"
#include "formats/map_pack.h"
#include "core/refpack.h"
#include "importer/src/readers/pis.h"
#include "game/map/water_anim.h"
#include "game/world/world_construct_pis.h"
#include "importer/src/writers/content_key.h"
#include "importer/src/writers/map_convert.h"
#include "importer/src/writers/png_write.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "game/world/world_script_parse.h"
#include "game/world/world_script_load.h"

#include "game/map/grid_state.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/stb/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>

using namespace obl::convert;
namespace {
void Set32(std::vector<uint8_t>& b, size_t at, uint32_t value)
{
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(value >> (i * 8));
}
std::vector<uint8_t> Read(const std::filesystem::path& path)
{
    std::vector<uint8_t> b;
    std::string error;
    OBL_REQUIRE(ReadBoundedFile(path.string(), 128 << 20, b, error));
    return b;
}
std::vector<uint8_t> Png8(const std::filesystem::path& path, int w, int h, int channels)
{
    const auto b = Read(path);
    int width = 0, height = 0, n = 0;
    auto* p = stbi_load_from_memory(b.data(), static_cast<int>(b.size()), &width, &height, &n, channels);
    OBL_REQUIRE(p);
    OBL_CHECK_EQ(width, w);
    OBL_CHECK_EQ(height, h);
    std::vector<uint8_t> out(p, p + size_t(width) * height * channels);
    stbi_image_free(p);
    return out;
}
uint32_t LittleU32(const std::vector<uint8_t>& bytes, size_t at)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, 4);
    return value;
}

// The island planes of a .biz file as the engine's blocks hold them: the 32 x 32 blocks in table order ([y][x]), each a RefPack stream of
// colour words then depth words. Block (x, y) fills the region at x * 128, y * 128 (the converter's cut, written out here independently of it).
obl::map_pack::IslandPlanes BizIsland(const std::vector<uint8_t>& bytes)
{
    obl::map_pack::IslandPlanes planes;
    planes.colour.assign(4096u * 4096u, 0);
    planes.depth.assign(4096u * 4096u, 0);
    std::vector<uint16_t> block(0x8000);
    for (size_t y = 0; y < 32; ++y)
        for (size_t x = 0; x < 32; ++x) {
            const size_t index = y * 32 + x;
            const size_t offset = LittleU32(bytes, index * 4), size = LittleU32(bytes, 4096 + index * 4);
            OBL_REQUIRE(offset + size <= bytes.size());
            OBL_REQUIRE(obl::refpack::Decode(bytes.data() + offset, reinterpret_cast<uint8_t*>(block.data()), 65536) == 65536);
            for (size_t by = 0; by < 128; ++by)
                for (size_t bx = 0; bx < 128; ++bx) {
                    const size_t src = by * 128 + bx, dst = (y * 128 + by) * 4096 + x * 128 + bx;
                    planes.colour[dst] = block[src];
                    planes.depth[dst] = block[16384 + src];
                }
        }
    return planes;
}
struct GridEnv {
    std::vector<uint32_t> flags;
    void SetGlobalGrid(obl::map::Grid*) {}
    bool HasHeightMap(const char*) { return false; }
    void RememberHeightMapName(const char*) {}
    bool LoadHeightMap(const char*, obl::map_pack::HeightSamples&) { return false; }
    const uint32_t* ReadGridFile(const char*) { return flags.data(); }
    void FreeGridFile(const uint32_t*) {}
    void ScreenToWorld(const int32_t*, float*) {}
    void WorldToTile(const float*, int32_t* out) { out[0] = out[1] = 0; }
    uint32_t FogStride() { return 32; }
    void MarkFog(uint32_t) {}
};
struct PisEnv {
    std::vector<obl::pis::RawEntry> raw;
    const char* GetString(const char* key) { return obl::pis::GetString(raw.data(), static_cast<int>(raw.size()), key); }
    const float* GetNumber(const char* key) { return obl::pis::GetNumber(raw.data(), static_cast<int>(raw.size()), key); }
    void MissingKey(const char*) { OBL_REQUIRE(false); }
};
} // namespace

OBL_TEST_CASE("convert_map.terrain_transpose_and_unrecognized_flags")
{
    std::vector<uint8_t> b(65536), indices;
    for (size_t i = 0; i < 16384; ++i) Set32(b, i * 4, 0x80000001);
    Set32(b, (3 * 128 + 7) * 4, 0x20000008);
    std::string error;
    OBL_REQUIRE(DecodeMapTerrain(b, indices, error));
    OBL_CHECK_EQ(indices[7 * 128 + 3], 3);
    OBL_CHECK_EQ(indices[3 * 128 + 7], 0);
    Set32(b, 0, 0x80000003);
    OBL_CHECK(!DecodeMapTerrain(b, indices, error));
    b.pop_back();
    OBL_CHECK(!DecodeMapTerrain(b, indices, error));
    OBL_CHECK_EQ(ContentKey(" Palm --4s! "), "palm_4s");
    OBL_CHECK_EQ(ContentKey("Flower 5"), "flower_5");
}

OBL_TEST_CASE("convert_map.png_index_and_depth_bits")
{
    const uint8_t pixels[] = {0, 1, 2, 1}, palette[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    const auto png = EncodeIndexedPng(2, 2, pixels, palette);
    OBL_REQUIRE(!png.empty());
    OBL_CHECK_EQ(png[25], 3); // actual indexed PNG, not grayscale masquerading as indices
    const uint16_t depths[] = {0, 1, 0x1234, 65535};
    const auto depth = EncodeGray16Png(2, 2, depths);
    OBL_REQUIRE(!depth.empty());
    OBL_CHECK_EQ(depth[24], 16);
    int w = 0, h = 0, n = 0;
    auto* p = stbi_load_16_from_memory(depth.data(), static_cast<int>(depth.size()), &w, &h, &n, 1);
    OBL_REQUIRE(p);
    for (int i = 0; i < 4; ++i) OBL_CHECK_EQ(p[i], depths[i]);
    stbi_image_free(p);
}

OBL_TEST_CASE("convert_map.height_samples_ignore_display_palette")
{
    std::vector<uint8_t> b(18 + 6 + 4);
    b[1] = b[2] = 1;
    b[5] = 2;
    b[7] = 24;
    b[12] = b[14] = 2;
    b[16] = 8;
    b[17] = 8; // shipped files can have this attribute count; the engine still reads sample bytes
    std::fill(b.begin() + 18, b.begin() + 24, uint8_t(200));
    b[24] = b[27] = 1;
    int width = 0, height = 0;
    std::string error;
    std::vector<uint8_t> samples;
    OBL_REQUIRE(DecodeMapHeight(b, width, height, samples, error));
    OBL_CHECK_EQ(width, 2);
    OBL_CHECK_EQ(height, 2);
    OBL_CHECK(samples == std::vector<uint8_t>({0, 1, 1, 0}));
    b[17] = 0x20;
    OBL_CHECK(!DecodeMapHeight(b, width, height, samples, error));
    b[17] = 0;
    b.pop_back();
    OBL_CHECK(!DecodeMapHeight(b, width, height, samples, error));
}

OBL_TEST_CASE("convert_map.bounded_refpack_commands")
{
    // An own block of literals, color and depth deliberately differ.
    std::vector<uint8_t> b{0x10, 0xfb, 1, 0, 0};
    std::vector<uint8_t> expected(65536);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i * 7);
    for (size_t at = 0; at < expected.size();) {
        const size_t count = std::min(size_t(112), expected.size() - at);
        b.push_back(static_cast<uint8_t>(0xe0 + count / 4 - 1));
        b.insert(b.end(), expected.begin() + at, expected.begin() + at + count);
        at += count;
    }
    b.push_back(0xfc);
    std::string error;
    std::vector<uint16_t> words;
    OBL_REQUIRE(DecodeMapBlock(b, words, error));
    std::vector<uint8_t> engine(65536);
    OBL_REQUIRE(obl::refpack::Decode(b.data(), engine.data(), engine.size()) == 65536);
    OBL_CHECK(engine == expected);
    for (size_t i = 0; i < words.size(); ++i) OBL_CHECK_EQ(words[i], uint16_t(expected[i * 2] | (expected[i * 2 + 1] << 8)));
    b.pop_back();
    OBL_CHECK(!DecodeMapBlock(b, words, error));
    b.resize(6);
    b[5] = 0xc0;
    OBL_CHECK(!DecodeMapBlock(b, words, error));
    b = {0x10, 0xfb, 1, 0, 0, 0, 0};
    OBL_CHECK(!DecodeMapBlock(b, words, error)); // backreference before the output
}

OBL_TEST_CASE("convert_map.water_rejects_ranges_and_incomplete_frames")
{
    std::vector<uint8_t> b(125), sheet;
    Set32(b, 0, 1);
    for (int i = 0; i < 15; ++i) Set32(b, 64 + i * 4, 1);
    b[124] = 127;
    std::string error;
    OBL_CHECK(!DecodeMapWater(b, sheet, error));
    Set32(b, 4, 2);
    OBL_CHECK(!DecodeMapWater(b, sheet, error));
}

OBL_TEST_CASE("convert_map.water_frame_and_mask_axes")
{
    std::vector<uint8_t> b(124), sheet;
    for (int mask = 0; mask < 4; ++mask) {
        int remaining = 16384;
        while (remaining) {
            const int count = std::min(remaining, mask % 2 ? 128 : 127);
            b.push_back(static_cast<uint8_t>(mask % 2 ? -count : count));
            remaining -= count;
        }
    }
    Set32(b, 0, static_cast<uint32_t>(b.size() - 124));
    for (int frame = 0; frame < 15; ++frame) Set32(b, 64 + frame * 4, static_cast<uint32_t>(b.size() - 124));
    std::string error;
    OBL_REQUIRE(DecodeMapWater(b, sheet, error));
    for (size_t y = 0; y < 512; ++y)
        for (size_t x = 0; x < 1920; ++x) OBL_CHECK_EQ(sheet[y * 1920 + x], y / 128 % 2 ? 0 : 255);
}

OBL_TEST_CASE("convert_map.prop_aliases_match_script_create_parser")
{
    const std::string text = "State TEST OnEntry / Create Scenery ignored Random Bad;\n"
                             "Create Scenery TREE01 3 7 2 First Number 2 Final;\n"
                             "Create Scenery TREE02 rAnDoM Number 3 Shared;\n"
                             "Create Scenery TREE03 -1 -1 Negative;\n"
                             "Create Building HOUSE 1 2 0; End End\n";
    std::vector<MapPropAlias> aliases;
    std::string error;
    OBL_REQUIRE(ParseMapPropAliases(text, aliases, error));
    OBL_REQUIRE(aliases.size() == 3);
    obl::world_script::WorldScript engine{};
    const obl::world_script_parse::Memory heap{std::malloc, std::free};
    obl::world_script_parse::Parse(engine, text.data(), text.size(), {}, heap, [](const char*) { return 0; });
    size_t i = 0;
    for (const auto* command = engine.m_states[0].m_onEntry; command; command = command->m_next) {
        if (command->m_opcode != 12) continue;
        const auto* create = static_cast<const obl::world_script::WorldScriptCreateData*>(command->m_data);
        if (create->m_class != 1) continue;
        OBL_REQUIRE(i < aliases.size());
        OBL_CHECK_EQ(aliases[i].name, std::string(create->m_sprite));
        OBL_CHECK_EQ(aliases[i].definition, std::string(create->m_type));
        ++i;
    }
    OBL_CHECK_EQ(i, aliases.size());
    obl::world_script_parse::Destroy(engine, heap);
    OBL_CHECK(!ParseMapPropAliases("Create Scenery TREE01 Random;", aliases, error));
    OBL_CHECK(!ParseMapPropAliases("Create Scenery TREE01 1", aliases, error));
}

OBL_TEST_CASE("data.convert_maps_match_engine_loaders")
{
    const std::filesystem::path root = OBL_GAME_DIR;
    if (!FindInstalledFile(root, "Data/Scenario/Scenario1/scenario1.biz")) OBL_SKIP("installation maps unavailable");
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    // An externally converted pack lets the same oracle verify another platform's CLI without converting it again.
    const char* supplied = std::getenv("OBL_MAP_ORACLE_PACK");
    const auto out = supplied
            ? std::filesystem::path(supplied)
            : root / "tmp" / ("convert-maps-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!supplied) {
        int files = 0;
        const bool converted =
                ConvertMaps(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), schemas, files, error);
        if (!converted) obl::unit::ReportFailure(__FILE__, __LINE__, error);
        OBL_REQUIRE(converted);
        OBL_CHECK(files >= 53);
    }
    constexpr uint32_t kFlags[] = {0x80000001, 0x80000002, 0x40000004, 0x20000008, 0x100, 0x20, 0x80};
    for (int map = 1; map <= 12; ++map) {
        const auto key = "scenario" + std::to_string(map);
        const auto prefix = "Data/Scenario/" + key + "/" + key;
        const auto directory = out / "maps" / key;
        const auto docBytes = Read(directory / "map.json");
        const auto doc = nlohmann::json::parse(docBytes);
        OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.map", doc, error));
        const auto pisBytes = Read(*FindInstalledFile(root, prefix + ".pis"));
        auto entries = obl::pis::ParseEntries(reinterpret_cast<const char*>(pisBytes.data()), static_cast<int>(pisBytes.size()));
        PisEnv pisEnv;
        for (auto& e : entries)
            pisEnv.raw.push_back({e.key.data(), e.type, e.hasString ? e.string.data() : nullptr, e.hasNumber ? &e.number : nullptr});
        const auto header = obl::world_construct_pis::ReadHeader(pisEnv);
        OBL_CHECK_EQ(doc["depth_plane_offset"], obl::world_construct_pis::DepthPlaneOffset(header.depthPlane));
        OBL_CHECK_EQ(doc["depth_slant_offset"], obl::world_construct_pis::DepthSlantOffset(header.depthSlant));
        if (doc.contains("height")) OBL_CHECK_EQ(doc["height_scale"].get<float>(), *header.heightScale);
        const auto terrain = Png8(directory / "terrain.png", 128, 128, 3);
        const auto source = Read(*FindInstalledFile(root, prefix + "grid.dat"));
        std::vector<uint8_t> indices;
        OBL_REQUIRE(DecodeMapTerrain(source, indices, error));
        constexpr uint8_t kPalette[] = {65, 130, 55, 235, 210, 140, 65, 175, 205, 20, 60, 145, 210, 190, 130, 90, 90, 90, 200, 40, 200};
        for (size_t pixel = 0; pixel < indices.size(); ++pixel)
            for (size_t channel = 0; channel < 3; ++channel)
                OBL_CHECK_EQ(terrain[pixel * 3 + channel], kPalette[indices[pixel] * 3 + channel]);
        GridEnv env;
        env.flags.resize(16384);
        std::memcpy(env.flags.data(), source.data(), source.size());
        obl::map::Grid grid{};
        obl::grid_lifecycle::ConstructGrid(grid, env, 128, 128, 32, "", 0, "");
        for (size_t x = 0; x < 128; ++x)
            for (size_t y = 0; y < 128; ++y) {
                const uint32_t effective = x == 0 || x == 127 || y == 0 || y == 127 ? 0x20 : kFlags[indices[y * 128 + x]];
                OBL_CHECK_EQ(grid.Cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).m_flags, effective);
            }
        OBL_CHECK_EQ(terrain.size(), 128u * 128u * 3u);
        if (doc.contains("height")) {
            const auto tga = Read(*FindInstalledFile(root, prefix + "height.tga"));
            obl::image::Image engine;
            OBL_REQUIRE(obl::image::DecodeTga(tga.data(), tga.size(), engine));
            OBL_CHECK(Png8(directory / "height.png", engine.width, engine.height, 1) == engine.pixels);
        } else
            OBL_CHECK_EQ(doc["height_scale"], 0);
        // The pack's island planes, read by the game's loader, against the planes of the installed .biz (every pixel).
        const auto island = BizIsland(Read(*FindInstalledFile(root, prefix + ".biz")));
        obl::map_pack::IslandPlanes pack;
        OBL_REQUIRE(obl::map_pack::ReadIslandPlanes(out, key, pack, error));
        OBL_REQUIRE(pack.colour.size() == island.colour.size());
        OBL_REQUIRE(pack.depth.size() == island.depth.size());
        size_t colorMismatches = 0, depthMismatches = 0;
        for (size_t i = 0; i < island.colour.size(); ++i) {
            colorMismatches += pack.colour[i] != island.colour[i];
            depthMismatches += pack.depth[i] != island.depth[i];
        }
        OBL_CHECK_EQ(colorMismatches, 0u);
        OBL_CHECK_EQ(depthMismatches, 0u);
    }
    const auto props = nlohmann::json::parse(Read(out / "maps/props.json"));
    OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.map_props", props, error));
    OBL_CHECK(props["props"].contains("palm_4s"));
    OBL_CHECK(props["props"].contains("flower_5"));
    OBL_CHECK(props["props"]["palm"].size() > 1);
    const auto ids = root / "packs/base/scripts/world_vocabulary.json";
    if (std::filesystem::exists(ids)) {
        const auto bytes = Read(ids);
        obl::world_script_parse::Vocabulary vocabulary;
        OBL_REQUIRE(obl::world::ParseWorldScriptIds(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), vocabulary, error));
        auto params = std::make_unique<obl::tables::WorldParams>();
        OBL_REQUIRE(obl::catalog::LoadPack(root / "packs" / "base", *params, error));
        obl::world::AddCatalogModifiers(*params, vocabulary);
        size_t aliases = 0;
        std::function<void(const obl::script::ScriptCommand*)> visit = [&](const obl::script::ScriptCommand* command) {
            for (; command; command = command->m_next) {
                if (command->m_opcode == 4) {
                    const auto* branch = static_cast<const obl::world_script::ScriptIfData*>(command->m_data);
                    visit(branch->m_true);
                    visit(branch->m_false);
                } else if (command->m_opcode == 12) {
                    const auto* create = static_cast<const obl::world_script::WorldScriptCreateData*>(command->m_data);
                    if (create->m_class != 1) continue;
                    const auto key = ContentKey(create->m_sprite), definition = LowerAscii(create->m_type);
                    OBL_REQUIRE(props["props"].contains(key));
                    const auto& candidates = props["props"][key];
                    OBL_CHECK(std::find(candidates.begin(), candidates.end(), definition) != candidates.end());
                    ++aliases;
                }
            }
        };
        const obl::world_script_parse::Memory heap{std::malloc, std::free};
        for (const auto& file : std::filesystem::recursive_directory_iterator(root / "Data/Scenario")) {
            if (LowerAscii(file.path().extension().string()) != ".ble") continue;
            auto scriptBytes = Read(file.path());
            for (auto& byte : scriptBytes) byte ^= 0xff;
            obl::world_script::WorldScript script{};
            obl::world_script_parse::Parse(script, reinterpret_cast<const char*>(scriptBytes.data()), scriptBytes.size(), vocabulary, heap,
                    [](const char*) { return 0; });
            for (uint32_t i = 0; i < script.m_numStates; ++i) {
                const auto& state = script.m_states[i];
                visit(state.m_onEntry);
                visit(state.m_onExit);
                visit(state.m_onUpdate);
                for (uint32_t h = 0; h < state.m_NumHandlers; ++h) {
                    visit(state.m_handlers[h].m_onReceipt);
                    visit(state.m_handlers[h].m_onSend);
                }
            }
            obl::world_script_parse::Destroy(script, heap);
        }
        OBL_CHECK(aliases > 1000);
    }
    for (const auto& entry : std::filesystem::directory_iterator(out / "maps/water")) {
        if (entry.path().extension() != ".json") continue;
        const auto key = entry.path().stem().string();
        const auto input = FindInstalledFile(root, "Data/WaterAnims/WaterAnim" + key + ".bin");
        OBL_REQUIRE(input);
        auto water = std::make_unique<obl::ui::WaterAnim>();
        obl::water_anim::Construct(*water);
        const obl::water_anim::Allocator heap{std::malloc, std::free};
        OBL_REQUIRE(obl::water_anim::Load(*water, input->string().c_str(), heap));
        const auto sheet = Png8(out / "maps/water" / (key + ".png"), 1920, 512, 1);
        for (int frame = 0; frame < 15; ++frame) {
            OBL_REQUIRE(obl::water_anim::Update(*water, static_cast<float>((frame + 0.25) / 7.5)));
            size_t mismatches = 0;
            for (size_t pixel = 0; pixel < 65536; ++pixel) {
                const size_t mask = pixel / 16384, within = pixel % 16384;
                mismatches += sheet[(mask * 128 + within / 128) * 1920 + frame * 128 + within % 128] != water->m_mask[pixel] * 255;
            }
            OBL_CHECK_EQ(mismatches, 0u);
        }
        auto packed = std::make_unique<obl::ui::WaterAnim>();
        obl::water_anim::Construct(*packed);
        std::vector<uint8_t> packSheet;
        OBL_REQUIRE(obl::map_pack::ReadWaterSheet(out, "data/WaterAnims/WaterAnim" + key + ".bin", packSheet, error));
        obl::water_anim::LoadSheet(*packed, packSheet.data(), heap);
        for (int frame = 0; frame < 15; ++frame) {
            const auto seconds = static_cast<float>((frame + 0.25) / 7.5);
            OBL_REQUIRE(obl::water_anim::Update(*water, seconds));
            OBL_REQUIRE(obl::water_anim::Update(*packed, seconds));
            OBL_CHECK(std::memcmp(water->m_mask, packed->m_mask, sizeof(water->m_mask)) == 0);
        }
        std::free(packed->m_data);
        std::free(water->m_data);
    }
    if (!supplied) std::filesystem::remove_all(out);
}
