#include "importer/src/writers/map_convert.h"

#include "core/ascii_case.h"
#include "importer/src/readers/pis.h"
#include "importer/src/readers/building_list.h"
#include "importer/src/writers/catalog_writer.h"
#include "importer/src/writers/content_key.h"
#include "importer/src/writers/png_write.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>

namespace obl::convert {
namespace {
using Json = nlohmann::ordered_json;
uint32_t U32(std::span<const uint8_t> b, size_t at)
{
    return b[at] | (uint32_t(b[at + 1]) << 8) | (uint32_t(b[at + 2]) << 16) | (uint32_t(b[at + 3]) << 24);
}
class Writer {
public:
    Writer(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
            const std::function<void(int done, int total)>& report)
        : m_source(source), m_out(out), m_schemas(schemas), m_files(files), m_error(error), m_report(report)
    {}

    bool Run()
    {
        BuildingList catalog;
        if (!ReadBuildingList(m_source, catalog, m_error)) return false;
        for (const auto& def : catalog.scenery) {
            m_definitions.insert(LowerAscii(def.key));
            if (!Alias(def.name, def.key)) return false;
        }
        const auto scenarios = FindFile(m_source, "Data/Scenario");
        const auto directories = scenarios ? m_source.List(*scenarios) : std::nullopt;
        if (!directories) return Fail("missing Data/Scenario");
        std::map<std::string, std::string> sources;
        for (const SourceEntry& directory : *directories) {
            if (!directory.directory) continue;
            const std::string folder = *scenarios + "/" + directory.name;
            const auto files = m_source.List(folder);
            if (!files) return Fail("cannot list " + m_source.Describe(folder));
            for (const SourceEntry& file : *files) {
                // Extension and stem as std::filesystem splits them: a leading dot starts the stem, not an extension.
                const size_t dot = file.name.rfind('.');
                if (dot == std::string::npos || dot == 0 || LowerAscii(file.name.substr(dot)) != ".pis") continue;
                const auto key = LowerAscii(file.name.substr(0, dot));
                if (!sources.emplace(key, folder + "/" + file.name).second) return Fail("duplicate scenario file: " + key);
            }
        }
        if (sources.empty()) return Fail("no scenario PIS files found");
        int done = 0;
        for (const auto& [key, path] : sources) {
            if (m_report) m_report(done++, static_cast<int>(sources.size()));
            std::vector<uint8_t> bytes;
            if (!ReadBoundedFile(m_source, path, 1 << 20, bytes, m_error)) return false;
            const auto entries = pis::ParseEntries(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
            auto entry = [&](const char* name) -> const pis::Entry* {
                for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                    if (ascii::EqualsNoCase(it->key.c_str(), name)) return &*it;
                return nullptr;
            };
            auto string = [&](const char* name) {
                const auto* e = entry(name);
                return e && e->type == pis::String && e->hasString ? e->string : std::string();
            };
            auto number = [&](const char* name, float fallback) {
                const auto* e = entry(name);
                return e && e->type == pis::Number && e->hasNumber ? e->number : fallback;
            };
            auto mapPath = NormalizePath(string("WORLD_MAPFILENAME"));
            const auto mapKey = LowerAscii(std::filesystem::path(mapPath).stem().string());
            if (mapKey.empty() || ContentKey(mapKey) != mapKey) return Fail("invalid map key in " + key);
            Json doc{{"format", "obl.map"}, {"version", 2}, {"size_px", {4096, 4096}}, {"cell_px", 32}, {"cells", {128, 128}},
                    {"event_area", {2884, 5054, 3951, 6962}}, {"terrain", "terrain.png"}, {"terrain_types", Json::array()},
                    {"color", "color.png"}, {"depth", "depth.png"}};
            const char* types[] = {"grass", "sand", "shallow_water", "deep_sea", "shoreline", "rock", "path"};
            for (int i = 0; i < 7; ++i) doc["terrain_types"].push_back({{"index", i}, {"type", types[i]}});
            const auto heightPath = NormalizePath(string("WORLD_MAPHEIGHTMAP"));
            // No height map named: an empty path would find the source root.
            const bool hasHeight = !heightPath.empty() && FindFile(m_source, heightPath).has_value();
            const float heightScale = hasHeight ? number("WORLD_HEIGHTMAPSCALE", NAN) : 0;
            const float plane = number("WORLD_DEPTH_PLANE_OFFSET", 0), slant = number("WORLD_DEPTH_SLANT_OFFSET", 16);
            if (!std::isfinite(heightScale) || !std::isfinite(plane) || !std::isfinite(slant) || double(plane) < INT32_MIN ||
                    double(plane) > INT32_MAX || double(slant) < INT32_MIN || double(slant) > INT32_MAX)
                return Fail("invalid map scale or depth offset in " + key);
            doc["height_scale"] = FloatNumber(heightScale);
            if (hasHeight) doc["height"] = "height.png";
            doc["depth_plane_offset"] = static_cast<int>(plane);
            doc["depth_slant_offset"] = static_cast<int>(slant);
            const auto gridPath = NormalizePath(string("WORLD_MAPGRID"));
            const auto waterPath = NormalizePath(string("WORLD_WATERANIM"));
            std::string waterKey;
            if (!waterPath.empty()) {
                auto stem = LowerAscii(std::filesystem::path(waterPath).stem().string());
                if (!stem.starts_with("wateranim") || stem.size() <= 9 ||
                        !std::all_of(stem.begin() + 9, stem.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    return Fail("invalid water animation name");
                waterKey = stem.substr(9);
                doc["water_anim"] = "water/" + waterKey;
            }
            // Sandboxes refer to the same island. Refuse conflicting effective metadata instead of silently choosing one.
            const auto signature = Json::array({doc, mapPath, gridPath, hasHeight ? heightPath : "", waterPath}).dump();
            const auto found = m_maps.find(mapKey);
            if (found != m_maps.end()) {
                if (found->second != signature) return Fail("conflicting map references: " + mapKey);
            } else {
                m_maps.emplace(mapKey, signature);
                const auto prefix = "maps/" + mapKey + "/";
                if (!Read(gridPath, bytes)) return false;
                std::vector<uint8_t> pixels;
                if (!DecodeMapTerrain(bytes, pixels, m_error)) return false;
                constexpr uint8_t kPalette[] = {
                        65, 130, 55, 235, 210, 140, 65, 175, 205, 20, 60, 145, 210, 190, 130, 90, 90, 90, 200, 40, 200};
                if (!Write(prefix + "terrain.png", EncodeIndexedPng(128, 128, pixels, kPalette))) return false;
                if (hasHeight) {
                    int width = 0, height = 0;
                    if (!Read(heightPath, bytes) || !DecodeMapHeight(bytes, width, height, pixels, m_error) ||
                            !Write(prefix + "height.png", EncodePng(width, height, 1, pixels)))
                        return false;
                }
                if (!Read(mapPath, bytes) || !Planes(bytes, prefix) || !Document(prefix + "map.json", doc)) return false;
            }
            if (!waterKey.empty() && m_water.insert(waterKey).second) {
                std::vector<uint8_t> sheet;
                if (!Read(waterPath, bytes) || !DecodeMapWater(bytes, sheet, m_error) ||
                        !Write("maps/water/" + waterKey + ".png", EncodePng(1920, 512, 1, sheet)) ||
                        !Document("maps/water/" + waterKey + ".json",
                                {{"format", "obl.water_anim"}, {"version", 1}, {"sheet", waterKey + ".png"}, {"mask_size", {128, 128}},
                                        {"frames", 15}, {"masks_per_frame", 4}, {"rate", 7.5}}))
                    return false;
            }
            auto scriptPath = NormalizePath(string("WORLD_SCRIPT"));
            if (scriptPath.size() < 4) return Fail("missing WORLD_SCRIPT in " + key);
            scriptPath.replace(scriptPath.size() - 4, 4, ".ble");
            if (!Read(scriptPath, bytes)) return false;
            for (auto& byte : bytes) byte ^= 0xff;
            if (!ScriptAliases(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) return false;
        }
        if (m_report) m_report(static_cast<int>(sources.size()), static_cast<int>(sources.size()));
        Json props = Json::object();
        for (const auto& [name, definitions] : m_props) props[name] = definitions;
        return Document("maps/props.json", {{"format", "obl.map_props"}, {"version", 1}, {"props", props}});
    }

private:
    static std::string NormalizePath(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return LowerAscii(path);
    }
    bool Fail(const std::string& message)
    {
        m_error = message;
        return false;
    }
    bool Read(const std::string& name, std::vector<uint8_t>& bytes)
    {
        const auto path = FindFile(m_source, name);
        if (!path) return Fail("missing map source: " + name);
        if (!ReadBoundedFile(m_source, *path, 64 << 20, bytes, m_error)) return false;
        return true;
    }
    bool Write(const std::string& name, std::span<const uint8_t> bytes)
    {
        if (bytes.empty()) return Fail("empty map output: " + name);
        if (!m_out.Write(name, bytes, m_error)) return false;
        ++m_files;
        return true;
    }
    bool Document(const std::string& name, const Json& doc)
    {
        if (!formats::ValidateDocument(m_schemas, doc["format"].get<std::string>(), doc, m_error)) return false;
        const auto text = doc.dump(2) + "\n";
        return Write(name, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    }
    bool Planes(std::span<const uint8_t> bytes, const std::string& prefix)
    {
        if (bytes.size() < 8192) return Fail("truncated BIZ tables");
        std::vector<uint8_t> rgb(4096 * 4096 * 3);
        std::vector<uint16_t> depth(4096 * 4096), block;
        for (size_t y = 0; y < 32; ++y)
            for (size_t x = 0; x < 32; ++x) {
                const size_t index = y * 32 + x, offset = U32(bytes, index * 4), size = U32(bytes, 4096 + index * 4);
                if (offset < 8192 || offset > bytes.size() || size > bytes.size() - offset) return Fail("invalid BIZ block range");
                if (!DecodeMapBlock(bytes.subspan(offset, size), block, m_error)) return false;
                for (size_t by = 0; by < 128; ++by)
                    for (size_t bx = 0; bx < 128; ++bx) {
                        const size_t src = by * 128 + bx, dst = (y * 128 + by) * 4096 + x * 128 + bx;
                        const auto color = block[src];
                        const auto r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
                        rgb[dst * 3] = static_cast<uint8_t>((r << 3) | (r >> 2));
                        rgb[dst * 3 + 1] = static_cast<uint8_t>((g << 2) | (g >> 4));
                        rgb[dst * 3 + 2] = static_cast<uint8_t>((b << 3) | (b >> 2));
                        depth[dst] = block[16384 + src];
                    }
            }
        return Write(prefix + "color.png", EncodePng(4096, 4096, 3, rgb)) &&
                Write(prefix + "depth.png", EncodeGray16Png(4096, 4096, depth));
    }
    bool Alias(std::string_view name, std::string_view definition)
    {
        const auto key = ContentKey(name), def = LowerAscii(definition);
        if (key.empty() || def.empty()) return Fail("empty prop key");
        auto& list = m_props[key];
        if (std::find(list.begin(), list.end(), def) == list.end()) list.push_back(def);
        return true;
    }
    bool ScriptAliases(std::string_view script)
    {
        std::vector<MapPropAlias> aliases;
        if (!ParseMapPropAliases(script, aliases, m_error)) return false;
        for (const auto& alias : aliases) {
            if (!m_definitions.contains(LowerAscii(alias.definition)))
                return Fail("script names unrecognized scenery: " + alias.definition);
            if (!Alias(alias.name, alias.definition)) return false;
        }
        return true;
    }
    const ByteSource& m_source;
    PackSink& m_out;
    const json_schema::Registry& m_schemas;
    int& m_files;
    std::string& m_error;
    const std::function<void(int done, int total)>& m_report;
    std::map<std::string, std::string> m_maps;
    std::set<std::string> m_water, m_definitions;
    std::map<std::string, std::vector<std::string>> m_props;
};
} // namespace

bool ConvertMaps(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
        const std::function<void(int done, int total)>& report)
{
    files = 0;
    try {
        return Writer(source, out, schemas, files, error, report).Run();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
} // namespace obl::convert
