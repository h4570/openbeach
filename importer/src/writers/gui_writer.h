#pragma once
#include "importer/src/readers/gui_tables.h"
#include "openbeach/importer/pack_sink.h"
#include <filesystem>
#include <nlohmann/json.hpp>

namespace obl::convert {
// Both documents are schema- and semantics-checked before any file is written.
bool GuiDocuments(const ExeGuiTables& tables, nlohmann::json& browser, nlohmann::json& credits, std::string& error);
bool WriteGuiTables(const ExeGuiTables& tables, PackSink& out, std::string& error);
} // namespace obl::convert
