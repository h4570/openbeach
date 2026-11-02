#pragma once

#include "core/json_schema.h"

#include <nlohmann/json.hpp>

#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace obl::convert {

// Reads the decoded text of one guest file (the bytes XOR 0xFF undone) with the engine's grammar (importer/src/readers/world_params_process.h,
// Process, and world_params_add.h for the fields) and builds the obl.guest_set document in file order. Keys and sprite names
// come out lowercase. Refused, with the byte offset: an odd number of quotes, a line the engine would not load (an unrecognized
// keyword, a record field before its GUEST_ line, a missing value), a repeated guest key, a missing GENDER, a number that is not
// finite, a DRUNKTYPE outside 0..4, a name longer than the engine's slot, and the engine's limits (20 guests, 140 male names,
// 140 female names, 600 surnames). Byte fields (COLOUR, NIGHTS_MIN, NIGHTS_MAX) wrap as the engine's byte stores do.
bool ParseGuestSet(std::string_view text, nlohmann::ordered_json& document, std::string& error);

// Converts the fifteen campaign and sandbox guest files of an installation into `<out>/guests/<set>.json`, validated before
// they are written. `files` is the number of sets written. Reads `Data/Script/Guests01.ble` ... `Guests12.ble` and
// `Data/Script/Sandbox{E,M,H}_Guests.ble` (case-insensitive, as the installation's files are).
bool ConvertGuestSets(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error);

} // namespace obl::convert
