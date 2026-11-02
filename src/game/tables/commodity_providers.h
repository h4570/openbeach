#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace obl::commodity {

constexpr int kFamilies = 26;

// One building slot that offers a commodity of the family; the order of the records is the order guests find providers in.
struct ProviderRecord {
    int id; // building object id
    int slot; // commodity slot in the building's definition
    int x; // tile of the building
    int y;
};

class Providers {
public:
    int buildingCount = 0;
    uint64_t signature = 0;
    bool hasSignature = false;
    std::array<std::vector<ProviderRecord>, kFamilies> records;
    // Missed-desire counters feedback::Feedback::Update reads.
    std::array<uint32_t, kFamilies> complaints{};

    int Count(int family) const { return static_cast<int>(records[family].size()); }
    // The next Rebuild runs whatever the building count says.
    void Invalidate() { buildingCount = -1; }
};

} // namespace obl::commodity
