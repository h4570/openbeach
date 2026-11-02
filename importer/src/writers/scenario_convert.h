#pragma once

#include "core/json_schema.h"
#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {
// Effective PIS values only. Unused entries (including duplicates and list entries after a gap) are reported.
bool ParseScenario(std::string_view text, int number, bool sandbox, nlohmann::ordered_json& document, std::vector<std::string>& ignored,
        std::string& error);
bool ConvertScenarios(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
        std::vector<std::string>* report = nullptr);
} // namespace obl::convert
