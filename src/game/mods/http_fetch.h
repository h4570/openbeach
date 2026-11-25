#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Platform-backed asynchronous HTTPS (and local file:// for tests) fetch interface for mod catalogs.
// Implementation lives in src/platform/http_fetch.cpp, keeping network code out of src/core/ and the simulation.
namespace obl::net {

struct FetchResult {
    bool ok = false;
    std::vector<uint8_t> bytes;
    std::string error;
};

// Starts an asynchronous fetch of `url`, refusing responses larger than `maxBytes`.
// Returns a non-zero handle id. `allowLocal` enables `file://` URLs (local/test catalogs only).
uint64_t StartFetch(std::string_view url, size_t maxBytes, bool allowLocal = false);

// Non-blocking poll. Returns true and populates `result` once the fetch completes (and releases the handle).
bool PollFetch(uint64_t id, FetchResult& result);

// Cancels an in-flight fetch and discards its result.
void CancelFetch(uint64_t id);

} // namespace obl::net
