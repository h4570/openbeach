#include "formats/palette_modifiers_pack.h"

#include "core/pack_path.h"
#include "core/png_read.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace obl::palette_modifiers {

void ConvertTables(const uint8_t* rgb, bool rgb565, uint16_t* tables)
{
    for (int i = 0; i < kTableCount * kTableEntries; ++i) {
        const unsigned r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        tables[i] = rgb565 ? static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3))
                           : static_cast<uint16_t>(((r & 0xf8) << 7) | ((g & 0xf8) << 2) | (b >> 3));
    }
}

bool LoadPack(const std::filesystem::path& pack, bool rgb565, uint16_t* tables, std::string& error)
{
    std::filesystem::path file = pack_path::Resolve(pack, "sprites/palette_modifiers.json");
    try {
        auto read = [](const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) throw std::runtime_error("cannot open (Import the game data again)");
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
        };
        const auto doc = nlohmann::json::parse(read(file));
        if (doc.at("format") != "obl.palette_modifiers" || doc.at("version") != 1 || doc.at("image") != "palette_modifiers.png" ||
                doc.at("colors") != kTableEntries || doc.at("tables") != kTableCount)
            throw std::runtime_error("invalid palette modifiers metadata");
        file = pack_path::Resolve(pack, "sprites/palette_modifiers.png");
        png_read::Image image;
        if (!png_read::Decode(read(file), image, error)) throw std::runtime_error(error);
        // The image is one row per table, kTableEntries colours wide, as the converter wrote it: 8-bit RGB.
        if (image.channels != 3 || image.bitDepth != 8 || image.width != kTableEntries || image.height != kTableCount)
            throw std::runtime_error("palette modifiers image must be 64 x 12 RGB8");
        ConvertTables(image.bytes.data(), rgb565, tables);
        return true;
    } catch (const std::exception& e) {
        error = file.string() + ": " + e.what();
        return false;
    }
}

} // namespace obl::palette_modifiers
