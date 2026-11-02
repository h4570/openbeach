#pragma once

// The GUI scripts of Data/Script/GUI as decoded layouts (`gui/<name>.gui`) and the language choice (`gui/layouts.json`,
// obl.gui_layouts). Game-data.md, "GUI layouts".

#include "core/json_schema.h"

#include <cstdint>
#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <span>
#include <string>

namespace obl::convert {

// The layout's text as UTF-16 code points: each byte XOR 0xFF, then the language's code page (Windows-1250 when `polish`,
// Windows-1252 otherwise). Undefined bytes keep their C1 code point. Nothing is trimmed or normalized; line ends stay as the
// file has them. Empty input is refused.
bool DecodeGuiLayout(std::span<const uint8_t> file, bool polish, std::u16string& units, std::string& error);

// Converts every installed GUI script that the layout table names and writes `gui/<name>.gui` (UTF-8, first line
// `#obl-gui 1`) and `gui/layouts.json`. The layouts.json document is validated before it is written. A file the revision
// lacks is skipped; a layout left without its "*" fallback is dropped. No script at all is an error.
bool ConvertGuiLayouts(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error);

} // namespace obl::convert
