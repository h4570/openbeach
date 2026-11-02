#pragma once

#include "importer/src/source/pe_file.h"

#include <cstdint>
#include <string>
#include <vector>

namespace obl::convert {

struct ScriptName {
    int32_t id = 0;
    std::string name;
};

struct ScriptTables {
    std::vector<ScriptName> states; // the object scripts' STATE_* (empty slots skipped)
    std::vector<ScriptName> messages; // MT_*, shared by the object and world scripts
};

// 135 named states (134 on the August 2002 CD build, renumbered to the engine's ids) and 108 messages; other counts are
// refused.
bool ExtractScriptVocabulary(const PeFile& exe, ScriptTables& out, std::string& error);

} // namespace obl::convert
