#pragma once

// catalog/ from BuildingList.ble (docs/architecture/data-formats/game-data.md, "Catalog";
// converter/catalog.md): index.json plus one validated JSON per entry.

#include "importer/src/readers/building_list.h"
#include "importer/src/readers/exe_tables.h"
#include "openbeach/importer/pack_sink.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace obl::json_schema {
class Registry;
}

namespace obl::convert {

// One catalog file: path relative to the pack root (`catalog/buildings/beachbar.json`), its schema id and the document.
struct CatalogFile {
    std::string path;
    std::string schema;
    nlohmann::ordered_json document;
};

// The documents of `list` in write order (index first), with each building's grid of `footprints` (FindFootprints) as its
// `footprint`. False when a key or name cannot become a content key (not [a-z0-9_] after lowercasing, or two entries of a
// kind that differ only in case), or a footprint names no building, differs from its building's size or has a value outside
// the (w + 2) x (h + 2) area the engine reads.
bool CatalogDocuments(
        const BuildingList& list, const std::vector<ExeFootprint>& footprints, std::vector<CatalogFile>& files, std::string& error);

// Validates every document, then writes them to `out`. Nothing is written when one is invalid.
bool WriteCatalog(const BuildingList& list, const std::vector<ExeFootprint>& footprints, PackSink& out,
        const json_schema::Registry& schemas, int& written, std::string& error);

bool ConvertCatalog(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& written, std::string& error);

// A float as the JSON number of the shortest decimal that reads back to the same float (principles.md, "Numbers").
nlohmann::ordered_json FloatNumber(float value);

} // namespace obl::convert
