#pragma once

// The whole import (docs/architecture/data-formats/converter/index.md): every converter of the base pack in the
// fixed order of obl-convert's commands, then pack.json, which hashes the files before it. The CLI's `import`
// command and the browser worker (importer/web/) both run it, so the two produce the same pack.

#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"
#include "openbeach/importer/spool.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace obl::json_schema {
class Registry;
}

namespace obl::convert {

struct KnownExe;

struct ImportOptions {
    // The pack's default text table; without one, BeachLife.ini's language of the source (an error when it has none).
    std::optional<std::string> language;
    // Without `language`, picks the table among the copy's own (ChooseLanguage) with this one as the second choice, so a
    // valid copy always imports. For a caller whose player chooses after the import: the browser.
    std::optional<std::string> preferredLanguage;
    // Receives the pack's language before the first step.
    std::string* chosenLanguage = nullptr;
    // Where a CD's files too large for memory (the sprite archive) are extracted; required for a CD, unused for an
    // installation.
    Spool* spool = nullptr;
    // Called before each step with its name (the CLI command's name: "gui-tables", "sprites", ..., "pack").
    std::function<void(std::string_view step)> progress;
    // Called while a long step runs ("maps", "sprites", "pack") with how many of its items are done out of the total.
    std::function<void(std::string_view step, uint64_t done, uint64_t total)> count;
    // Worker threads for the sprite and map images (0: one per hardware thread). The pack is the same for every value; the
    // default, one, converts on the calling thread, which is all the browser build (no threads) ever does.
    int jobs = 1;
    // The executables the source may hold (KnownExe, importer/src/source/source.h); empty means the supported builds.
    // Tests pass their own table so a synthetic copy imports end to end.
    std::span<const KnownExe> knownExes;
};

// Converts the user's copy in `source` (an installation, or a CD root read through its cabinets) into the base pack in
// `pack`; false with `error` naming the failed step. The
// class icon and cursors (`resources`) are not pack files and are left to the CLI.
bool ImportPack(
        const ByteSource& source, PackSink& pack, const json_schema::Registry& schemas, const ImportOptions& options, std::string& error);

} // namespace obl::convert
