#include "importer/src/writers/image_convert.h"

#include "formats/palette_modifiers_pack.h"
#include "core/image_decode.h"
#include "importer/src/writers/png_write.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace obl::convert {
namespace {

uint16_t U16(std::span<const uint8_t> b, size_t at)
{
    return b[at] | (b[at + 1] << 8);
}
uint32_t U32(std::span<const uint8_t> b, size_t at)
{
    return U16(b, at) | (uint32_t(U16(b, at + 2)) << 16);
}
bool Has(std::span<const uint8_t> b, size_t at, size_t size)
{
    return at <= b.size() && size <= b.size() - at;
}
bool Fail(std::string& error, const char* message)
{
    error = message;
    return false;
}

bool JsonFile(const json_schema::Registry& schemas, const nlohmann::ordered_json& doc, const std::string& path,
        const ConvertedFileSink& sink, std::string& error)
{
    if (!formats::ValidateDocument(schemas, doc["format"].get<std::string>(), doc, error)) return false;
    const auto text = doc.dump(2) + "\n";
    return sink(path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error);
}

bool Tga(std::span<const uint8_t> b, bool cursor, int& width, int& height, std::vector<uint8_t>& pixels, std::string& error)
{
    if (b.size() < 18) return Fail(error, "truncated TGA header");
    const int type = b[2], depth = b[16];
    const bool indexed = type == 1 || type == 9, rle = type == 9 || type == 10;
    if ((!indexed && type != 2 && type != 10) || b[1] != (indexed ? 1 : 0) ||
            (indexed ? depth != 8 || b[7] != 24 : depth != 16 && depth != 24 && depth != 32) || (b[17] & 0xc0) || (cursor && !indexed))
        return Fail(error, "unsupported TGA layout");
    width = U16(b, 12);
    height = U16(b, 14);
    if (!width || !height || uint64_t(width) * height > 16777216) return Fail(error, "invalid TGA dimensions");
    const int first = U16(b, 3), colors = U16(b, 5);
    size_t at = 18 + b[0];
    std::vector<uint8_t> palette;
    if (indexed) {
        if (!colors || first + colors > 256 || !Has(b, at, colors * 3)) return Fail(error, "invalid TGA palette");
        palette.assign(b.begin() + at, b.begin() + at + colors * 3);
        at += colors * 3;
    }
    pixels.resize(size_t(width) * height * 4);
    const size_t count = size_t(width) * height, stride = depth / 8;
    size_t written = 0;
    auto pixel = [&](size_t pos) {
        size_t x = written % width, y = written / width;
        if (!(b[17] & 0x20)) y = height - 1 - y;
        if (b[17] & 0x10) x = width - 1 - x;
        auto* dst = pixels.data() + (y * width + x) * 4;
        if (indexed) {
            const int index = b[pos];
            if (index < first || index >= first + colors) return false;
            const auto* color = palette.data() + (index - first) * 3;
            dst[0] = color[2];
            dst[1] = color[1];
            dst[2] = color[0];
            dst[3] = cursor && index == 0 ? 0 : 255;
        } else if (depth == 16) {
            // TGA true color is RGB555; bit replication keeps a round trip exact.
            const auto color = U16(b, pos);
            for (int c = 0; c < 3; ++c) {
                const int v = (color >> (10 - c * 5)) & 31;
                dst[c] = uint8_t((v << 3) | (v >> 2));
            }
            dst[3] = (b[17] & 15) ? ((color & 0x8000) ? 255 : 0) : 255;
        } else {
            dst[0] = b[pos + 2];
            dst[1] = b[pos + 1];
            dst[2] = b[pos];
            dst[3] = depth == 32 ? b[pos + 3] : 255;
        }
        ++written;
        return true;
    };
    while (written < count) {
        size_t run = 1;
        bool repeat = false;
        if (rle) {
            if (!Has(b, at, 1)) return Fail(error, "truncated TGA packet");
            const auto packet = b[at++];
            run = (packet & 127) + 1;
            repeat = (packet & 128) != 0;
        }
        if (run > count - written || !Has(b, at, stride * (repeat ? 1 : run))) return Fail(error, "invalid TGA run");
        for (size_t i = 0; i < run; ++i)
            if (!pixel(at + (repeat ? 0 : i * stride))) return Fail(error, "TGA index outside palette");
        at += stride * (repeat ? 1 : run);
    }
    return true;
}

bool IndexedTiff(std::span<const uint8_t> b, int& width, int& height, std::vector<uint8_t>& pixels, std::string& error, bool font = false)
{
    if (!Has(b, 0, 8) || U16(b, 0) != 0x4949 || U16(b, 2) != 42) return Fail(error, "unsupported TIFF header");
    const size_t ifd = U32(b, 4);
    if (!Has(b, ifd, 2)) return Fail(error, "truncated TIFF IFD");
    const int entries = U16(b, ifd);
    if (!Has(b, ifd + 2, size_t(entries) * 12 + 4)) return Fail(error, "truncated TIFF tags");
    std::map<int, std::vector<uint32_t>> tags;
    const std::set<int> used = {256, 257, 258, 259, 262, 273, 274, 277, 278, 279, 284};
    for (int i = 0; i < entries; ++i) {
        const size_t e = ifd + 2 + i * 12;
        const int tag = U16(b, e);
        if (tag == 322 || tag == 323 || tag == 324 || tag == 325) return Fail(error, "tiled indexed TIFF is unsupported");
        if (!used.contains(tag)) continue;
        const int type = U16(b, e + 2);
        const size_t n = U32(b, e + 4), stride = type == 3 ? 2 : 4;
        if ((type != 3 && type != 4) || !n || n > 65536 || tags.contains(tag)) return Fail(error, "invalid TIFF tag");
        const size_t at = n * stride <= 4 ? e + 8 : U32(b, e + 8);
        if (!Has(b, at, n * stride)) return Fail(error, "truncated TIFF tag values");
        auto& values = tags[tag];
        for (size_t j = 0; j < n; ++j) values.push_back(stride == 2 ? U16(b, at + j * stride) : U32(b, at + j * stride));
    }
    auto scalar = [&](int tag, uint32_t fallback) {
        auto it = tags.find(tag);
        return it == tags.end() ? fallback : it->second.size() == 1 ? it->second[0] : 0u;
    };
    const uint32_t bits = scalar(258, 0);
    const uint32_t w = scalar(256, 0), h = scalar(257, 0), rows = scalar(278, h), compression = scalar(259, 1);
    if (!w || !h || w > 4096 || h > 4096 || !rows || (font ? bits != 4 && bits != 8 : bits != 8) || scalar(277, 1) != 1 ||
            scalar(274, 1) != 1 || scalar(284, 1) != 1 || scalar(262, 3) != 3 || (compression != 1 && compression != 32773))
        return Fail(error, "unsupported indexed TIFF layout");
    const size_t strips = (size_t(h) + rows - 1) / rows;
    if (!tags.contains(273) || !tags.contains(279) || tags[273].size() != strips || tags[279].size() != strips)
        return Fail(error, "invalid TIFF strips");
    if (bits == 4 && w % 2) return Fail(error, "odd font TIFF width");
    width = int(w);
    height = int(h);
    pixels.clear();
    pixels.reserve(size_t(w) * h);
    for (size_t s = 0; s < strips; ++s) {
        const size_t at = tags[273][s], size = tags[279][s],
                     expected = size_t(w / (bits == 4 ? 2 : 1)) * std::min(uint32_t(rows), h - uint32_t(s * rows));
        if (!Has(b, at, size)) return Fail(error, "truncated TIFF strip");
        const size_t start = pixels.size();
        if (compression == 1) {
            if (size != expected) return Fail(error, "invalid TIFF strip size");
            pixels.insert(pixels.end(), b.begin() + at, b.begin() + at + size);
        } else {
            size_t pos = at;
            while (pos < at + size) {
                const int control = b[pos++];
                if (control == 128) continue;
                const size_t run = control < 128 ? control + 1 : 257 - control;
                if (run > expected - (pixels.size() - start) || !Has(b, pos, control < 128 ? run : 1) ||
                        (control < 128 ? run : 1) > at + size - pos)
                    return Fail(error, "invalid TIFF PackBits run");
                if (control < 128) {
                    pixels.insert(pixels.end(), b.begin() + pos, b.begin() + pos + run);
                    pos += run;
                } else
                    pixels.insert(pixels.end(), run, b[pos++]);
            }
            if (pixels.size() - start != expected) return Fail(error, "short TIFF PackBits strip");
        }
    }
    if (bits == 4) {
        std::vector<uint8_t> unpacked;
        for (auto v : pixels) {
            unpacked.push_back(v >> 4);
            unpacked.push_back(v & 15);
        }
        pixels = std::move(unpacked);
    }
    return true;
}
} // namespace

bool DecodeFontAtlas(std::span<const uint8_t> bytes, int& width, int& height, std::vector<uint8_t>& pixels, std::string& error)
{
    return IndexedTiff(bytes, width, height, pixels, error, true);
}

bool ConvertPaletteModifiers(
        std::span<const uint8_t> bytes, const json_schema::Registry& schemas, const ConvertedFileSink& sink, std::string& error)
{
    if (bytes.size() != palette_modifiers::kFileBytes) return Fail(error, "PaletteModifiers.bin must contain 2304 bytes");
    // Only the user's tables: which entries the engine mixes between them is the engine's own toning rule (palette_modifiers.h).
    const nlohmann::ordered_json doc = {
            {"format", "obl.palette_modifiers"}, {"version", 1}, {"image", "palette_modifiers.png"}, {"colors", 64}, {"tables", 12}};
    if (!formats::ValidateDocument(schemas, "obl.palette_modifiers", doc, error)) return false;
    const auto png = EncodePng(64, 12, 3, bytes);
    if (png.empty()) return Fail(error, "cannot encode palette modifiers");
    return sink("sprites/palette_modifiers.png", png, error) && JsonFile(schemas, doc, "sprites/palette_modifiers.json", sink, error);
}

bool ConvertAssetImage(std::span<const uint8_t> bytes, bool haze, bool cursor, std::vector<uint8_t>& png, std::string& error)
{
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    if (haze ? !IndexedTiff(bytes, width, height, pixels, error) : !Tga(bytes, cursor, width, height, pixels, error)) return false;
    auto encoded = EncodePng(width, height, haze ? 1 : 4, pixels);
    if (encoded.empty()) return Fail(error, "cannot encode image");
    png = std::move(encoded);
    return true;
}

bool ConvertImages(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error)
{
    files = 0;
    try {
        ConvertedFileSink sink = [&](const std::string& name, std::span<const uint8_t> bytes, std::string& why) {
            if (!out.Write(name, bytes, why)) return false;
            ++files;
            return true;
        };
        auto read = [&](const std::string& name, std::vector<uint8_t>& bytes) {
            auto path = FindFile(source, name);
            if (!path) {
                error = "missing installation file: " + name;
                return false;
            }
            return ReadBoundedFile(source, *path, 64 * 1024 * 1024, bytes, error);
        };
        auto enginePng = [&](std::span<const uint8_t> bytes, bool haze, bool cursor, std::vector<uint8_t>& png, std::string& error) {
            if (!ConvertAssetImage(bytes, haze, cursor, png, error)) return false;
            image::Image decoded;
            if (!(haze ? image::DecodeTiff(bytes.data(), bytes.size(), decoded) : image::DecodeTga(bytes.data(), bytes.size(), decoded)))
                return Fail(error, "unsupported engine image layout");
            png = decoded.hasPalette() ? EncodeIndexedPng(decoded.width, decoded.height, decoded.pixels, decoded.palette)
                                       : EncodePng(decoded.width, decoded.height, decoded.bpp / 8, decoded.pixels);
            return !png.empty() || Fail(error, "cannot encode engine image");
        };
        std::vector<uint8_t> bytes, png;
        if (!read("Data/Sprites/PaletteModifiers.bin", bytes) || !ConvertPaletteModifiers(bytes, schemas, sink, error)) return false;
        if (!read("Data/cursor.tga", bytes) || !enginePng(bytes, false, true, png, error) || !sink("images/cursor.png", png, error))
            return false;
        // The cursor atlas is sliced into 32 x 32 cells; its draw origin has no hotspot offset.
        const nlohmann::ordered_json cursor = {{"format", "obl.image"}, {"version", 1}, {"image", "cursor.png"}, {"hotspot", {0, 0}}};
        if (!JsonFile(schemas, cursor, "images/cursor.json", sink, error)) return false;
        const auto screens = FindFile(source, "Data/Screens");
        const auto entries = screens ? source.List(*screens) : std::nullopt;
        if (!entries) return Fail(error, "missing Data/Screens directory");
        std::map<std::string, std::string> sources;
        for (const SourceEntry& entry : *entries) {
            // Extension and stem as std::filesystem splits them: a leading dot starts the stem, not an extension.
            const size_t dot = entry.name.rfind('.');
            if (dot == std::string::npos || dot == 0 || LowerAscii(entry.name.substr(dot)) != ".tga") continue;
            const auto key = LowerAscii(entry.name.substr(0, dot));
            if (key.empty() ||
                    !std::all_of(
                            key.begin(), key.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }) ||
                    !sources.emplace(key, *screens + "/" + entry.name).second)
                return Fail(error, "invalid or duplicate screen key");
        }
        if (sources.empty()) return Fail(error, "no TGA screens found");
        for (const auto& [key, path] : sources) {
            if (!ReadBoundedFile(source, path, 64 * 1024 * 1024, bytes, error) || !enginePng(bytes, false, false, png, error) ||
                    !sink("images/screens/" + key + ".png", png, error)) {
                error = source.Describe(path) + ": " + error;
                return false;
            }
        }
        for (int i = 0; i < 6; ++i) {
            const auto name = "Data/Haze/wibble_0" + std::to_string(i) + ".tif";
            if (!read(name, bytes) || !enginePng(bytes, true, false, png, error) ||
                    !sink("images/haze/" + std::to_string(i) + ".png", png, error)) {
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
