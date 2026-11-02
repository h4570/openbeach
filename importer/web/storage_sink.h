#pragma once

// The browser's pack output (docs/architecture/data-formats/converter/index.md, "Browser adapter"): converted files
// go to a key/value store the worker entry (worker_main.cpp) binds to browser storage; tests bind it to memory. Keys are
// "<root>/<pack path>", so several packs and other data share one store. Portable: no browser API here.

#include "openbeach/importer/pack_sink.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert::web {

class PackStorage {
public:
    virtual ~PackStorage() = default;

    // Stores `bytes` under `key`, replacing an older value; false when the storage refuses (quota).
    virtual bool Put(std::string_view key, std::span<const uint8_t> bytes) = 0;
    virtual std::optional<std::vector<uint8_t>> Get(std::string_view key) const = 0;
    // A missing key is not an error.
    virtual bool Delete(std::string_view key) = 0;
    // Every key that starts with `prefix`.
    virtual std::vector<std::string> Keys(std::string_view prefix) const = 0;
};

// A sink writing the pack under `root` (with '/', no trailing '/', e.g. "packs/base") of `storage`, which must outlive
// it. Pack paths must be relative and plain ('/'-separated, no empty, "." or ".." parts): a key never leaves the root.
std::unique_ptr<PackSink> OpenStorageSink(PackStorage& storage, std::string root);

} // namespace obl::convert::web
