#pragma once

// The inverse of ReadOldSaveBody: an old (1.6) save body as the record bytes the game's loader reads
// (docs/architecture/data-formats/converter/save-load-bridge.md). The bridge that loads an obl.save maps the document back
// to an OldSaveBody and feeds these bytes to the unchanged section loaders through an in-memory stream; native per-module
// loaders can replace it.

#include "importer/src/readers/save/save_body.h"

#include <cstdint>
#include <vector>

namespace obl::convert {

// The stream and save signatures, the header records, then every section in ReadOldSaveBody's order with its class record
// (World's is the header's last record; EventManager and Weather have none) and the objects after the ObjectManager's
// fields. Each record is written with its own tag and count, so a body read from a file encodes to the same bytes.
std::vector<uint8_t> EncodeOldSave(const OldSaveBody& body);

} // namespace obl::convert
