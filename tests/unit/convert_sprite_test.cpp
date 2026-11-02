// Sprites of the converter (importer/src/readers/sprite_bank.cpp, sprite_convert.cpp): the archive parser, the strict run-length
// decoder and the conversion to obl.sprite JSON with PNG palettes and sheet pages. The synthetic archive is hand-made
// with our own names and pixels; the PNGs are decoded again to prove the conversion lossless, and a synthetic cabinet
// carries it as a CD. The "data." cases convert a few sprites of the installation in run/ and of the user's CD.

#include "unit_test.h"

#include "core/json_schema.h"
#include "importer/src/writers/png_write.h"
#include "importer/src/source/cabinet.h"
#include "importer/src/source/cd_source.h"
#include "formats/schemas.h"
#include "importer/src/readers/sprite_bank.h"
#include "importer/src/writers/sprite_convert.h"
#include "og_data_dir.h"
#include "synthetic_cabinet.h"
#include "synthetic_sprites.h"
#include "temp_dir.h"

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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace obl::convert;
namespace fs = std::filesystem;

namespace {

using obl::test::Archive;
using obl::test::Block;
using Bytes = obl::test::SpriteBytes;

// Pixels of the frames above as gray + alpha.
const std::vector<uint8_t> kPixelsA = {0, 0, 0, 255, 7, 255, 9, 255, 5, 255, 6, 255, 0, 0, 0, 0};
const std::vector<uint8_t> kPixelsB = {1, 255, 2, 255, 3, 255, 4, 255, 0, 0, 0, 0, 0, 0, 0, 0};

bool Decode(const SpriteAnimRecord& anim, const std::vector<uint8_t>& block, bool mask, std::vector<uint16_t>& map,
        std::vector<std::vector<uint8_t>>& frames, std::string& error)
{
    frames.clear();
    int runLength = 0;
    return DecodeSpriteAnim(
            anim, block, mask, map, runLength,
            [&](int, std::span<const uint8_t> p, std::string&) {
                frames.emplace_back(p.begin(), p.end());
                return true;
            },
            error);
}

std::vector<uint8_t> LoadPng(const std::vector<uint8_t>& png, int channels, int& w, int& h)
{
    int n = 0;
    stbi_uc* data = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &n, channels);
    std::vector<uint8_t> out;
    if (data) out.assign(data, data + static_cast<size_t>(w) * h * channels);
    stbi_image_free(data);
    OBL_CHECK_EQ(n, channels);
    return out;
}

bool LoadSchemas(obl::json_schema::Registry& registry)
{
    std::string error;
    const bool ok = obl::formats::LoadEmbeddedSchemas(registry, error);
    if (!ok) std::printf("  %s\n", error.c_str());
    return ok;
}

} // namespace

OBL_TEST_CASE("convert.sprite_bank_parses_records")
{
    const auto [hdr, dat] = Archive();
    SpriteBank bank;
    std::string error;
    OBL_REQUIRE(ParseSpriteBank(hdr, bank, error));
    OBL_CHECK_EQ(bank.stamp, std::string("2099:01:01 00:00:00"));
    OBL_CHECK_EQ(bank.version, 4u);
    OBL_REQUIRE(bank.sprites.size() == 2);
    const SpriteRecord& walker = bank.sprites[0];
    OBL_CHECK_EQ(walker.name, std::string("Walker"));
    OBL_CHECK_EQ(walker.type, 3u);
    OBL_CHECK_EQ(walker.PaletteColors(), 64);
    OBL_REQUIRE(walker.palettes.size() == 2);
    OBL_CHECK_EQ(walker.palettes[1][5], 10);
    OBL_REQUIRE(walker.anims.size() == 1);
    OBL_CHECK_EQ(walker.anims[0].name, std::string("D_Walk"));
    OBL_CHECK_EQ(walker.anims[0].hotspotX, -2);
    OBL_CHECK_EQ(walker.anims[0].hotspotY, -1);
    OBL_CHECK_EQ(walker.anims[0].uniqueFrames, 3);
    const SpriteRecord& hut = bank.sprites[1];
    OBL_CHECK_EQ(hut.type, 5u);
    OBL_CHECK_EQ(hut.PaletteColors(), 256);
    OBL_CHECK(hut.extra == (std::array<uint8_t, 4>{1, 2, 3, 4}));
    OBL_CHECK_EQ(hut.anims.size(), 2u);

    // Every truncation, a trailing byte and an unknown type are refused.
    for (size_t length = 0; length < hdr.size(); ++length) {
        SpriteBank cut;
        OBL_CHECK(!ParseSpriteBank(std::span(hdr.data(), length), cut, error));
    }
    auto longer = hdr;
    longer.push_back(0);
    OBL_CHECK(!ParseSpriteBank(longer, bank, error));
    auto badType = hdr;
    badType[28 + 8] = 11; // the type of "Walker" (after its length byte and 7 name bytes)
    OBL_CHECK(!ParseSpriteBank(badType, bank, error));
    OBL_CHECK(error.find("unsupported sprite type 11") != std::string::npos);
}

OBL_TEST_CASE("convert.sprite_decode_runs")
{
    SpriteAnimRecord anim;
    const auto block = Block(anim);
    std::vector<uint16_t> map;
    std::vector<std::vector<uint8_t>> frames;
    std::string error;
    OBL_REQUIRE(Decode(anim, block, false, map, frames, error));
    OBL_CHECK(map == (std::vector<uint16_t>{0, 1, 2, 1}));
    OBL_REQUIRE(frames.size() == 3);
    OBL_CHECK(frames[0] == kPixelsA);
    OBL_CHECK(frames[1] == kPixelsB);
    OBL_CHECK(frames[2] == kPixelsA);

    // A mask: opaque runs carry no bytes and decode to gray 0.
    SpriteAnimRecord mask = anim;
    mask.frames = mask.directions = 1;
    mask.uniqueFrames = 1;
    mask.size = 4;
    Bytes maskBlock;
    maskBlock.U16(0).U32(0).Raw({0xff, 3, 2, 0xfe});
    OBL_REQUIRE(Decode(mask, maskBlock.b, true, map, frames, error));
    OBL_CHECK(frames[0] == (std::vector<uint8_t>{0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0}));

    // Refused: a wrong block size, a run past the width, an empty run, a frame index outside, data that ends early.
    auto shorter = block;
    shorter.pop_back();
    OBL_CHECK(!Decode(anim, shorter, false, map, frames, error));
    auto wide = block;
    wide[21] = 4; // frame A row 0 (pixel data at 20): skip 1, then four pixels
    OBL_CHECK(!Decode(anim, wide, false, map, frames, error));
    OBL_CHECK(error.find("does not fit the width") != std::string::npos);
    auto empty = block;
    empty[21] = 0;
    OBL_CHECK(!Decode(anim, empty, false, map, frames, error));
    auto index = block;
    index[2] = 3;
    OBL_CHECK(!Decode(anim, index, false, map, frames, error));
    OBL_CHECK(error.find("frame index 3") != std::string::npos);
    SpriteAnimRecord cut = mask;
    Bytes cutBlock;
    cutBlock.U16(0).U32(0).Raw({0xff, 3, 7, 7});
    OBL_CHECK(!Decode(cut, cutBlock.b, false, map, frames, error));
}

OBL_TEST_CASE("convert.sprite_png_round_trip")
{
    std::vector<uint8_t> ga = {0, 0, 7, 255, 255, 255, 1, 0};
    int w = 0, h = 0;
    const auto png = EncodePng(2, 2, 2, ga);
    OBL_CHECK(LoadPng(png, 2, w, h) == ga);
    OBL_CHECK_EQ(w, 2);
    OBL_CHECK(EncodePng(2, 2, 2, std::span(ga.data(), 7)).empty());
    OBL_CHECK(EncodePng(2, 2, 2, ga) == png); // deterministic
}

OBL_TEST_CASE("convert.sprite_to_json_and_pngs")
{
    const auto [hdr, dat] = Archive();
    SpriteBank bank;
    std::string error;
    OBL_REQUIRE(ParseSpriteBank(hdr, bank, error));
    const SpriteBlockReader reader = [&](const SpriteAnimRecord& a, std::vector<uint8_t>& block, std::string&) {
        block.assign(dat.begin() + a.offset, dat.begin() + a.offset + static_cast<ptrdiff_t>(a.BlockSize()));
        return true;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    const ConvertedFileSink sink = [&](const std::string& path, std::span<const uint8_t> bytes, std::string&) {
        files[path].assign(bytes.begin(), bytes.end());
        return true;
    };
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));

    nlohmann::ordered_json walker;
    OBL_REQUIRE(ConvertSprite(bank.sprites[0], reader, sink, walker, error));
    OBL_CHECK(obl::formats::ValidateDocument(schemas, "obl.sprite", walker, error));
    OBL_CHECK_EQ(walker["name"], "walker");
    OBL_CHECK_EQ(walker["kind"], "character");
    OBL_CHECK(!walker.contains("tone_group"));
    OBL_CHECK_EQ(walker["palette_colors"], 64);
    OBL_CHECK_EQ(walker["palette_count"], 2);
    const auto& walk = walker["animations"][0];
    OBL_CHECK_EQ(walk["name"], "d_walk");
    OBL_CHECK(walk["hotspot"] == nlohmann::ordered_json({-2, -1}));
    OBL_CHECK(walk["frame_map"] == nlohmann::ordered_json({0, 1, 2, 1}));
    OBL_CHECK(walk["sheets"] == nlohmann::ordered_json({"walker/d_walk.png"}));
    OBL_CHECK_EQ(walk["columns"], 3);
    OBL_CHECK(!walk.contains("rows"));

    int w = 0, h = 0;
    const auto palette = LoadPng(files.at("walker.palettes.png"), 3, w, h);
    OBL_CHECK_EQ(w, 64);
    OBL_CHECK_EQ(h, 2);
    OBL_CHECK(std::equal(palette.begin(), palette.begin() + 192, bank.sprites[0].palettes[0].begin()));
    OBL_CHECK(std::equal(palette.begin() + 192, palette.end(), bank.sprites[0].palettes[1].begin()));
    const auto sheet = LoadPng(files.at("walker/d_walk.png"), 2, w, h);
    OBL_REQUIRE(w == 12 && h == 2);
    for (int y = 0; y < 2; ++y) {
        OBL_CHECK(std::memcmp(&sheet[(y * 12 + 0) * 2], &kPixelsA[y * 8], 8) == 0);
        OBL_CHECK(std::memcmp(&sheet[(y * 12 + 4) * 2], &kPixelsB[y * 8], 8) == 0);
        OBL_CHECK(std::memcmp(&sheet[(y * 12 + 8) * 2], &kPixelsA[y * 8], 8) == 0);
    }

    nlohmann::ordered_json hut;
    OBL_REQUIRE(ConvertSprite(bank.sprites[1], reader, sink, hut, error));
    OBL_CHECK(obl::formats::ValidateDocument(schemas, "obl.sprite", hut, error));
    OBL_CHECK_EQ(hut["kind"], "building");
    OBL_CHECK(hut["depth_extents"] == nlohmann::ordered_json({1, 2, 3, 4}));
    OBL_CHECK_EQ(hut["animations"][0]["plane"], "color");
    OBL_CHECK_EQ(hut["animations"][1]["plane"], "pick");
    OBL_CHECK(!hut["animations"][1].contains("frame_map"));
    OBL_CHECK(
            LoadPng(files.at("hut/pick.png"), 2, w, h) == (std::vector<uint8_t>{0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0}));

    // A tone group is written as given; repeated animation names keep their order with their own sheets.
    SpriteRecord gail = bank.sprites[0];
    gail.name = "Gail";
    gail.anims.push_back(gail.anims[0]);
    nlohmann::ordered_json toned;
    OBL_REQUIRE(ConvertSprite(gail, reader, sink, toned, error, 3));
    OBL_CHECK_EQ(toned["tone_group"], 3);
    OBL_CHECK(toned["animations"][1]["sheets"] == nlohmann::ordered_json({"gail/d_walk_1.png"}));
    OBL_CHECK(obl::formats::ValidateDocument(schemas, "obl.sprite", toned, error));
    OBL_REQUIRE(ConvertSprite(gail, reader, sink, toned, error));
    OBL_CHECK(!toned.contains("tone_group"));

    // Not a content key: refused.
    SpriteRecord odd = bank.sprites[0];
    odd.name = "a-b";
    OBL_CHECK(!ConvertSprite(odd, reader, sink, toned, error));
}

OBL_TEST_CASE("convert.sprite_tone_groups_of_guest_sets")
{
    // Invented guest sets: the models their guest types name, in key order, take the four tone plans; a fifth stays untoned and
    // a model named by several sets or types counts once.
    auto installed = [](const std::string& text) {
        std::vector<uint8_t> bytes;
        for (char c : text) bytes.push_back(static_cast<uint8_t>(c) ^ 0xFF);
        return bytes;
    };
    auto guest = [](const std::string& key, const std::string& sprite) {
        return "\"GUEST_" + key + "\" \"" + key + "_GENDER\" \"M\" \"" + key + "_SPRITE\" \"" + sprite + "\"\n";
    };
    const std::vector<std::vector<uint8_t>> sets = {installed(guest("a", "Walrus") + guest("b", "eel")),
            installed(guest("c", "crab") + guest("d", "walrus") + guest("e", "puffin") + guest("f", "zebra"))};
    std::map<std::string, int> groups;
    std::string error;
    OBL_REQUIRE(ToneGroupsOfGuestSets(sets, groups, error));
    const std::map<std::string, int> expected = {{"crab", 1}, {"eel", 2}, {"puffin", 3}, {"walrus", 4}};
    OBL_CHECK(groups == expected);
    OBL_REQUIRE(ToneGroupsOfGuestSets({}, groups, error));
    OBL_CHECK(groups.empty());
    OBL_CHECK(!ToneGroupsOfGuestSets({installed("\"a_MONEY\" \"1\"\n")}, groups, error));
}

OBL_TEST_CASE("convert.sprite_sheet_pages")
{
    // Frames of 1500 x 1500: two columns and two rows fit a 4096 page, so five unique frames take two pages.
    SpriteAnimRecord anim;
    anim.name = "big";
    anim.frames = 5;
    anim.directions = 1;
    anim.width = anim.height = 1500;
    anim.uniqueFrames = 5;
    Bytes pixels;
    std::vector<uint32_t> offsets;
    for (int u = 0; u < 5; ++u) {
        offsets.push_back(static_cast<uint32_t>(pixels.b.size()));
        for (int y = 0; y < 1500; ++y) {
            // Row y of frame u: one pixel of value u + y % 7, then transparent runs to the width.
            pixels.U8(1).U8(static_cast<uint32_t>(u * 10 + y % 7));
            for (int left = 1499; left > 0; left -= 127) pixels.U8(static_cast<uint32_t>(-std::min(left, 127)) & 0xff);
        }
    }
    anim.size = static_cast<uint32_t>(pixels.b.size());
    Bytes block;
    for (int i = 0; i < 5; ++i) block.U16(static_cast<uint32_t>(i));
    for (uint32_t o : offsets) block.U32(o);
    block.b.insert(block.b.end(), pixels.b.begin(), pixels.b.end());

    SpriteRecord sprite;
    sprite.name = "pages";
    sprite.type = 1;
    sprite.palettes.resize(1);
    sprite.anims.push_back(anim);
    const SpriteBlockReader reader = [&](const SpriteAnimRecord&, std::vector<uint8_t>& b, std::string&) {
        b = block.b;
        return true;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    const ConvertedFileSink sink = [&](const std::string& path, std::span<const uint8_t> bytes, std::string&) {
        files[path].assign(bytes.begin(), bytes.end());
        return true;
    };
    nlohmann::ordered_json json;
    std::string error;
    OBL_REQUIRE(ConvertSprite(sprite, reader, sink, json, error));
    const auto& a = json["animations"][0];
    OBL_CHECK(a["sheets"] == nlohmann::ordered_json({"pages/big.png", "pages/big_p1.png"}));
    OBL_CHECK_EQ(a["columns"], 2);
    OBL_CHECK_EQ(a["rows"], 2);
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    OBL_CHECK(obl::formats::ValidateDocument(schemas, "obl.sprite", json, error));

    int w = 0, h = 0;
    const auto first = LoadPng(files.at("pages/big.png"), 2, w, h);
    OBL_CHECK(w == 3000 && h == 3000);
    // Frame 3 is at column 1, row 1 of the first page; its row 4 starts with value 30 + 4.
    OBL_CHECK_EQ(first[((1500 + 4) * 3000 + 1500) * 2], 34);
    OBL_CHECK_EQ(first[((1500 + 4) * 3000 + 1501) * 2 + 1], 0);
    const auto second = LoadPng(files.at("pages/big_p1.png"), 2, w, h);
    OBL_CHECK(w == 3000 && h == 1500);
    OBL_CHECK_EQ(second[(6 * 3000) * 2], 46); // frame 4, row 6
    OBL_CHECK_EQ(second[(6 * 3000 + 1500) * 2 + 1], 0); // the empty slot next to it stays transparent
}

OBL_TEST_CASE("convert.sprites_write_validated_files")
{
    const auto [hdr, dat] = Archive();
    const obl::test::TempPath tmp("obl_convert_sprites_test");
    const fs::path& root = tmp.path;
    fs::create_directories(root / "Data" / "Sprites");
    std::ofstream(root / "Data" / "Sprites" / "BLSprite.hdr", std::ios::binary)
            .write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
    std::ofstream(root / "Data" / "Sprites" / "BLSprite.dat", std::ios::binary)
            .write(reinterpret_cast<const char*>(dat.data()), dat.size());
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    SpriteConversionStats stats;
    std::string error;
    OBL_REQUIRE(ConvertSprites(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, "", stats, error));
    OBL_CHECK_EQ(stats.sprites, 2);
    OBL_CHECK_EQ(stats.animations, 3);
    OBL_CHECK_EQ(stats.files, 7); // two JSON, two palettes, three sheets
    OBL_CHECK(fs::exists(root / "out" / "sprites" / "walker.json"));
    OBL_CHECK(fs::exists(root / "out" / "sprites" / "hut" / "image.png"));

    SpriteConversionStats filtered;
    OBL_REQUIRE(ConvertSprites(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "one"), schemas, "HU", filtered, error));
    OBL_CHECK_EQ(filtered.sprites, 1);

    // A .dat of another revision (stamp differs) is refused.
    auto other = dat;
    other[0] = '1';
    std::ofstream(root / "Data" / "Sprites" / "BLSprite.dat", std::ios::binary)
            .write(reinterpret_cast<const char*>(other.data()), other.size());
    OBL_CHECK(!ConvertSprites(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "bad"), schemas, "", stats, error));
    OBL_CHECK(error.find("different revisions") != std::string::npos);
}

OBL_TEST_CASE("convert.sprite_autotile_records_and_truncation")
{
    auto encoded = [](const std::string& text) {
        std::vector<uint8_t> bytes;
        for (unsigned char c : text) bytes.push_back(c ^ 255);
        return bytes;
    };
    nlohmann::ordered_json table;
    std::string error;
    OBL_REQUIRE(ConvertPathAutotile(encoded("/ comment\n1 0 0 0 0 0 0 0 7\n1 0 0 0 0 0 0 0 265\n"
                                            "0 0 0 0 0 0 0 1 -1\n"),
            table, error));
    OBL_CHECK_EQ(table["masks"].size(), size_t{256});
    OBL_CHECK_EQ(table["masks"][1], 9);
    OBL_CHECK_EQ(table["masks"][128], -1);
    OBL_CHECK_EQ(table["masks"][0], 0);
    OBL_CHECK_EQ(table["full"], 67);
    OBL_CHECK(!ConvertPathAutotile(encoded("1 0 0"), table, error));
    OBL_CHECK(!ConvertPathAutotile(encoded("2 0 0 0 0 0 0 0 1"), table, error));
    OBL_CHECK(!ConvertPathAutotile(encoded("/ no records\n"), table, error));
}

OBL_TEST_CASE("convert.sprite_autotile_upgrade_preserves_pngs_and_tracks_source")
{
    const auto [hdr, dat] = Archive("PathTile_10");
    obl::test::TempPath tmp("obl-path-autotile");
    const auto root = tmp.path;
    fs::create_directories(root / "Data/Sprites");
    std::ofstream(root / "Data/Sprites/BLSprite.hdr", std::ios::binary).write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
    std::ofstream(root / "Data/Sprites/BLSprite.dat", std::ios::binary).write(reinterpret_cast<const char*>(dat.data()), dat.size());
    auto writeLookup = [&](int frame) {
        std::string text = "1 0 0 0 0 0 0 0 " + std::to_string(frame) + "\n";
        for (char& c : text) c = static_cast<char>(static_cast<unsigned char>(c) ^ 255);
        std::ofstream(root / "Data/pathtiles.ble", std::ios::binary) << text;
    };
    writeLookup(7);
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    SpriteConversionStats stats;
    std::string error;
    const auto out = root / "out";
    OBL_REQUIRE(ConvertSprites(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), schemas, "", stats, error));
    const auto spriteJson = out / "sprites/pathtile_10.json";
    const auto png = out / "sprites/pathtile_10/d_walk.png";
    const auto pngTime = fs::last_write_time(png);
    const auto stampPath = out / "sprites" / kSpriteStampName;
    std::ifstream stampFile(stampPath);
    std::string stamp(std::istreambuf_iterator<char>(stampFile), {});
    stampFile.close();
    stamp.erase(stamp.find("path tiles "));
    stamp.replace(0, stamp.find('\n'), "obl.sprite output 3");
    std::ofstream(stampPath, std::ios::binary) << stamp;
    nlohmann::ordered_json doc;
    {
        std::ifstream in(spriteJson);
        in >> doc;
    }
    doc.erase("autotile");
    std::ofstream(spriteJson) << doc;
    stats = {};
    OBL_REQUIRE(ConvertSprites(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), schemas, "", stats, error));
    OBL_CHECK_EQ(stats.files, 1);
    OBL_CHECK(fs::last_write_time(png) == pngTime);
    {
        std::ifstream in(spriteJson);
        in >> doc;
    }
    OBL_CHECK_EQ(doc["autotile"]["masks"][1], 7);
    writeLookup(9);
    stats = {};
    OBL_REQUIRE(ConvertSprites(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), schemas, "", stats, error));
    OBL_CHECK_EQ(stats.files, 1);
    OBL_CHECK(fs::last_write_time(png) == pngTime);
    {
        std::ifstream in(spriteJson);
        in >> doc;
    }
    OBL_CHECK_EQ(doc["autotile"]["masks"][1], 9);
    stats = {};
    OBL_REQUIRE(ConvertSprites(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), schemas, "", stats, error));
    OBL_CHECK(stats.upToDate);
}

OBL_TEST_CASE("convert.sprites_of_cd_equal_installation")
{
    // A CD whose cabinet holds the installation's sprite archive and path tile lookup gives the same files with the same
    // bytes, the up-to-date stamp included; the archive is read through the CD source's spool, which goes away with it.
    const auto [hdr, dat] = Archive("PathTile_10");
    std::string lookup = "1 0 0 0 0 0 0 0 7\n";
    for (char& c : lookup) c = static_cast<char>(static_cast<unsigned char>(c) ^ 255);
    const std::map<std::string, std::vector<uint8_t>> installed = {
            {"Data/Sprites/BLSprite.hdr", hdr}, {"Data/Sprites/BLSprite.dat", dat}, {"Data/pathtiles.ble", {lookup.begin(), lookup.end()}}};
    MemorySource install, cd;
    for (const auto& [path, bytes] : installed) install.Add(path, bytes);
    const obl::test::SyntheticCabinetFiles cabinet = obl::test::SyntheticCabinet(installed, "Game");
    cd.Add("data1.hdr", cabinet.hdr);
    cd.Add("data1.cab", cabinet.cab);
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    const obl::test::TempPath tmp("obl_convert_sprites_cd_synthetic");
    const fs::path cdOut = tmp.path / "cd";
    const fs::path installOut = tmp.path / "install";
    std::string error;
    {
        const auto spool = obl::convert::OpenDirectorySpool(cdOut);
        const auto cdSource = obl::convert::OpenCdSource(cd, *spool, error);
        OBL_REQUIRE(cdSource != nullptr);
        SpriteConversionStats fromCd, fromInstall;
        OBL_REQUIRE(ConvertSprites(*cdSource, *obl::convert::OpenDirectorySink(cdOut), schemas, "", fromCd, error));
        OBL_REQUIRE(ConvertSprites(install, *obl::convert::OpenDirectorySink(installOut), schemas, "", fromInstall, error));
        OBL_CHECK_EQ(fromCd.sprites, 2);
        OBL_CHECK_EQ(fromCd.sprites, fromInstall.sprites);
        OBL_CHECK_EQ(fromCd.files, fromInstall.files);
    }
    bool spoolLeft = false;
    for (const auto& entry : fs::directory_iterator(cdOut))
        spoolLeft = spoolLeft || entry.path().filename().string().rfind("spool-", 0) == 0;
    OBL_CHECK(!spoolLeft);

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(installOut / "sprites"))
        if (entry.is_regular_file()) files.push_back(fs::relative(entry.path(), installOut));
    OBL_CHECK(files.size() > 3);
    for (const auto& relative : files) {
        std::ifstream a(cdOut / relative, std::ios::binary), b(installOut / relative, std::ios::binary);
        OBL_CHECK(a.good());
        const std::string textA((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
        const std::string textB((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
        OBL_CHECK(textA == textB);
    }
    nlohmann::json tile;
    {
        std::ifstream in(cdOut / "sprites" / "pathtile_10.json");
        tile = nlohmann::json::parse(in, nullptr, false);
    }
    OBL_CHECK_EQ(tile["autotile"]["masks"][1], 7);
}

OBL_TEST_CASE("data.convert_sprite_autotile_of_cd_matches_installation")
{
    auto ogData = obl::test::FindOgData();
    if (!ogData) OBL_SKIP("the installation was not found (set OBL_OGDATA)");
    const auto installed = *ogData / "program-files/Beach Life/Data/pathtiles.ble";
    const auto cd = *ogData / "iso/data1.hdr";
    if (!fs::exists(installed) || !fs::exists(cd)) OBL_SKIP("no CD copy or installation");
    std::ifstream in(installed, std::ios::binary);
    const std::vector<uint8_t> source(std::istreambuf_iterator<char>(in), {});
    std::string error;
    auto cabinet = Cabinet::Open(cd, error);
    OBL_REQUIRE(cabinet.has_value());
    const auto* entry = cabinet->Find("Data/pathtiles.ble", error);
    OBL_REQUIRE(entry != nullptr);
    std::vector<uint8_t> extracted;
    OBL_REQUIRE(cabinet->ExtractToMemory(*entry, extracted, error));
    nlohmann::ordered_json installedTable, cdTable;
    OBL_REQUIRE(ConvertPathAutotile(source, installedTable, error));
    OBL_REQUIRE(ConvertPathAutotile(extracted, cdTable, error));
    OBL_CHECK(installedTable == cdTable);
}

OBL_TEST_CASE("data.convert_sprites_of_installation")
{
    const fs::path game(OBL_GAME_DIR);
    if (!fs::exists(game / "Data" / "Sprites" / "BLSprite.dat")) OBL_SKIP("no run/Data/Sprites");
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    // A per-run directory: parallel runs in other worktrees must not delete each other's output.
    const obl::test::TempPath tmp("obl_convert_sprites_data");
    const fs::path& out = tmp.path;
    // A plain sprite, a building with all planes and a prop character (blueherring: one frame, no tone group). The guest models
    // (shaz: about 100k frames, about 200 s in Debug) are left out; the synthetic convert.sprite_* tests cover tone groups.
    for (const char* filter : {"ui3_bi_help", "accomblk", "blueherring"}) {
        SpriteConversionStats stats;
        std::string error;
        OBL_CHECK(ConvertSprites(
                *obl::convert::OpenDirectorySource(game), *obl::convert::OpenDirectorySink(out), schemas, filter, stats, error));
        if (!error.empty()) std::printf("  %s: %s\n", filter, error.c_str());
        OBL_CHECK(stats.sprites >= 1);
    }
    nlohmann::json prop;
    {
        std::ifstream in(out / "sprites" / "blueherring.json");
        prop = nlohmann::json::parse(in, nullptr, false);
    }
    OBL_CHECK_EQ(prop["kind"], "character");
    OBL_CHECK(!prop.contains("tone_group"));
}

OBL_TEST_CASE("data.convert_sprites_of_cd_equal_installation")
{
    // The European CD's cabinets hold the installation's sprite archive byte for byte, so the sprites converted from the
    // CD root are the same files with the same bytes. The UK CD is another data revision and is only converted by hand.
    auto ogData = obl::test::FindOgData();
    if (!ogData) OBL_SKIP("the installation was not found (set OBL_OGDATA)");
    const fs::path install = *ogData / "program-files" / "Beach Life";
    const fs::path cd = *ogData / "iso";
    if (!fs::exists(cd / "data1.hdr") || !fs::exists(install / "Data" / "Sprites" / "BLSprite.dat")) OBL_SKIP("no CD copy or installation");
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    const obl::test::TempPath tmp("obl_convert_sprites_cd_equal");
    const fs::path& base = tmp.path;
    // The CD's .dat is spooled once and shared by the conversions (the spool file is removed with its CD source).
    const fs::path cdOut = base / "cd";
    const fs::path installOut = base / "install";
    std::string error;
    {
        // The sources outlive the archive that reads them.
        const auto cdRoot = obl::convert::OpenDirectorySource(cd);
        const auto spool = obl::convert::OpenDirectorySpool(cdOut);
        const auto cdSource = obl::convert::OpenCdSource(*cdRoot, *spool, error);
        OBL_REQUIRE(cdSource != nullptr);
        SpriteArchive cdArchive;
        OBL_REQUIRE(SpriteArchive::Open(*cdSource, cdArchive, error));
        // A plain sprite and a building with its planes; the guest models (shaz: 100k frames) are too slow for the suite.
        for (const char* filter : {"ui3_bi_help", "accomblk"}) {
            SpriteConversionStats fromCd, fromInstall;
            OBL_REQUIRE(cdArchive.Convert(*obl::convert::OpenDirectorySink(cdOut), schemas, filter, fromCd, error));
            OBL_REQUIRE(ConvertSprites(*obl::convert::OpenDirectorySource(install), *obl::convert::OpenDirectorySink(installOut), schemas,
                    filter, fromInstall, error));
            OBL_CHECK_EQ(fromCd.sprites, fromInstall.sprites);
            OBL_CHECK_EQ(fromCd.files, fromInstall.files);
        }
    }
    // The spool lives in the output directory only while the CD source does.
    bool spoolLeft = false;
    for (const auto& entry : fs::directory_iterator(cdOut))
        spoolLeft = spoolLeft || entry.path().filename().string().rfind("spool-", 0) == 0;
    OBL_CHECK(!spoolLeft);

    // Same file set and the same bytes in every file.
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(installOut / "sprites"))
        if (entry.is_regular_file()) files.push_back(fs::relative(entry.path(), installOut));
    OBL_CHECK(files.size() > 3);
    for (const auto& relative : files) {
        const fs::path fromCd = cdOut / relative;
        OBL_CHECK(fs::exists(fromCd));
        if (!fs::exists(fromCd)) continue;
        OBL_CHECK_EQ(fs::file_size(fromCd), fs::file_size(installOut / relative));
        std::ifstream a(fromCd, std::ios::binary), b(installOut / relative, std::ios::binary);
        const std::string textA((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
        const std::string textB((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
        OBL_CHECK(textA == textB);
    }
}
