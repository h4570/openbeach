#pragma once

#include "game/tables/world_params_tables.h"
#include <filesystem>
#include <string>

namespace obl::catalog {
// Loads the ordered catalog into a reset WorldParams; the records own everything they hold. Failure leaves the destination
// unchanged.
bool LoadPack(const std::filesystem::path& pack, tables::WorldParams& out, std::string& error);
} // namespace obl::catalog
