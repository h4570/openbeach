#pragma once

// The converted scripts of the pack (docs/architecture/data-formats/converter/scripts.md): `scripts/object/<name>.obs` and
// `scenarios/<id>/world.wsc`, decoded script text whose first line names its kind. The engine paths (`data/script/object/...`,
// `Data/Scenario/<folder>/...`) map to these files here, so the converter and both loaders (the object and the world script
// loaders, and the Linux simulation) share one mapping. Pure logic: no Windows APIs.

#include <filesystem>
#include <string>
#include <string_view>

namespace obl::script_pack {

enum class Kind { Object, World };

// The first line of a converted script, with its LF: `#obl-script 1 object` or `#obl-script 1 world`.
std::string_view Header(Kind kind);

// The scenario id of a scenario folder (case-insensitive): `Scenario<n>` is `s<n>`, `SandScenario<n>` is `sandbox<n>`
// (maps-scenarios.md). False for any other name.
bool ScenarioId(std::string_view folder, std::string& id);

// The pack-relative file of an engine script path of `kind`: `data/script/object/<name>.lgc` is `scripts/object/<name>.obs`
// (the name lowercased), and `Data/Scenario/<folder>/<folder>.lgc` is `scenarios/<id>/world.wsc`. Slashes and backslashes both
// separate the parts; case is ignored. "" for any other shape, so an object path is refused as a world script and back.
std::string PackPath(std::string_view enginePath, Kind kind);

// Reads a converted script: `file` must start with the header of `kind`; `text` gets the rest, every byte kept (CRLF line ends
// stay). False with `error` (naming the file) for a missing file, a missing or wrong header, or an empty text.
bool ReadScript(const std::filesystem::path& file, Kind kind, std::string& text, std::string& error);

// The script the engine asks for by `enginePath`, read from the pack directory `packDir` (`<game dir>/packs/base`). False with
// `error` when the path has no pack file of `kind` or the file is refused by ReadScript.
bool ReadPackScript(const std::filesystem::path& packDir, std::string_view enginePath, Kind kind, std::string& text, std::string& error);

} // namespace obl::script_pack
