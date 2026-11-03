#pragma once

#include "importer/src/writers/sprite_convert.h"

#include <functional>

namespace obl::convert {

// Strict source decoders; planes are top-down row-major. Terrain indices follow the map legend.
bool DecodeMapTerrain(std::span<const uint8_t> bytes, std::vector<uint8_t>& indices, std::string& error);
bool DecodeMapHeight(std::span<const uint8_t> bytes, int& width, int& height, std::vector<uint8_t>& samples, std::string& error);
bool DecodeMapBlock(std::span<const uint8_t> bytes, std::vector<uint16_t>& words, std::string& error);
bool DecodeMapWater(std::span<const uint8_t> bytes, std::vector<uint8_t>& sheet, std::string& error);

struct MapPropAlias {
    std::string name, definition;
};
// Decoded script names only; no placements or scenario logic are emitted.
bool ParseMapPropAliases(std::string_view script, std::vector<MapPropAlias>& aliases, std::string& error);

// Bumped whenever the converter writes other map files from the same sources, so existing run/ packs are redone.
inline constexpr int kMapOutputRevision = 1;
// maps/<this>: what the last conversion was made from and wrote (see ConvertMaps); not a JSON, so no loader lists it.
inline constexpr const char* kMapStampName = "conversion.stamp";

// Installation maps (all campaign islands, shared by sandboxes), water sheets and save-name -> catalog prop lookup.
// `jobs` worker threads read, decode and encode the images (0: one per hardware thread; the output, the first error and
// the progress are those of one job). Scenario scripts are read only for prop aliases; placements and scenario logic remain there.
// A conversion writes maps/conversion.stamp last (revision, SHA-256 of every input, the files written). When the stamp
// matches the source and all those files are present nothing is read or written, no worker starts, `files` is 0 and
// `*upToDate` is set.
bool ConvertMaps(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
        const std::function<void(int done, int total)>& report = {}, bool* upToDate = nullptr, int jobs = 1);
} // namespace obl::convert
