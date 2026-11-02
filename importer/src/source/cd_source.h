#pragma once

#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/spool.h"

#include <cstdint>
#include <memory>
#include <string>

namespace obl::convert {

// True when `source` is a CD root: it has data1.hdr but no installed sprite header.
bool IsCdRoot(const ByteSource& source);

struct CdSourceLimits {
    // A file up to this size is extracted into memory when first read; a larger one into the spool. Extracted files stay
    // cached while they fit this budget together, so repeated ranged reads of one file extract it once.
    uint64_t memory = 64u << 20;
};

// The installed tree of the CD in `cd`. The source keeps references to `cd` and `spool`, so the caller keeps both alive
// while it is used; its spool files are removed when it is destroyed. Nullptr with `error` when the cabinets cannot be
// read or no group holds the sprite header.
std::unique_ptr<ByteSource> OpenCdSource(const ByteSource& cd, Spool& spool, std::string& error, CdSourceLimits limits = {});

} // namespace obl::convert
