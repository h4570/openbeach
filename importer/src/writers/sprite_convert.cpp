#include "importer/src/writers/sprite_convert.h"

#include "importer/src/writers/guest_set_convert.h"
#include "importer/src/writers/png_write.h"
#include "core/ascii_case.h"
#include "formats/schemas.h"
#include "core/sha256.h"
#include "importer/src/source/source.h"
#include "game/objects/guest_sets.h"
#include "formats/sprite_pack.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace obl::convert {

namespace {

constexpr std::string_view kPathTilesPath = "Data/pathtiles.ble";

bool HasPathTiles(const std::vector<uint8_t>& header, bool& found, std::string& error)
{
    SpriteBank bank;
    if (!ParseSpriteBank(header, bank, error)) return false;
    found = std::any_of(
            bank.sprites.begin(), bank.sprites.end(), [](const auto& sprite) { return LowerAscii(sprite.name).starts_with("pathtile_"); });
    return true;
}

bool IsKey(const std::string& key)
{
    return !key.empty() &&
            std::all_of(key.begin(), key.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

const char* Kind(uint32_t type)
{
    switch (type) {
    case 1: return "ui";
    case 2: return "special";
    case 3: return "character";
    case 5: return "building";
    default: return "object_variants"; // 6; ParseSpriteBank refuses other types
    }
}

// Building planes by animation name; empty for plain animations.
const char* Plane(const std::string& anim)
{
    if (anim == "image") return "color";
    if (anim == "depth") return "depth";
    if (anim == "lightmap") return "light";
    if (anim == "pick") return "pick";
    if (anim == "shadow") return "shadow";
    return "";
}

} // namespace

std::string LowerAscii(std::string_view text)
{
    std::string out(text);
    for (char& c : out) c = ascii::Lower(c);
    return out;
}

bool ConvertPathAutotile(std::span<const uint8_t> source, nlohmann::ordered_json& autotile, std::string& error)
{
    std::string text;
    for (uint8_t byte : source) text.push_back(static_cast<char>(byte ^ 255));
    size_t at = 0;
    auto space = [](char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; };
    auto token = [&](std::string_view& value) {
        while (at < text.size()) {
            if (space(text[at]))
                ++at;
            else if (text[at] == '/') {
                const size_t end = text.find('\n', at);
                if (end == std::string::npos) {
                    at = text.size();
                    break;
                }
                at = end + 1;
            } else
                break;
        }
        if (at == text.size()) return false;
        const size_t first = at++;
        if (text[first] != ';')
            while (at < text.size() && !space(text[at]) && text[at] != ';') ++at;
        value = std::string_view(text).substr(first, at - first);
        return true;
    };
    std::array<uint8_t, 256> table{};
    std::string_view value;
    int records = 0;
    while (token(value)) {
        int mask = 0, frame = 0;
        for (int field = 0; field < 9; ++field) {
            if (field && !token(value)) {
                error = "pathtiles.ble: incomplete nine-field record";
                return false;
            }
            int number = 0;
            if (value.starts_with('+')) value.remove_prefix(1);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            // Like atoi, a non-number is zero and a numeric prefix is accepted; overflow is refused.
            if (parsed.ec == std::errc::result_out_of_range) {
                error = "pathtiles.ble: integer overflow";
                return false;
            }
            if (field < 8) {
                if (number < 0 || number > 1) {
                    error = "pathtiles.ble: neighbour is not a bit";
                    return false;
                }
                mask |= number << field;
            } else
                frame = number;
        }
        table[mask] = static_cast<uint8_t>(frame);
        ++records;
    }
    if (!records) {
        error = "pathtiles.ble: no records";
        return false;
    }
    autotile = nlohmann::ordered_json::object();
    autotile["masks"] = nlohmann::ordered_json::array();
    for (uint8_t frame : table) autotile["masks"].push_back(frame == 255 ? -1 : frame);
    autotile["full"] = 67;
    return true;
}

bool ConvertSprite(const SpriteRecord& sprite, const SpriteBlockReader& readBlock, const ConvertedFileSink& writeFile,
        nlohmann::ordered_json& json, std::string& error, int toneGroup)
{
    const std::string key = LowerAscii(sprite.name);
    if (!IsKey(key)) {
        error = "sprite \"" + sprite.name + "\": the name is not a content key";
        return false;
    }
    if (sprite.palettes.empty()) {
        error = "sprite " + key + ": no palette";
        return false;
    }
    std::set<std::string> paths;
    auto emit = [&](const std::string& path, const std::vector<uint8_t>& bytes) {
        if (bytes.empty()) {
            error = "sprite " + key + ": cannot encode " + path;
            return false;
        }
        if (!paths.insert(path).second) {
            error = "sprite " + key + ": two files named " + path;
            return false;
        }
        return writeFile(path, bytes, error);
    };

    json = nlohmann::ordered_json::object();
    json["format"] = "obl.sprite";
    json["version"] = 1;
    json["name"] = key;
    json["kind"] = Kind(sprite.type);
    if (toneGroup) json["tone_group"] = toneGroup;
    json["pixels"] = "indexed";

    const int colors = sprite.PaletteColors();
    std::vector<uint8_t> rgb;
    for (const auto& palette : sprite.palettes) rgb.insert(rgb.end(), palette.begin(), palette.begin() + colors * 3);
    const std::string palettePath = key + ".palettes.png";
    if (!emit(palettePath, EncodePng(colors, static_cast<int>(sprite.palettes.size()), 3, rgb))) return false;
    json["palettes"] = palettePath;
    json["palette_colors"] = colors;
    json["palette_count"] = sprite.palettes.size();
    if (sprite.type == 5) json["depth_extents"] = sprite.extra;

    nlohmann::ordered_json anims = nlohmann::ordered_json::array();
    std::set<std::string> sheetNames;
    std::vector<uint8_t> block, page;
    for (size_t index = 0; index < sprite.anims.size(); ++index) {
        const SpriteAnimRecord& anim = sprite.anims[index];
        const std::string name = LowerAscii(anim.name);
        if (!IsKey(name)) {
            error = "sprite " + key + ": animation \"" + anim.name + "\" is not a content key";
            return false;
        }
        const char* plane = sprite.type == 5 ? Plane(name) : "";
        const bool mask = std::strcmp(plane, "pick") == 0 || std::strcmp(plane, "shadow") == 0;
        // Animations are addressed by index, so a repeated name (two sprites have one) stays, with its own sheets.
        std::string sheetName = name;
        if (!sheetNames.insert(sheetName).second) sheetNames.insert(sheetName = name + "_" + std::to_string(index));

        const int w = anim.width, h = anim.height, unique = anim.uniqueFrames;
        if (w == 0 || h == 0 || unique == 0) {
            error = "sprite " + key + ": animation " + name + " has no size or no frames";
            return false;
        }
        const int columns = std::clamp(kMaxSheetSize / w, 1, unique);
        const int totalRows = (unique + columns - 1) / columns;
        const int rows = std::clamp(kMaxSheetSize / h, 1, totalRows);
        const int perPage = columns * rows;
        const size_t sheetWidth = static_cast<size_t>(columns) * w;
        nlohmann::ordered_json sheets = nlohmann::ordered_json::array();

        // Frames arrive in order; a page is encoded and written as soon as its last frame is in.
        auto addFrame = [&](int u, std::span<const uint8_t> pixels, std::string& err) {
            const int pageIndex = u / perPage, i = u % perPage;
            const int count = std::min(perPage, unique - pageIndex * perPage);
            const int pageRows = (count + columns - 1) / columns;
            if (i == 0) page.assign(sheetWidth * pageRows * h * 2, 0);
            const size_t x0 = static_cast<size_t>(i % columns) * w, y0 = static_cast<size_t>(i / columns) * h;
            for (int y = 0; y < h; ++y)
                std::memcpy(&page[((y0 + y) * sheetWidth + x0) * 2], &pixels[static_cast<size_t>(y) * w * 2], static_cast<size_t>(w) * 2);
            if (i + 1 < count) return true;
            const std::string path = key + "/" + sheetName + (pageIndex ? "_p" + std::to_string(pageIndex) : "") + ".png";
            sheets.push_back(path);
            if (emit(path, EncodePng(static_cast<int>(sheetWidth), pageRows * h, 2, page))) return true;
            err = error;
            return false;
        };
        std::vector<uint16_t> frameMap;
        int runLength = 0;
        if (!readBlock(anim, block, error) || !DecodeSpriteAnim(anim, block, mask, frameMap, runLength, addFrame, error)) {
            error = "sprite " + key + ": " + error;
            return false;
        }

        nlohmann::ordered_json a;
        a["name"] = name;
        if (*plane) a["plane"] = plane;
        a["directions"] = anim.directions;
        a["frames"] = anim.frames;
        a["size"] = {w, h};
        a["hotspot"] = {anim.hotspotX, anim.hotspotY};
        a["sheets"] = std::move(sheets);
        a["columns"] = columns;
        if (rows < totalRows) a["rows"] = rows;
        a["unique_frames"] = unique;
        if (runLength != sprite_pack::kDefaultRunLength) a["run_length"] = runLength;
        bool identity = frameMap.size() == static_cast<size_t>(unique);
        for (size_t i = 0; identity && i < frameMap.size(); ++i) identity = frameMap[i] == i;
        if (!identity) a["frame_map"] = frameMap;
        anims.push_back(std::move(a));
    }
    json["animations"] = std::move(anims);
    return true;
}

namespace {

constexpr std::string_view kSpriteDataPath = "Data/Sprites/BLSprite.dat";

} // namespace

bool ToneGroupsOfGuestSets(const std::vector<std::vector<uint8_t>>& installedSets, std::map<std::string, int>& groups, std::string& error)
{
    groups.clear();
    std::set<std::string> models;
    for (const auto& installed : installedSets) {
        std::string text(installed.size(), char{});
        for (size_t i = 0; i < installed.size(); ++i) text[i] = static_cast<char>(installed[i] ^ 0xFF);
        nlohmann::ordered_json set;
        if (!ParseGuestSet(text, set, error)) {
            error = "guest set: " + error;
            return false;
        }
        for (const auto& type : set["types"]) models.insert(type["sprite"].get<std::string>());
    }
    int group = 0;
    for (const auto& model : models) {
        if (group == kToneGroups) break;
        groups[model] = ++group;
    }
    return true;
}

bool SpriteArchive::Open(const ByteSource& source, SpriteArchive& archive, std::string& error)
{
    const auto headerPath = FindFile(source, kSpriteHeaderPath);
    const auto dataPath = FindFile(source, kSpriteDataPath);
    if (!headerPath || !dataPath) {
        error = "no Data/Sprites/BLSprite.hdr and BLSprite.dat under " + source.Describe("");
        return false;
    }
    archive.header_ = source.ReadAll(*headerPath).value_or(std::vector<uint8_t>());
    archive.data_ = &source;
    archive.dataPath_ = *dataPath;
    bool paths = false;
    if (!HasPathTiles(archive.header_, paths, error)) return false;
    if (paths) {
        const auto path = FindFile(source, kPathTilesPath);
        if (!path) {
            error = "no Data/pathtiles.ble under " + source.Describe("");
            return false;
        }
        archive.pathTiles_ = source.ReadAll(*path).value_or(std::vector<uint8_t>());
        if (!ConvertPathAutotile(archive.pathTiles_, archive.autotile_, error)) return false;
    }
    std::vector<std::vector<uint8_t>> sets;
    for (const auto& set : guest_sets::kSets) {
        const auto path = FindFile(source, std::string("Data/Script/") + set.stem + ".ble");
        if (!path) continue;
        sets.push_back(source.ReadAll(*path).value_or(std::vector<uint8_t>()));
    }
    return ToneGroupsOfGuestSets(sets, archive.toneGroups_, error);
}

// Converts the sprites of the archive (its header and .dat must share one stamp and version).
bool SpriteArchive::Convert(PackSink& out, const json_schema::Registry& schemas, const std::string& filter, SpriteConversionStats& stats,
        std::string& error) const
{
    const std::vector<uint8_t>& header = header_;
    SpriteBank bank;
    if (!ParseSpriteBank(header, bank, error)) return false;

    if (!data_) {
        error = "the sprite archive is not open";
        return false;
    }
    const ByteSource& data = *data_;
    uint8_t dataHead[24] = {};
    if (!data.Read(dataPath_, 0, dataHead) || header.size() < sizeof(dataHead) ||
            std::memcmp(dataHead, header.data(), sizeof(dataHead)) != 0) {
        error = "BLSprite.dat does not start with the stamp and version of BLSprite.hdr (files of different revisions)";
        return false;
    }
    const uint64_t dataSize = data.Size(dataPath_).value_or(0);
    const SpriteBlockReader readBlock = [&](const SpriteAnimRecord& anim, std::vector<uint8_t>& block, std::string& err) {
        if (anim.offset + anim.BlockSize() > dataSize) {
            err = "animation " + anim.name + ": block outside BLSprite.dat";
            return false;
        }
        block.resize(static_cast<size_t>(anim.BlockSize()));
        if (!data.Read(dataPath_, anim.offset, block)) {
            err = "animation " + anim.name + ": cannot read its block";
            return false;
        }
        return true;
    };

    const ConvertedFileSink write = [&](const std::string& relative, std::span<const uint8_t> bytes, std::string& err) {
        if (!out.Write("sprites/" + relative, bytes, err)) return false;
        ++stats.files;
        return true;
    };
    std::set<std::string> keys;
    const int total = static_cast<int>(bank.sprites.size());
    for (const SpriteRecord& sprite : bank.sprites) {
        const std::string key = LowerAscii(sprite.name);
        if (!filter.empty() && key.find(LowerAscii(filter)) == std::string::npos) continue;
        // The game takes the first sprite of a name regardless of case; a second one would never be drawn.
        if (!keys.insert(key).second) {
            error = "sprite " + key + " occurs twice (names differ only in case)";
            return false;
        }
        nlohmann::ordered_json json;
        const auto tone = toneGroups_.find(key);
        if (!ConvertSprite(sprite, readBlock, write, json, error, tone == toneGroups_.end() ? 0 : tone->second)) return false;
        if (key.starts_with("pathtile_") && !autotile_.is_null()) json["autotile"] = autotile_;
        if (!formats::ValidateDocument(schemas, "obl.sprite", json, error)) {
            error = "sprites/" + key + ".json: " + error;
            return false;
        }
        const std::string text = json.dump(2) + "\n";
        if (!write(key + ".json", std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error)) return false;
        ++stats.sprites;
        stats.animations += static_cast<int>(sprite.anims.size());
        for (const auto& anim : sprite.anims) stats.frames += anim.uniqueFrames;
        // Every sprite: the big ones take seconds each, so a coarser count looks like a hang.
        if (stats.report) stats.report(stats.sprites, total);
    }
    return true;
}

bool SpriteArchive::UpdateAutotiles(
        PackSink& out, const json_schema::Registry& schemas, SpriteConversionStats& stats, std::string& error) const
{
    SpriteBank bank;
    if (!ParseSpriteBank(header_, bank, error)) return false;
    for (const auto& sprite : bank.sprites) {
        const std::string key = LowerAscii(sprite.name);
        if (!key.starts_with("pathtile_")) continue;
        const std::string path = "sprites/" + key + ".json";
        try {
            const auto bytes = out.Read(path).value_or(std::vector<uint8_t>());
            auto doc = nlohmann::ordered_json::parse(bytes.begin(), bytes.end());
            doc["autotile"] = autotile_;
            if (!formats::ValidateDocument(schemas, "obl.sprite", doc, error)) return false;
            if (!out.WriteText(path, doc.dump(2) + '\n', error)) return false;
        } catch (const std::exception& e) {
            error = out.Describe(path) + ": " + e.what();
            return false;
        }
        ++stats.sprites;
        ++stats.files;
    }
    return true;
}

bool ConvertSprites(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, const std::string& filter,
        SpriteConversionStats& stats, std::string& error)
{
    SpriteArchive archive;
    if (!SpriteArchive::Open(source, archive, error)) return false;
    if (!filter.empty()) return archive.Convert(out, schemas, filter, stats, error);
    // A full conversion takes minutes and every build's post-build step asks for one: skip it when the stamp of the last
    // one names this archive and this output revision. The stamp is removed first, so an interrupted run is redone.
    const std::string stamp = "sprites/" + std::string(kSpriteStampName);
    const std::string current = archive.Stamp();
    std::string previous;
    if (const auto bytes = out.Read(stamp)) {
        previous.assign(bytes->begin(), bytes->end());
        if (previous == current) {
            stats.upToDate = true;
            return true;
        }
    }
    out.Remove(stamp);
    const std::string base = current.substr(0, current.find("path tiles "));
    std::string revision3 = base;
    revision3.replace(0, revision3.find('\n'), "obl.sprite output 3");
    // Only the autotile metadata changed since revision 3. Keep the validated sheets and palettes of this archive;
    // later edits of pathtiles.ble also update only JSON, with their hash tracked separately in the stamp.
    if (previous == revision3 || previous.starts_with(base)) {
        if (!archive.UpdateAutotiles(out, schemas, stats, error)) return false;
    } else if (!archive.Convert(out, schemas, filter, stats, error))
        return false;
    return out.WriteText(stamp, current, error);
}

std::string SpriteArchive::Stamp() const
{
    const uint64_t dataSize = data_ ? data_->Size(dataPath_).value_or(0) : 0;
    std::string stamp = "obl.sprite output " + std::to_string(kSpriteOutputRevision) + "\nheader " + sha256::Hex(header_) +
            "\ndata bytes " + std::to_string(dataSize) + "\n";
    if (!pathTiles_.empty()) stamp += "path tiles " + sha256::Hex(pathTiles_) + "\n";
    return stamp;
}

} // namespace obl::convert
