#pragma once

#include "game/tables/world_params_tables.h"

#include <filesystem>
#include <string>

namespace obl::guest_set {
// Loads `<pack>/guests/<set>.json` (obl.guest_set, game-data.md "Guest sets") into the guest part of a WorldParams that already
// holds the catalog: the guest records, the name pools and their counts. The engine's Guests*.ble parser
// fills the same fields from the same data; the converted file is checked against it by the guest_set_load unit test.
// Transactional: on failure `out` is unchanged and `error` names the file and the reason.
bool LoadPack(const std::filesystem::path& pack, const char* set, tables::WorldParams& out, std::string& error);
} // namespace obl::guest_set
