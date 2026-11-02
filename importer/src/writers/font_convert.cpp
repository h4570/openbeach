#include "importer/src/writers/font_convert.h"
#include "core/ascii_case.h"
#include "core/code_pages.h"
#include "importer/src/writers/image_convert.h"
#include "importer/src/writers/png_write.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
namespace obl::convert {

using obl::code_pages::CodePoint;
using obl::code_pages::kUndefinedCodePoint;
namespace {
unsigned U16(std::span<const uint8_t> b, size_t at)
{
    return b[at] | (unsigned(b[at + 1]) << 8);
}
unsigned U32(std::span<const uint8_t> b, size_t at)
{
    return U16(b, at) | (U16(b, at + 2) << 16);
}
std::string Lower(std::string s)
{
    for (auto& c : s) c = ascii::Lower(c);
    return s;
}
} // namespace
bool ConvertFont(const std::string& name, std::span<const uint8_t> metrics, std::span<const uint8_t> kerning,
        std::span<const uint8_t> atlas, const json_schema::Registry& schemas, const ConvertedFileSink& sink, std::string& error)
{
    if (metrics.size() != 2056 || kerning.size() != 65536 || !U32(metrics, 0) || U32(metrics, 4) > 1) {
        error = "invalid font metrics or kerning size";
        return false;
    }
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    if (!DecodeFontAtlas(atlas, width, height, pixels, error)) return false;
    std::vector<uint8_t> indexed;
    indexed.reserve(pixels.size() * 2);
    for (auto v : pixels) {
        if (v > 3) {
            error = "unsupported font pixel bits";
            return false;
        }
        indexed.push_back(v);
        indexed.push_back(v ? 255 : 0);
    }
    const auto key = Lower(name);
    const bool polish = key.find("pol") != std::string::npos;
    std::string codes[256];
    auto glyphs = nlohmann::ordered_json::object(), pairs = nlohmann::ordered_json::array();
    for (unsigned b = 0; b < 256; ++b) {
        const unsigned cp = CodePoint(polish, b);
        // Undefined bytes have no Unicode character; do not merge several glyphs into U+FFFD.
        if (cp == kUndefinedCodePoint) continue;
        char code[8];
        std::snprintf(code, sizeof(code), "%04x", cp);
        codes[b] = code;
        const size_t at = 8 + b * 8;
        const auto l = U16(metrics, at), t = U16(metrics, at + 2), r = U16(metrics, at + 4), bottom = U16(metrics, at + 6);
        if (l > r || t > bottom || r > unsigned(width) || bottom > unsigned(height)) {
            error = "font rectangle outside atlas";
            return false;
        }
        glyphs[code] = {l, t, r, bottom};
    }
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned b = 0; b < 256; ++b) {
            const int v = kerning[a * 255 + b] < 128 ? kerning[a * 255 + b] : int(kerning[a * 255 + b]) - 256;
            if (v && !codes[a].empty() && !codes[b].empty()) pairs.push_back({codes[a], codes[b], v});
        }
    nlohmann::ordered_json doc = {{"format", "obl.font"}, {"version", 1}, {"name", key}, {"size", U32(metrics, 0)},
            {"bold", U32(metrics, 4) != 0}, {"atlas", key + ".png"}, {"glyphs", glyphs}, {"kerning", pairs}};
    if (!formats::ValidateDocument(schemas, "obl.font", doc, error)) return false;
    const auto png = EncodePng(width, height, 2, indexed);
    if (png.empty()) {
        error = "cannot encode font atlas";
        return false;
    }
    const auto text = doc.dump(2) + "\n";
    return sink("fonts/" + key + ".png", png, error) &&
            sink("fonts/" + key + ".json", std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error);
}
bool ConvertFonts(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error)
{
    files = 0;
    try {
        const auto dir = FindFile(source, "Data/Fonts");
        const auto entries = dir ? source.List(*dir) : std::nullopt;
        if (!entries) {
            error = "missing installation Data/Fonts";
            return false;
        }
        std::map<std::string, std::string> names;
        for (const SourceEntry& entry : *entries) {
            const auto& file = entry.name;
            const auto lower = Lower(file);
            const std::string suffix = "texcoord.dat";
            if (entry.directory || !lower.ends_with(suffix)) continue;
            const auto name = file.substr(0, file.size() - suffix.size());
            if (!names.emplace(Lower(name), *dir + "/" + file).second) {
                error = "ambiguous font name";
                return false;
            }
        }
        if (names.empty()) {
            error = "no font metrics found";
            return false;
        }
        ConvertedFileSink sink = [&](const std::string& name, std::span<const uint8_t> bytes, std::string& why) {
            if (!out.Write(name, bytes, why)) return false;
            ++files;
            return true;
        };
        for (const auto& [name, path] : names) {
            std::vector<uint8_t> metrics, kerning, atlas;
            const auto k = FindFile(source, "Data/Fonts/" + name + ".dat"), a = FindFile(source, "Data/Fonts/" + name + ".tiff");
            if (!k || !a) {
                error = "missing font companion: " + name;
                return false;
            }
            if (!ReadBoundedFile(source, path, 2056, metrics, error) || !ReadBoundedFile(source, *k, 65536, kerning, error) ||
                    !ReadBoundedFile(source, *a, 64 * 1024 * 1024, atlas, error) ||
                    !ConvertFont(name, metrics, kerning, atlas, schemas, sink, error)) {
                error = name + ": " + error;
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
} // namespace obl::convert
