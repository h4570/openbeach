#pragma once

// Scratch files of an import (docs/architecture/data-formats/converter/index.md): a CD keeps the game in
// compressed cabinets, and a file too large to hold in memory (the 467 MB sprite archive) is extracted once into a spool
// file and then read in ranges. The desktop CLI spools into a directory, the browser into Blobs it may page to disk.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

namespace obl::convert {

class Spool {
public:
    virtual ~Spool() = default;

    // Starts the file `name` (one path component) empty, replacing an older one of that name.
    virtual bool Create(std::string_view name) = 0;
    // Appends `bytes` to the file `name`.
    virtual bool Append(std::string_view name, std::span<const uint8_t> bytes) = 0;
    // Fills `out` from `offset` of the file `name`; false when it is missing or shorter.
    virtual bool Read(std::string_view name, uint64_t offset, std::span<uint8_t> out) = 0;
    // Drops the file `name`; a missing one is not an error.
    virtual void Remove(std::string_view name) = 0;
    // True when the file `name` exists, so a source can adopt what another instance of the importer already extracted into
    // the same spool (the browser's workers share their Blobs). A spool that cannot tell says no.
    virtual bool Has(std::string_view name) const
    {
        (void)name;
        return false;
    }
};

// Files in `directory` (created when missing) under names unique to this spool; the files left are removed when the spool
// is destroyed.
std::unique_ptr<Spool> OpenDirectorySpool(const std::filesystem::path& directory);
// Files in memory, for tests.
std::unique_ptr<Spool> OpenMemorySpool();

} // namespace obl::convert
