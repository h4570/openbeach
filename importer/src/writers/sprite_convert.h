#pragma once

#include "importer/src/readers/sprite_bank.h"
#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::json_schema {
class Registry;
}

namespace obl::convert {

// Reads the BLSprite.dat block of one animation (BlockSize() bytes at its offset).
using SpriteBlockReader = std::function<bool(const SpriteAnimRecord& anim, std::vector<uint8_t>& block, std::string& error)>;
// Stores one output file; `path` is relative to the sprites/ directory, with '/' separators.
using ConvertedFileSink = std::function<bool(const std::string& path, std::span<const uint8_t> bytes, std::string& error)>;

std::string LowerAscii(std::string_view text);

// Largest sheet page in pixels, both ways: unique frames wrap to the next row past this width and to the next page past
// this height (a larger frame gets a page of its own). Keeps every page a loadable texture and the 32-bit converter's
// memory bounded (gail's d_cabaret alone has 4000 frames of 320 x 240).
inline constexpr int kMaxSheetSize = 4096;

// One sprite: its palette PNG and sheet pages go to `writeFile` as they are encoded; `json` receives the obl.sprite
// document for sprites/<lowercase name>.json (not validated here), with `tone_group` when `toneGroup` is not 0.
bool ConvertSprite(const SpriteRecord& sprite, const SpriteBlockReader& readBlock, const ConvertedFileSink& writeFile,
        nlohmann::ordered_json& json, std::string& error, int toneGroup = 0);

// The engine has four tone plans (palette_modifiers.h); they belong to the guest models.
inline constexpr int kToneGroups = 4;

// The tone group of each guest model: the sprites the guest types of the installed guest sets (`Data/Script/<set>.ble`, XOR-FF)
// name, numbered 1..kToneGroups in key order; models past the fourth stay untoned. No sets: no tone groups.
bool ToneGroupsOfGuestSets(const std::vector<std::vector<uint8_t>>& installedSets, std::map<std::string, int>& groups, std::string& error);

struct SpriteConversionStats {
    int sprites = 0, animations = 0, frames = 0, files = 0;
    bool upToDate = false; // nothing written: the stamp says the output is of this archive and revision
    // Called every few sprites and after the last one with the sprites done and the total of the conversion.
    std::function<void(int done, int total)> report;
};

// Bumped whenever the converter writes other sprite files from the same archive, so existing run/ packs are redone.
inline constexpr int kSpriteOutputRevision = 4;
// sprites/<this>: what the last full conversion was made from (SpriteArchive::Stamp); not a JSON, so no loader lists it.
inline constexpr const char* kSpriteStampName = "conversion.stamp";

// XOR-FF source records: eight neighbour bits then one frame; duplicate masks keep the last low byte.
bool ConvertPathAutotile(std::span<const uint8_t> source, nlohmann::ordered_json& autotile, std::string& error);

// Converts every sprite of the installation in `source` (Data/Sprites/BLSprite.hdr and .dat; their stamps and versions
// must match; a CD is read through OpenCdSource) whose key contains `filter` (all when empty) into sprites/ of `out`.
// Every JSON is validated against `obl.sprite` in `schemas` before it is written; the first invalid one stops the
// conversion with the schema errors. A full conversion (no filter) writes sprites/conversion.stamp and does nothing when
// the stamp already matches.
// The sprite archive of an installation, opened once so that several conversions (filters) share its header and tables.
class SpriteArchive {
public:
    // The .dat is read from `source` while the archive converts, so the caller keeps the source alive as long as the
    // archive.
    static bool Open(const ByteSource& source, SpriteArchive& archive, std::string& error);

    // Converts like ConvertSprites; the archive must have been opened.
    bool Convert(PackSink& out, const json_schema::Registry& schemas, const std::string& filter, SpriteConversionStats& stats,
            std::string& error) const;

    // The output revision, the SHA-256 of the header and the size of the .dat.
    std::string Stamp() const;
    // Revision 3 already wrote the same PNGs: add/update only autotile JSON when the archive part of its stamp matches.
    bool UpdateAutotiles(PackSink& out, const json_schema::Registry& schemas, SpriteConversionStats& stats, std::string& error) const;

private:
    std::vector<uint8_t> header_;
    std::vector<uint8_t> pathTiles_;
    nlohmann::ordered_json autotile_;
    const ByteSource* data_ = nullptr; // holds BLSprite.dat
    std::string dataPath_; // inside data_
    std::map<std::string, int> toneGroups_; // ToneGroupsOfGuestSets of the root's guest sets
};

bool ConvertSprites(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, const std::string& filter,
        SpriteConversionStats& stats, std::string& error);

} // namespace obl::convert
