#include "font_pack.h"
#include "core/ascii_case.h"
#include "core/code_pages.h"
#include "core/pack_path.h"
#include "core/png_read.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace obl::font_pack {
bool ReadFont(const std::filesystem::path& pack, std::string_view name, Font& font, std::string& error)
{
    std::string id(name);
    for (char& c : id) {
        c = ascii::Lower(c);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            error = "invalid font name";
            return false;
        }
    }
    auto file = pack_path::Resolve(pack, "fonts/" + id + ".json");
    try {
        auto read = [&](const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) throw std::runtime_error("cannot open (Import the game data again)");
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
        };
        auto doc = nlohmann::json::parse(read(file));
        if (doc.at("format") != "obl.font" || doc.at("version") != 1 || doc.at("name") != id || doc.at("atlas") != id + ".png")
            throw std::runtime_error("invalid font metadata");
        Font result;
        result.size = doc.at("size").get<uint32_t>();
        result.bold = doc.at("bold").get<bool>();
        if (!result.size) throw std::runtime_error("invalid font size");
        std::array<std::string, 256> codes;
        const bool polish = id.find("pol") != std::string::npos;
        for (unsigned b = 0; b < 256; ++b) {
            unsigned cp = code_pages::CodePoint(polish, b);
            if (cp == code_pages::kUndefinedCodePoint) continue;
            char key[8];
            std::snprintf(key, sizeof(key), "%04x", cp);
            codes[b] = key;
            const auto& rect = doc.at("glyphs").at(key);
            if (!rect.is_array() || rect.size() != 4) throw std::runtime_error("invalid glyph rectangle");
            auto component = [&](int i) {
                int v = rect.at(i).get<int>();
                if (v < 0 || v > 65535) throw std::runtime_error("invalid glyph coordinate");
                return static_cast<uint16_t>(v);
            };
            result.glyphs[b] = {component(0), component(1), component(2), component(3)};
        }
        // Undefined code-page bytes cannot be emitted by converted text; give them the missing glyph.
        for (unsigned b = 0; b < 256; ++b)
            if (codes[b].empty()) result.glyphs[b] = result.glyphs['?'];
        for (const auto& pair : doc.at("kerning")) {
            if (!pair.is_array() || pair.size() != 3) throw std::runtime_error("invalid kerning pair");
            auto byte = [&](const std::string& key) {
                for (unsigned b = 0; b < 256; ++b)
                    if (!codes[b].empty() && codes[b] == key) return b;
                throw std::runtime_error("kerning glyph missing from the font");
            };
            unsigned a = byte(pair[0].get<std::string>()), b = byte(pair[1].get<std::string>());
            int delta = pair[2].get<int>();
            if (delta < -128 || delta > 127) throw std::runtime_error("invalid kerning delta");
            result.kerning[a * 255 + b] = static_cast<int8_t>(delta);
        }
        file = pack_path::Resolve(pack, "fonts/" + id + ".png");
        png_read::Image image;
        if (!png_read::Decode(read(file), image, error)) throw std::runtime_error(error);
        if (image.channels != 2 || image.bitDepth != 8) throw std::runtime_error("font atlas must be gray+alpha8");
        result.width = image.width;
        result.height = image.height;
        for (size_t i = 0; i < image.bytes.size(); i += 2) {
            uint8_t v = image.bytes[i];
            if (v > 3 || image.bytes[i + 1] != (v ? 255 : 0)) throw std::runtime_error("invalid font pixel bits");
            result.pixels.push_back(v);
        }
        for (const auto& g : result.glyphs)
            if (g.left > g.right || g.top > g.bottom || g.right > result.width || g.bottom > result.height)
                throw std::runtime_error("glyph outside atlas");
        font = std::move(result);
        return true;
    } catch (const std::exception& e) {
        error = file.string() + ": " + e.what();
        return false;
    }
}
} // namespace obl::font_pack
