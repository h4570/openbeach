#include "importer/src/writers/map_convert.h"

#include "core/ascii_case.h"
#include "core/sha256.h"
#include "importer/src/readers/pis.h"
#include "importer/src/readers/building_list.h"
#include "importer/src/writers/catalog_writer.h"
#include "importer/src/writers/content_key.h"
#include "importer/src/writers/ordered_pool.h"
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
// Source paths in a .pis use either slash and any case.
std::string NormalizeSourcePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return LowerAscii(path);
}
class Writer {
public:
    Writer(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
            const std::function<void(int done, int total)>& report, int jobs)
        : m_source(source), m_out(out), m_schemas(schemas), m_files(files), m_error(error), m_report(report), m_jobs(jobs)
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
        Plan(sources);
        return Execute();
    }

    // Every file Run has written, for the stamp.
    const std::set<std::string>& Written() const { return m_written; }

private:
    // One step of the conversion, in the order a serial run does them. Image steps (`work`) read, decode and encode on a
    // worker and only produce bytes; everything with a side effect on the pack or on the counters (`commit`) runs on the
    // calling thread when its turn comes, so the files, the first error and the progress are those of a serial run.
    struct Outputs {
        std::vector<std::pair<std::string, std::vector<uint8_t>>> files; // those made before `error`, in write order
        std::string error;
    };
    struct Step {
        uint64_t cost = 0; // memory a worker holds for the step, in bytes
        std::function<Outputs()> work;
        std::function<bool()> commit; // without `work`: the step itself; with it: unused, the outputs are written
    };

    // Memory estimates (bytes) of the image steps: the raw planes, their filtered copy and the compressed bytes.
    static constexpr uint64_t kTerrainCost = 4 << 20, kHeightCost = 96 << 20, kPlanesCost = 256 << 20, kWaterCost = 16 << 20;
    static constexpr uint64_t kMemoryBudget = 1ull << 30;

    void AddStep(std::function<bool()> commit) { m_steps.push_back({0, {}, std::move(commit)}); }
    void AddImage(uint64_t cost, std::function<Outputs()> work) { m_steps.push_back({cost, std::move(work), {}}); }
    // The error a serial run reports at this point, after the steps before it.
    void AddFailure(std::string message)
    {
        AddStep([this, message = std::move(message)] { return Fail(message); });
    }

    static bool ReadSource(const ByteSource& source, const std::string& name, std::vector<uint8_t>& bytes, std::string& error)
    {
        const auto path = FindFile(source, name);
        if (!path) {
            error = "missing map source: " + name;
            return false;
        }
        return ReadBoundedFile(source, *path, 64 << 20, bytes, error);
    }

    bool Execute()
    {
        const std::function<uint64_t(size_t)> cost = [&](size_t i) { return m_steps[i].cost; };
        const std::function<Outputs(size_t)> produce = [&](size_t i) { return m_steps[i].work ? m_steps[i].work() : Outputs{}; };
        const std::function<bool(size_t, Outputs&)> consume = [&](size_t i, Outputs& outputs) {
            if (!m_steps[i].work) return m_steps[i].commit();
            for (const auto& [name, bytes] : outputs.files)
                if (!Write(name, bytes)) return false;
            if (outputs.error.empty()) return true;
            return Fail(outputs.error);
        };
        return RunOrdered<Outputs>(m_steps.size(), m_jobs, kMemoryBudget, cost, produce, consume);
    }

    // Reads the scenarios in key order, the cheap parts (PIS entries, metadata, scripts) now and the images as steps.
    void Plan(const std::map<std::string, std::string>& sources)
    {
        const int total = static_cast<int>(sources.size());
        int done = 0;
        for (const auto& [key, path] : sources) {
            if (m_report) AddStep([this, done, total] { return m_report(done, total), true; });
            ++done;
            std::vector<uint8_t> bytes;
            if (!ReadBoundedFile(m_source, path, 1 << 20, bytes, m_error)) return AddFailure(m_error);
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
            if (mapKey.empty() || ContentKey(mapKey) != mapKey) return AddFailure("invalid map key in " + key);
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
                return AddFailure("invalid map scale or depth offset in " + key);
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
                    return AddFailure("invalid water animation name");
                waterKey = stem.substr(9);
                doc["water_anim"] = "water/" + waterKey;
            }
            // Sandboxes refer to the same island. Refuse conflicting effective metadata instead of silently choosing one.
            const auto signature = Json::array({doc, mapPath, gridPath, hasHeight ? heightPath : "", waterPath}).dump();
            const auto found = m_maps.find(mapKey);
            if (found != m_maps.end()) {
                if (found->second != signature) return AddFailure("conflicting map references: " + mapKey);
            } else {
                m_maps.emplace(mapKey, signature);
                const std::string prefix = "maps/" + mapKey + "/";
                AddImage(kTerrainCost, [this, gridPath, prefix] { return TerrainImage(m_source, gridPath, prefix); });
                if (hasHeight) AddImage(kHeightCost, [this, heightPath, prefix] { return HeightImage(m_source, heightPath, prefix); });
                AddImage(kPlanesCost, [this, mapPath, prefix] { return PlanesImages(m_source, mapPath, prefix); });
                AddStep([this, prefix, doc] { return Document(prefix + "map.json", doc); });
            }
            if (!waterKey.empty() && m_water.insert(waterKey).second) {
                AddImage(kWaterCost, [this, waterPath, waterKey] { return WaterImage(m_source, waterPath, waterKey); });
                AddStep([this, waterKey] {
                    return Document("maps/water/" + waterKey + ".json",
                            {{"format", "obl.water_anim"}, {"version", 1}, {"sheet", waterKey + ".png"}, {"mask_size", {128, 128}},
                                    {"frames", 15}, {"masks_per_frame", 4}, {"rate", 7.5}});
                });
            }
            auto scriptPath = NormalizePath(string("WORLD_SCRIPT"));
            if (scriptPath.size() < 4) return AddFailure("missing WORLD_SCRIPT in " + key);
            scriptPath.replace(scriptPath.size() - 4, 4, ".ble");
            std::string error;
            if (!ReadSource(m_source, scriptPath, bytes, error)) return AddFailure(error);
            for (auto& byte : bytes) byte ^= 0xff;
            if (!ScriptAliases(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) return AddFailure(m_error);
        }
        if (m_report) AddStep([this, total] { return m_report(total, total), true; });
        AddStep([this] {
            Json props = Json::object();
            for (const auto& [name, definitions] : m_props) props[name] = definitions;
            return Document("maps/props.json", {{"format", "obl.map_props"}, {"version", 1}, {"props", props}});
        });
    }

    static Outputs TerrainImage(const ByteSource& source, const std::string& gridPath, const std::string& prefix)
    {
        Outputs outputs;
        std::vector<uint8_t> bytes, pixels;
        if (!ReadSource(source, gridPath, bytes, outputs.error) || !DecodeMapTerrain(bytes, pixels, outputs.error)) return outputs;
        constexpr uint8_t kPalette[] = {65, 130, 55, 235, 210, 140, 65, 175, 205, 20, 60, 145, 210, 190, 130, 90, 90, 90, 200, 40, 200};
        outputs.files.emplace_back(prefix + "terrain.png", EncodeIndexedPng(128, 128, pixels, kPalette));
        return outputs;
    }
    static Outputs HeightImage(const ByteSource& source, const std::string& heightPath, const std::string& prefix)
    {
        Outputs outputs;
        std::vector<uint8_t> bytes, pixels;
        int width = 0, height = 0;
        if (!ReadSource(source, heightPath, bytes, outputs.error) || !DecodeMapHeight(bytes, width, height, pixels, outputs.error))
            return outputs;
        outputs.files.emplace_back(prefix + "height.png", EncodePng(width, height, 1, pixels));
        return outputs;
    }
    static Outputs WaterImage(const ByteSource& source, const std::string& waterPath, const std::string& waterKey)
    {
        Outputs outputs;
        std::vector<uint8_t> bytes, sheet;
        if (!ReadSource(source, waterPath, bytes, outputs.error) || !DecodeMapWater(bytes, sheet, outputs.error)) return outputs;
        outputs.files.emplace_back("maps/water/" + waterKey + ".png", EncodePng(1920, 512, 1, sheet));
        return outputs;
    }
    static std::string NormalizePath(std::string path) { return NormalizeSourcePath(std::move(path)); }
    bool Fail(const std::string& message)
    {
        m_error = message;
        return false;
    }
    bool Write(const std::string& name, std::span<const uint8_t> bytes)
    {
        if (bytes.empty()) return Fail("empty map output: " + name);
        if (!m_out.Write(name, bytes, m_error)) return false;
        ++m_files;
        m_written.insert(name);
        return true;
    }
    bool Document(const std::string& name, const Json& doc)
    {
        if (!formats::ValidateDocument(m_schemas, doc["format"].get<std::string>(), doc, m_error)) return false;
        const auto text = doc.dump(2) + "\n";
        return Write(name, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    }
    static Outputs PlanesImages(const ByteSource& source, const std::string& mapPath, const std::string& prefix)
    {
        Outputs outputs;
        std::vector<uint8_t> bytesStorage;
        if (!ReadSource(source, mapPath, bytesStorage, outputs.error)) return outputs;
        const std::span<const uint8_t> bytes = bytesStorage;
        if (bytes.size() < 8192) {
            outputs.error = "truncated BIZ tables";
            return outputs;
        }
        std::vector<uint8_t> rgb(4096 * 4096 * 3);
        std::vector<uint16_t> depth(4096 * 4096), block;
        for (size_t y = 0; y < 32; ++y)
            for (size_t x = 0; x < 32; ++x) {
                const size_t index = y * 32 + x, offset = U32(bytes, index * 4), size = U32(bytes, 4096 + index * 4);
                if (offset < 8192 || offset > bytes.size() || size > bytes.size() - offset) {
                    outputs.error = "invalid BIZ block range";
                    return outputs;
                }
                if (!DecodeMapBlock(bytes.subspan(offset, size), block, outputs.error)) return outputs;
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
        outputs.files.emplace_back(prefix + "color.png", EncodePng(4096, 4096, 3, rgb));
        outputs.files.emplace_back(prefix + "depth.png", EncodeGray16Png(4096, 4096, depth));
        return outputs;
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
    int m_jobs;
    std::vector<Step> m_steps;
    std::map<std::string, std::string> m_maps;
    std::set<std::string> m_water, m_definitions, m_written;
    std::map<std::string, std::vector<std::string>> m_props;
};

// SHA-256 over everything Writer::Run reads, in a fixed order: the building list, every scenario .pis and the files those
// name (map, grid, height map, water animation, script), each by lowercase path, size and digest. It mirrors Run's lookups
// on purpose: a file Run starts to read must be added here too (the unit test flips a byte in every source file). False
// when a name does not resolve; the conversion then runs and reports that error itself.
bool HashInputs(const ByteSource& source, std::string& digest)
{
    // Lowercase path -> the path as the source spells it; empty for a height map the .pis names but the source lacks.
    std::map<std::string, std::string> inputs;
    std::string error;
    const auto building = FindFile(source, "Data/Script/BuildingList.ble");
    const auto scenarios = FindFile(source, "Data/Scenario");
    const auto directories = scenarios ? source.List(*scenarios) : std::nullopt;
    if (!building || !directories) return false;
    inputs.emplace(LowerAscii(*building), *building);
    for (const SourceEntry& directory : *directories) {
        if (!directory.directory) continue;
        const std::string folder = *scenarios + "/" + directory.name;
        const auto files = source.List(folder);
        if (!files) return false;
        for (const SourceEntry& file : *files) {
            const size_t dot = file.name.rfind('.');
            if (dot == std::string::npos || dot == 0 || LowerAscii(file.name.substr(dot)) != ".pis") continue;
            const std::string path = folder + "/" + file.name;
            std::vector<uint8_t> bytes;
            if (!ReadBoundedFile(source, path, 1 << 20, bytes, error)) return false;
            inputs.emplace(LowerAscii(path), path);
            const auto entries = pis::ParseEntries(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
            auto named = [&](const char* name) {
                for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                    if (ascii::EqualsNoCase(it->key.c_str(), name))
                        return NormalizeSourcePath(it->type == pis::String && it->hasString ? it->string : std::string());
                return std::string();
            };
            auto add = [&](const std::string& name, bool optional) {
                if (name.empty()) return !optional;
                const auto found = FindFile(source, name);
                if (!found && !optional) return false;
                inputs.emplace(name, found.value_or(""));
                return true;
            };
            auto script = named("WORLD_SCRIPT");
            if (script.size() < 4) return false;
            script.replace(script.size() - 4, 4, ".ble");
            if (!add(named("WORLD_MAPFILENAME"), false) || !add(named("WORLD_MAPGRID"), false) || !add(named("WORLD_MAPHEIGHTMAP"), true) ||
                    !add(named("WORLD_WATERANIM"), true) || !add(script, false))
                return false;
        }
    }
    sha256::Hasher total;
    auto feed = [&](std::string_view text) { total.Update(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size())); };
    for (const auto& [name, path] : inputs) {
        if (path.empty()) {
            feed(name + " absent\n");
            continue;
        }
        std::vector<uint8_t> bytes;
        if (!ReadBoundedFile(source, path, 64 << 20, bytes, error)) return false;
        feed(name + ' ' + std::to_string(bytes.size()) + ' ' + sha256::Hex(bytes) + '\n');
    }
    digest = total.HexDigest();
    return true;
}

// The stamp holds the revision and inputs (`header`), then every file that conversion wrote; "end" shows it was written whole.
bool StampIsCurrent(const std::string& stamp, const std::string& header, const PackSink& out)
{
    if (!stamp.starts_with(header) || !stamp.ends_with("\nend\n")) return false;
    const auto present = out.Files();
    const std::set<std::string> have(present.begin(), present.end());
    const size_t last = stamp.size() - 4; // the "end\n" line
    for (size_t at = header.size(); at < last;) {
        const size_t end = stamp.find('\n', at);
        if (!std::string_view(stamp).substr(at, end - at).starts_with("file ") || !have.contains(stamp.substr(at + 5, end - at - 5)))
            return false;
        at = end + 1;
    }
    return true;
}
} // namespace

bool ConvertMaps(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
        const std::function<void(int done, int total)>& report, bool* upToDate, int jobs)
{
    files = 0;
    if (upToDate) *upToDate = false;
    try {
        // Every build's post-build step asks for the maps and the PNGs take seconds per island: skip when the stamp of the last
        // conversion names these inputs, this revision and outputs that are all still there. The stamp is removed first,
        // so an interrupted run is redone, and written last.
        const std::string stamp = "maps/" + std::string(kMapStampName);
        std::string header;
        if (std::string digest; HashInputs(source, digest))
            header = "obl.map output " + std::to_string(kMapOutputRevision) + "\ninputs " + digest + "\n";
        if (!header.empty()) {
            if (const auto bytes = out.Read(stamp)) {
                const std::string previous(bytes->begin(), bytes->end());
                if (StampIsCurrent(previous, header, out)) {
                    if (upToDate) *upToDate = true;
                    return true;
                }
            }
        }
        out.Remove(stamp);
        Writer writer(source, out, schemas, files, error, report, jobs);
        if (!writer.Run()) return false;
        if (header.empty()) return true;
        std::string text = header;
        for (const auto& path : writer.Written()) text += "file " + path + "\n";
        return out.WriteText(stamp, text + "end\n", error);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
} // namespace obl::convert
