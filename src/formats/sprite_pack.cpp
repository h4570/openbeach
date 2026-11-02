#include "formats/sprite_pack.h"

#include "core/pack_path.h"
#include "core/png_read.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace obl::sprite_pack {
namespace {

using json = nlohmann::json;

bool ReadFile(const std::filesystem::path& file, std::vector<uint8_t>& bytes, std::string& error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = file.string() + ": cannot open (Import the game data again)";
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

uint32_t TypeOf(const std::string& kind)
{
    if (kind == "ui") return 1;
    if (kind == "special") return 2;
    if (kind == "character") return 3;
    if (kind == "building") return 5;
    if (kind == "object_variants") return 6;
    return 0;
}

void ReadAnim(const json& a, Anim& anim)
{
    anim.name = a.at("name").get<std::string>();
    anim.plane = a.value("plane", std::string());
    anim.frames = a.at("frames").get<uint16_t>();
    anim.directions = a.at("directions").get<uint8_t>();
    anim.width = a.at("size").at(0).get<uint16_t>();
    anim.height = a.at("size").at(1).get<uint16_t>();
    anim.hotspotX = a.at("hotspot").at(0).get<int16_t>();
    anim.hotspotY = a.at("hotspot").at(1).get<int16_t>();
    anim.uniqueFrames = a.at("unique_frames").get<uint16_t>();
    anim.columns = a.at("columns").get<int>();
    anim.runLength = a.value("run_length", kDefaultRunLength);
    anim.sheets = a.at("sheets").get<std::vector<std::string>>();
    if (anim.frames == 0 || anim.directions == 0 || anim.width == 0 || anim.height == 0 || anim.uniqueFrames == 0 || anim.columns <= 0 ||
            anim.runLength < 1 || anim.runLength > 127)
        throw std::runtime_error("animation " + anim.name + ": empty or out-of-range counts");
    const int totalRows = (anim.uniqueFrames + anim.columns - 1) / anim.columns;
    anim.framesPerPage = anim.columns * a.value("rows", totalRows);
    if (anim.framesPerPage <= 0) throw std::runtime_error("animation " + anim.name + ": empty sheet page");
    const size_t pages = (anim.uniqueFrames + anim.framesPerPage - 1) / anim.framesPerPage;
    if (anim.sheets.size() != pages)
        throw std::runtime_error(
                "animation " + anim.name + ": " + std::to_string(anim.sheets.size()) + " sheets for " + std::to_string(pages) + " pages");
    const size_t logical = static_cast<size_t>(anim.frames) * anim.directions;
    if (a.contains("frame_map")) {
        anim.frameMap = a.at("frame_map").get<std::vector<uint16_t>>();
        if (anim.frameMap.size() != logical) throw std::runtime_error("animation " + anim.name + ": frame_map size");
        for (uint16_t u : anim.frameMap)
            if (u >= anim.uniqueFrames) throw std::runtime_error("animation " + anim.name + ": frame_map outside the unique frames");
    } else {
        if (logical != anim.uniqueFrames) throw std::runtime_error("animation " + anim.name + ": no frame_map, but frames differ");
        anim.frameMap.resize(logical);
        for (size_t i = 0; i < logical; ++i) anim.frameMap[i] = static_cast<uint16_t>(i);
    }
}

} // namespace

size_t PaletteSize(uint32_t type)
{
    switch (type) {
    case 3:
    case 6: return 64;
    case 1:
    case 2:
    case 5:
    case 11: return 256;
    default: throw std::runtime_error("unsupported sprite type");
    }
}

uint16_t PaletteColor(uint8_t r, uint8_t g, uint8_t b, bool is555)
{
    return static_cast<uint16_t>(is555 ? ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) : ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void EncodeFrame(std::span<const uint8_t> pixels, int width, int height, bool mask, int runLength, std::vector<uint8_t>& out)
{
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = pixels.data() + static_cast<size_t>(y) * width * 2;
        int x = 0;
        while (x < width) {
            const bool opaque = row[x * 2 + 1] != 0;
            int end = x;
            while (end < width && (row[end * 2 + 1] != 0) == opaque) ++end;
            for (int start = x; start < end; start += runLength) {
                const int length = std::min(runLength, end - start);
                out.push_back(static_cast<uint8_t>(opaque ? length : -length));
                if (opaque && !mask)
                    for (int i = start; i < start + length; ++i) out.push_back(row[i * 2]);
            }
            x = end;
        }
    }
}

bool ListSprites(const std::filesystem::path& spritesDir, std::vector<std::string>& keys, std::string& error)
{
    keys.clear();
    auto directory = spritesDir;
    if (!directory.has_filename()) directory = directory.parent_path();
    for (const auto& stem : pack_path::ListStems(directory.parent_path(), directory.filename(), ".json")) {
        // The palette modifiers share the directory (obl.palette_modifiers); every other JSON there is a sprite.
        if (stem == "palette_modifiers") continue;
        keys.push_back(stem);
    }
    if (keys.empty()) {
        error = spritesDir.string() + ": no converted sprites (Import the game data again)";
        return false;
    }
    std::sort(keys.begin(), keys.end());
    return true;
}

bool ReadSprite(
        const std::filesystem::path& spritesDir, std::string_view key, bool is555, Sprite& sprite, std::string& error, ReadMode mode)
{
    const std::filesystem::path file = pack_path::ResolveIn(spritesDir, std::string(key) + ".json");
    std::vector<uint8_t> bytes;
    if (!ReadFile(file, bytes, error)) return false;
    try {
        const json doc = json::parse(bytes.begin(), bytes.end());
        if (doc.at("format").get<std::string>() != "obl.sprite" || doc.at("version").get<int>() != 1)
            throw std::runtime_error("not an obl.sprite version 1 document");
        if (doc.at("pixels").get<std::string>() != "indexed") throw std::runtime_error("only indexed sprites are supported");
        sprite = {};
        sprite.name = doc.at("name").get<std::string>();
        sprite.type = TypeOf(doc.at("kind").get<std::string>());
        if (!sprite.type) throw std::runtime_error("unrecognized kind");
        sprite.toneGroup = doc.value("tone_group", 0);
        if (doc.contains("autotile")) {
            auto& tiles = sprite.autotile.emplace();
            if (doc.at("autotile").at("masks").size() != 256) throw std::runtime_error("autotile requires 256 masks");
            const auto masks = doc.at("autotile").at("masks").get<std::array<int, 256>>();
            for (size_t mask = 0; mask < masks.size(); ++mask) {
                if (masks[mask] < -1 || masks[mask] > 255) throw std::runtime_error("autotile mask frame outside -1..255");
                tiles.masks[mask] = static_cast<uint8_t>(masks[mask]);
            }
            tiles.full = doc.at("autotile").at("full").get<int>();
            if (tiles.full < 0 || tiles.full > 255) throw std::runtime_error("autotile full frame outside 0..255");
        }
        if (doc.contains("depth_extents")) sprite.depthExtents = doc.at("depth_extents").get<std::array<uint8_t, 4>>();
        const int colors = doc.at("palette_colors").get<int>();
        if (static_cast<size_t>(colors) != PaletteSize(sprite.type)) throw std::runtime_error("palette_colors does not fit the kind");
        sprite.paletteCount = doc.value("palette_count", 0);
        if (sprite.paletteCount < 0 || (mode == ReadMode::Metadata && sprite.paletteCount == 0))
            throw std::runtime_error("metadata requires a positive palette_count (Import the game data again)");
        if (mode == ReadMode::Palettes) {
            const std::filesystem::path paletteFile = pack_path::ResolveIn(spritesDir, doc.at("palettes").get<std::string>());
            std::vector<uint8_t> png;
            png_read::Image image;
            if (!ReadFile(paletteFile, png, error)) return false;
            if (!png_read::Decode(png, image, error)) {
                error = paletteFile.string() + ": " + error;
                return false;
            }
            if (image.channels != 3 || image.bitDepth != 8 || image.width != colors)
                throw std::runtime_error(paletteFile.string() + ": not an RGB palette of " + std::to_string(colors) + " colors");
            if (sprite.paletteCount && sprite.paletteCount != image.height)
                throw std::runtime_error("palette_count does not match the palette PNG");
            sprite.paletteCount = image.height;
            for (int p = 0; p < image.height; ++p) {
                auto& palette = sprite.palettes.emplace_back(colors);
                const uint8_t* rgb = image.bytes.data() + static_cast<size_t>(p) * colors * 3;
                for (int c = 0; c < colors; ++c) palette[c] = PaletteColor(rgb[c * 3], rgb[c * 3 + 1], rgb[c * 3 + 2], is555);
            }
        }
        for (const json& a : doc.at("animations")) ReadAnim(a, sprite.anims.emplace_back());
    } catch (const std::exception& e) {
        error = file.string() + ": " + e.what();
        return false;
    }
    return true;
}

bool EncodeFrames(const std::filesystem::path& spritesDir, const Anim& anim, int first, int end, uint32_t base, std::vector<uint8_t>& data,
        std::vector<uint32_t>& offsets, std::string& error)
{
    const size_t w = anim.width, h = anim.height;
    std::vector<uint8_t> png, frame(w * h * 2);
    png_read::Image page;
    int loaded = -1;
    for (int u = first; u < end; ++u) {
        const int pageIndex = u / anim.framesPerPage, i = u % anim.framesPerPage;
        if (pageIndex != loaded) {
            const std::filesystem::path file = pack_path::ResolveIn(spritesDir, anim.sheets[pageIndex]);
            if (!ReadFile(file, png, error)) return false;
            if (!png_read::Decode(png, page, error)) {
                error = file.string() + ": " + error;
                return false;
            }
            const int count = std::min(anim.framesPerPage, anim.uniqueFrames - pageIndex * anim.framesPerPage);
            const int rows = (count + anim.columns - 1) / anim.columns;
            if (page.channels != 2 || page.bitDepth != 8 || static_cast<size_t>(page.width) != anim.columns * w ||
                    static_cast<size_t>(page.height) != rows * h) {
                error = file.string() + ": not a gray + alpha sheet of " + std::to_string(rows) + " rows of " +
                        std::to_string(anim.columns) + " frames";
                return false;
            }
            loaded = pageIndex;
        }
        const size_t x0 = static_cast<size_t>(i % anim.columns) * w, y0 = static_cast<size_t>(i / anim.columns) * h;
        for (size_t y = 0; y < h; ++y) std::memcpy(&frame[y * w * 2], &page.bytes[((y0 + y) * page.width + x0) * 2], w * 2);
        offsets[u] = base + static_cast<uint32_t>(data.size());
        EncodeFrame(frame, anim.width, anim.height, anim.Mask(), anim.runLength, data);
    }
    return true;
}

} // namespace obl::sprite_pack
