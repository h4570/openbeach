#pragma once

// The object and world scripts of the installation as decoded script text: `scripts/object/<name>.obs` (Data/Script/Object)
// and `scenarios/<id>/world.wsc` (Data/Scenario/<folder>). Game-data.md, "Scripts".

#include <cstdint>
#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <span>
#include <string>

namespace obl::convert {

// The script's text: each byte XOR 0xFF. Nothing is trimmed or normalized; CRLF line ends stay as the file has them. The
// installed scripts are ASCII; any byte outside ASCII (a UTF-16 byte-order mark among them) is refused, so the text
// encodes back to the installed bytes exactly. Empty input is refused.
bool DecodeScript(std::span<const uint8_t> file, std::string& text, std::string& error);

// The text of `scripts/feedback_vocabulary.json` (obl.feedback_vocabulary), which ConvertScripts writes next to the script vocabularies.
std::string FeedbackVocabularyJson();

// Converts every object script and every scenario's world script and writes them under `out` (UTF-8 files with the first
// line `#obl-script 1 object` or `#obl-script 1 world`). Object names are lowercased (pack paths are lowercase). Scenario
// folders map to ids as maps-scenarios.md says: `Scenario<n>` to `s<n>`, `SandScenario<n>` to `sandbox<n>`; any other folder
// is refused, and so is a folder with other than one .ble script. Returns the number of files written.
bool ConvertScripts(const ByteSource& source, PackSink& out, int& files, std::string& error);

} // namespace obl::convert
