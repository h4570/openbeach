#pragma once

#include "importer/src/source/pe_file.h"

#include <cstdint>
#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <string>
#include <vector>

namespace obl::convert {

inline constexpr uint16_t kGameIconId = 0x68;
inline constexpr uint16_t kCursorIds[] = {101, 102, 103, 106, 107, 108, 111};

bool ConvertResources(const ByteSource& source, PackSink& out, int& files, std::string& error);

bool BuildIconFile(const PeFile& exe, uint16_t groupId, bool cursor, std::vector<uint8_t>& file, std::string& error);

} // namespace obl::convert
