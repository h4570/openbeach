#include "unit_test.h"
#include "importer/src/source/source.h"
#include "importer/src/readers/sprite_bank.h"
#include "formats/sprite_pack.h"
#include "temp_dir.h"
#include "game/map/path_tiles_lifecycle.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// On failure the reason goes to stderr before the check reports it.
#define OBL_REQUIRE_OK(expr)                                                                                                               \
    do {                                                                                                                                   \
        const bool ok_ = (expr);                                                                                                           \
        if (!ok_) std::fprintf(stderr, "  %s\n", error.c_str());                                                                           \
        OBL_REQUIRE(ok_);                                                                                                                  \
    } while (0)
#define OBL_CHECK_FOR(cond, what)                                                                                                          \
    do {                                                                                                                                   \
        const bool ok_ = (cond);                                                                                                           \
        if (!ok_) std::fprintf(stderr, "  %s\n", std::string(what).c_str());                                                               \
        OBL_CHECK(ok_);                                                                                                                    \
    } while (0)

namespace {

using obl::sprite_pack::EncodeFrame;

// One row of gray + alpha pixels: `opaque[i]` says whether pixel i is drawn; its gray value is i.
std::vector<uint8_t> Row(const std::vector<bool>& opaque)
{
    std::vector<uint8_t> pixels;
    for (size_t i = 0; i < opaque.size(); ++i) {
        pixels.push_back(static_cast<uint8_t>(i));
        pixels.push_back(opaque[i] ? 255 : 0);
    }
    return pixels;
}

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return text;
}

} // namespace

OBL_TEST_CASE("sprite_pack.enumeration_includes_mod_only_sprites")
{
    const obl::test::TempPath game("sprite-pack-layers");
    const auto sprites = game.path / "packs" / "base" / "sprites";
    const auto mod = game.path / "mod" / "sprites";
    std::filesystem::create_directories(sprites);
    std::filesystem::create_directories(mod);
    std::ofstream(sprites / "b.json") << "{}";
    std::ofstream(mod / "b.json") << "{}";
    std::ofstream(mod / "a.json") << "{}";
    std::ofstream(mod / "palette_modifiers.json") << "{}";
    std::ofstream(mod / "sheet.png") << "png";
    std::filesystem::create_directories(mod / "directory.json");
    std::vector<std::string> keys;
    std::string error;
    OBL_REQUIRE_OK(obl::sprite_pack::ListSprites(sprites.string() + "/", keys, error));
    OBL_CHECK(keys == std::vector<std::string>({"a", "b"}));
    std::filesystem::remove_all(sprites);
    OBL_REQUIRE_OK(obl::sprite_pack::ListSprites(sprites, keys, error));
    OBL_CHECK(keys == std::vector<std::string>({"a", "b"}));
}

OBL_TEST_CASE("sprite_pack.palette_width_and_renderer_colors")
{
    using namespace obl::sprite_pack;
    OBL_CHECK_EQ(PaletteSize(3), size_t{64});
    OBL_CHECK_EQ(PaletteSize(6), size_t{64});
    for (uint32_t type : {1u, 2u, 5u, 11u}) OBL_CHECK_EQ(PaletteSize(type), size_t{256});
    OBL_CHECK_EQ(PaletteColor(255, 255, 255, false), 0xffff);
    OBL_CHECK_EQ(PaletteColor(255, 255, 255, true), 0x7fff);
    OBL_CHECK_EQ(PaletteColor(0, 255, 0, false), 0x07e0);
    OBL_CHECK_EQ(PaletteColor(0, 255, 0, true), 0x03e0);
}

OBL_TEST_CASE("sprite_pack.metadata_without_palette_or_sheet_pngs")
{
    using namespace obl::sprite_pack;
    obl::test::TempPath tmp("obl-sprite-metadata");
    std::filesystem::create_directories(tmp.path);
    auto doc = nlohmann::json::parse(R"({
        "format":"obl.sprite", "version":1, "name":"hut", "kind":"building",
        "pixels":"indexed", "palettes":"missing.png", "palette_colors":256, "palette_count":3,
        "depth_extents":[1,2,3,4], "tone_group":2,
        "animations":[{"name":"image", "plane":"color", "frames":2, "directions":2,
            "size":[3,4], "hotspot":[-2,-3], "unique_frames":2,
            "columns":1, "rows":1, "sheets":["missing1.png","missing2.png"],
            "frame_map":[0,1,1,0], "run_length":126}]
    })");
    auto write = [&] { std::ofstream(tmp.path / "hut.json") << doc; };
    doc["autotile"] = {{"masks", std::vector<int>(256, -1)}, {"full", 42}};
    write();
    Sprite sprite;
    sprite.palettes = {{123}};
    std::string error;
    OBL_REQUIRE_OK(ReadSprite(tmp.path, "hut", false, sprite, error, ReadMode::Metadata));
    OBL_CHECK(sprite.palettes.empty());
    OBL_CHECK_EQ(sprite.paletteCount, 3);
    OBL_REQUIRE(sprite.autotile.has_value());
    OBL_CHECK_EQ(sprite.autotile->masks[0], 255);
    OBL_CHECK_EQ(sprite.autotile->full, 42);
    OBL_CHECK_EQ(sprite.type, 5u);
    OBL_CHECK_EQ(sprite.toneGroup, 2);
    OBL_CHECK(sprite.depthExtents == (std::array<uint8_t, 4>{1, 2, 3, 4}));
    OBL_REQUIRE(sprite.anims.size() == 1);
    const auto& anim = sprite.anims[0];
    OBL_CHECK_EQ(anim.frames, 2);
    OBL_CHECK_EQ(anim.directions, 2);
    OBL_CHECK_EQ(anim.width, 3);
    OBL_CHECK_EQ(anim.height, 4);
    OBL_CHECK_EQ(anim.hotspotX, -2);
    OBL_CHECK_EQ(anim.hotspotY, -3);
    OBL_CHECK_EQ(anim.runLength, 126);
    OBL_CHECK(anim.frameMap == (std::vector<uint16_t>{0, 1, 1, 0}));
    OBL_CHECK(!ReadSprite(tmp.path, "hut", false, sprite, error));
    OBL_CHECK(error.find("missing.png") != std::string::npos);
    doc["animations"][0]["frame_map"] = {0, 1, 2, 0};
    write();
    OBL_CHECK(!ReadSprite(tmp.path, "hut", false, sprite, error, ReadMode::Metadata));
    OBL_CHECK(error.find("frame_map") != std::string::npos);
    doc["animations"][0]["frame_map"] = {0, 1, 1, 0};
    doc["animations"][0]["rows"] = 0;
    write();
    OBL_CHECK(!ReadSprite(tmp.path, "hut", false, sprite, error, ReadMode::Metadata));
    OBL_CHECK(error.find("empty sheet page") != std::string::npos);
    doc["animations"][0]["rows"] = 1;
    doc.erase("palette_count");
    write();
    OBL_CHECK(!ReadSprite(tmp.path, "hut", false, sprite, error, ReadMode::Metadata));
    OBL_CHECK(error.find("palette_count") != std::string::npos);
    doc["palette_count"] = 3;
    doc["autotile"]["masks"][0] = 256;
    write();
    OBL_CHECK(!ReadSprite(tmp.path, "hut", false, sprite, error, ReadMode::Metadata));
    OBL_CHECK(error.find("autotile") != std::string::npos);
}

OBL_TEST_CASE("sprite_pack.encode_frame_splits_runs")
{
    // 300 opaque pixels then 260 transparent: maximal runs split every run length, each kind on its own.
    std::vector<bool> opaque(560, false);
    for (int i = 0; i < 300; ++i) opaque[i] = true;
    std::vector<uint8_t> out;
    EncodeFrame(Row(opaque), 560, 1, false, 127, out);
    OBL_REQUIRE(out.size() == 3 + 300 + 3);
    OBL_CHECK_EQ(out[0], 127);
    OBL_CHECK_EQ(out[1], 0);
    OBL_CHECK_EQ(out[128], 127);
    OBL_CHECK_EQ(out[129], 127);
    OBL_CHECK_EQ(out[256], 46);
    OBL_CHECK_EQ(out[257], 254);
    OBL_CHECK_EQ(static_cast<int8_t>(out[303]), -127);
    OBL_CHECK_EQ(static_cast<int8_t>(out[304]), -127);
    OBL_CHECK_EQ(static_cast<int8_t>(out[305]), -6);

    out.clear();
    EncodeFrame(Row(opaque), 560, 1, true, 126, out);
    const std::vector<uint8_t> mask = {126, 126, 48, static_cast<uint8_t>(-126), static_cast<uint8_t>(-126), static_cast<uint8_t>(-8)};
    OBL_CHECK(out == mask);
}

OBL_TEST_CASE("sprite_pack.encode_frame_rows_restart")
{
    // Two rows of two pixels, transparent then opaque: each row starts its own runs.
    const std::vector<uint8_t> pixels = {0, 0, 7, 255, 0, 0, 9, 255};
    std::vector<uint8_t> out;
    EncodeFrame(pixels, 2, 2, false, 127, out);
    const std::vector<uint8_t> expected = {static_cast<uint8_t>(-1), 1, 7, static_cast<uint8_t>(-1), 1, 9};
    OBL_CHECK(out == expected);
}

OBL_TEST_CASE("data.sprite_pack_matches_sprite_bank")
{
    // Every sprite of the installed archive against the converted pack the engine reads: the header fields of the bank, the
    // palettes in both renderer formats, the frame maps, and every unique frame re-encoded from the sheets byte for byte.
    const auto headerPath = obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Sprites/BLSprite.hdr");
    const auto dataPath = obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Sprites/BLSprite.dat");
    if (!headerPath || !dataPath) OBL_SKIP("no installed sprite archive");
    const std::filesystem::path spritesDir = std::filesystem::path(OBL_GAME_DIR) / "packs" / "base" / "sprites";
    OBL_REQUIRE(std::filesystem::exists(spritesDir));

    std::ifstream headerFile(*headerPath, std::ios::binary);
    const std::vector<uint8_t> header{std::istreambuf_iterator<char>(headerFile), std::istreambuf_iterator<char>()};
    obl::convert::SpriteBank bank;
    std::string error;
    OBL_REQUIRE_OK(obl::convert::ParseSpriteBank(header, bank, error));
    const auto lookupPath = obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/pathtiles.ble");
    OBL_REQUIRE(lookupPath.has_value());
    std::ifstream lookupFile(*lookupPath, std::ios::binary);
    std::string lookup(std::istreambuf_iterator<char>(lookupFile), {});
    for (char& c : lookup) c = static_cast<char>(static_cast<unsigned char>(c) ^ 255);
    uint8_t expectedTiles[256]{};
    obl::path_tiles::ParseLookup((lookup + ".").c_str(), static_cast<uint32_t>(lookup.size()), expectedTiles);
    std::vector<std::string> keys;
    OBL_REQUIRE_OK(obl::sprite_pack::ListSprites(spritesDir, keys, error));
    OBL_CHECK_EQ(keys.size(), bank.sprites.size());

    std::ifstream data(*dataPath, std::ios::binary);
    size_t anims = 0, frames = 0;
    std::vector<uint8_t> block, encoded;
    for (const auto& record : bank.sprites) {
        const std::string key = Lower(record.name);
        for (int is555 = 1; is555 >= 0; --is555) {
            obl::sprite_pack::Sprite sprite;
            OBL_REQUIRE_OK(obl::sprite_pack::ReadSprite(spritesDir, key, is555 != 0, sprite, error));
            OBL_CHECK_EQ(sprite.name, key);
            OBL_CHECK_EQ(sprite.type, record.type);
            if (key.starts_with("pathtile_")) {
                OBL_REQUIRE(sprite.autotile.has_value());
                OBL_CHECK(std::equal(sprite.autotile->masks.begin(), sprite.autotile->masks.end(), expectedTiles));
                OBL_CHECK_EQ(sprite.autotile->full, 67);
            } else
                OBL_CHECK(!sprite.autotile.has_value());
            const int tone = key == "fat_bloke" ? 1 : key == "fit_bloke" ? 2 : key == "gail" ? 3 : key == "shaz" ? 4 : 0;
            OBL_CHECK_EQ(sprite.toneGroup, tone);
            if (record.type == 5) OBL_CHECK(sprite.depthExtents == record.extra);
            OBL_REQUIRE(sprite.palettes.size() == record.palettes.size());
            OBL_CHECK_EQ(sprite.paletteCount, static_cast<int>(record.palettes.size()));
            for (size_t p = 0; p < record.palettes.size(); ++p) {
                const auto& rgb = record.palettes[p];
                std::vector<uint16_t> expected(record.PaletteColors());
                for (size_t c = 0; c < expected.size(); ++c)
                    expected[c] = obl::sprite_pack::PaletteColor(rgb[c * 3], rgb[c * 3 + 1], rgb[c * 3 + 2], is555 != 0);
                OBL_CHECK_FOR(sprite.palettes[p] == expected, key);
            }
            OBL_REQUIRE(sprite.anims.size() == record.anims.size());
            if (!is555) continue; // the frames do not depend on the renderer format
            for (size_t a = 0; a < record.anims.size(); ++a) {
                const auto& reference = record.anims[a];
                const auto& anim = sprite.anims[a];
                const std::string where = key + "/" + reference.name;
                OBL_CHECK_FOR(anim.name == Lower(reference.name), where);
                OBL_CHECK_FOR(
                        (anim.frames == reference.frames && anim.directions == reference.directions && anim.width == reference.width &&
                                anim.height == reference.height && anim.hotspotX == reference.hotspotX &&
                                anim.hotspotY == reference.hotspotY && anim.uniqueFrames == reference.uniqueFrames),
                        where);
                block.resize(static_cast<size_t>(reference.BlockSize()));
                data.seekg(reference.offset);
                OBL_REQUIRE(data.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size())));
                const size_t logical = static_cast<size_t>(reference.frames) * reference.directions;
                std::vector<uint16_t> indices(logical);
                std::memcpy(indices.data(), block.data(), logical * 2);
                OBL_CHECK_FOR(anim.frameMap == indices, where);
                std::vector<uint32_t> offsets(reference.uniqueFrames), packOffsets(reference.uniqueFrames);
                std::memcpy(offsets.data(), block.data() + logical * 2, offsets.size() * 4);
                const uint8_t* pixels = block.data() + logical * 2 + offsets.size() * 4;
                encoded.clear();
                OBL_REQUIRE_OK(obl::sprite_pack::EncodeFrames(spritesDir, anim, 0, anim.uniqueFrames, 0, encoded, packOffsets, error));
                for (size_t u = 0; u < offsets.size(); ++u) {
                    const size_t end = u + 1 < offsets.size() ? packOffsets[u + 1] : encoded.size();
                    const size_t bytes = end - packOffsets[u];
                    const bool same = offsets[u] + bytes <= reference.size &&
                            std::memcmp(pixels + offsets[u], encoded.data() + packOffsets[u], bytes) == 0;
                    OBL_CHECK_FOR(same, where + " frame " + std::to_string(u) + " differs from the reference runs");
                    ++frames;
                }
                ++anims;
            }
        }
    }
    OBL_CHECK_EQ(anims, 2920u);
    OBL_CHECK_EQ(frames, 584564u);
}
