#pragma once

// The browser's sources of the user's copy (docs/architecture/data-formats/converter/index.md, "Browser adapter"):
// the files of a folder the player picked, or a disc image file. Both read bytes through a callback, which the worker
// entry (worker_main.cpp) binds to the browser's File objects; tests bind it to memory. Portable: no browser API here.

#include "openbeach/importer/byte_source.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace obl::convert::web {

// Fills `out` with the bytes at `offset` of one file; false when it cannot (a short read is a failure).
using ReadAt = std::function<bool(uint64_t offset, std::span<uint8_t> out)>;

// An ISO 9660 image of `size` bytes: the Joliet names when the image has them, otherwise the primary volume's (version
// suffix ";1" and a trailing '.' dropped). Nullptr with `error` when the image has no valid primary volume descriptor.
std::unique_ptr<ByteSource> OpenIsoSource(uint64_t size, ReadAt read, std::string& error);

struct PickedFile {
    std::string path; // as the browser names it (webkitRelativePath: "<picked folder>/Data/...")
    uint64_t size = 0;
};

// Fills `out` from file `index` of the picked list.
using ReadPicked = std::function<bool(size_t index, uint64_t offset, std::span<uint8_t> out)>;

std::unique_ptr<ByteSource> OpenPickedFilesSource(const std::vector<PickedFile>& files, ReadPicked read, std::string& error);

} // namespace obl::convert::web
