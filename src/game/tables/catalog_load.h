#pragma once

#include "game/tables/world_params_tables.h"
#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <string>

namespace obl::catalog {
// Loads the merged catalog index (`catalog/index.json` plus every active mod's `catalog/index.patch.json` in load order).
bool LoadIndex(const std::filesystem::path& pack, nlohmann::json& out, std::string& error);
// Loads the ordered catalog into a reset WorldParams; the records own everything they hold. Failure leaves the destination
// unchanged.
bool LoadPack(const std::filesystem::path& pack, tables::WorldParams& out, std::string& error);
} // namespace obl::catalog
