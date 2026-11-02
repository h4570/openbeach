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

// Installation maps (all campaign islands, shared by sandboxes), water sheets and save-name -> catalog prop lookup.
// Scenario scripts are read only for prop aliases; placements and scenario logic remain there.
bool ConvertMaps(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
        const std::function<void(int done, int total)>& report = {});
} // namespace obl::convert
