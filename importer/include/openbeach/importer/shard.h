#pragma once

// Sharing the heavy steps of an import between workers that share no memory (docs/architecture/data-formats/converter/index.md,
// "Sharded conversion"). The browser cannot use threads, so the page runs several instances of the importer: each converts
// its share of the units of the "maps" and "sprites" steps and hands the finished units over; one instance then runs the
// whole import and takes those units instead of converting them. The unit split is the one of the native worker pool
// (ordered_pool.h): a sprite of the bank, an image step of the map writer.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obl::convert {

// The share `index` of `count` shards.
struct Shard {
    int index = 0;
    int count = 1;
};

// What a unit wrote: its files in write order, and the failure that ended it after those files (empty when it succeeded).
struct UnitOutput {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    std::string error;
};

// How a step's units travel. A shard run hands each finished unit of its share to `sink` and writes nothing to the pack; a
// run with `source` asks it for every unit instead of converting it (false: the unit is missing). Units are numbered in the
// order a serial run does them; the steps are named "maps" and "sprites".
struct UnitExchange {
    const Shard* shard = nullptr;
    std::function<void(std::string_view step, size_t unit, UnitOutput& output)> sink;
    std::function<bool(std::string_view step, size_t unit, UnitOutput& output)> source;
};

// Which shard converts which unit: the heaviest units first, each to the shard with the least work so far, so shards finish
// together whatever order the costs come in. Every instance computes the same owners from the same costs.
inline std::vector<int> AssignUnits(std::span<const uint64_t> costs, int count)
{
    std::vector<size_t> order(costs.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return costs[a] > costs[b]; });
    std::vector<uint64_t> load(static_cast<size_t>(std::max(count, 1)), 0);
    std::vector<int> owner(costs.size(), 0);
    for (const size_t unit : order) {
        const auto lightest = std::min_element(load.begin(), load.end());
        owner[unit] = static_cast<int>(lightest - load.begin());
        *lightest += std::max<uint64_t>(costs[unit], 1);
    }
    return owner;
}

} // namespace obl::convert
