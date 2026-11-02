#include "importer/src/readers/exe_tables.h"

#include "importer/src/source/exe_scan.h"

#include <algorithm>
#include <map>
#include <string_view>

namespace obl::convert {

namespace {

constexpr uint8_t kPushImm32 = 0x68;
constexpr uint8_t kCallRel32 = 0xE8;
// mov dword ptr [esi + reg*4 + disp32], imm32: C7 84 <sib> <disp32> <imm32>
constexpr uint8_t kMovImm32[] = {0xC7, 0x84};
constexpr size_t kStoreSize = 11;
// The key push sits at most this many bytes before its lookup call (an interleaved store and `mov ecx, esi` in between).
constexpr size_t kPushWindow = 32;
// A grid store follows the lookup call of its key within this many bytes (the inlined lookup's compare loop included).
constexpr size_t kMaxKeyDistance = 0x400;
// The building area plus a one-cell margin must fit the 25-column rows, four layers of (h + 2) rows the 100 rows.
constexpr int32_t kMaxFootprintSide = kFootprintColumns - 2;
constexpr int kFootprintLayers = 4;

using scan::LoadU32;

// A grid block at file offset `pos`: a plausible size and no cell outside the four (h + 2) x (w + 2) layers.
bool IsGridBlock(std::span<const uint8_t> bytes, size_t pos)
{
    if (pos + kFootprintBlockSize > bytes.size()) return false;
    const auto width = static_cast<int32_t>(LoadU32(&bytes[pos]));
    const auto height = static_cast<int32_t>(LoadU32(&bytes[pos + 4]));
    if (width < 1 || width > kMaxFootprintSide || height < 1 || height > kMaxFootprintSide) return false;
    const int layerRows = kFootprintRows / kFootprintLayers;
    for (int row = 0; row < kFootprintRows; ++row) {
        const bool rowInArea = row % layerRows < height + 2;
        for (int column = rowInArea ? width + 2 : 0; column < kFootprintColumns; ++column)
            if (LoadU32(&bytes[pos + 8 + (size_t(row) * kFootprintColumns + column) * 4]) != 0) return false;
    }
    return true;
}

// The grid table: the longest run of back-to-back grid blocks in the file, as virtual addresses.
std::vector<uint32_t> FindGridRun(const PeFile& exe)
{
    auto bytes = exe.Bytes();
    size_t bestStart = 0;
    size_t bestLength = 0;
    for (size_t pos = 0; pos + kFootprintBlockSize <= bytes.size(); pos += 4) {
        if (!IsGridBlock(bytes, pos) || (pos >= kFootprintBlockSize && IsGridBlock(bytes, pos - kFootprintBlockSize))) continue;
        size_t length = 0;
        while (IsGridBlock(bytes, pos + length * kFootprintBlockSize)) ++length;
        if (length > bestLength) {
            bestStart = pos;
            bestLength = length;
        }
    }
    std::vector<uint32_t> run;
    for (size_t i = 0; i < bestLength; ++i) {
        auto va = exe.VaOfFileOffset(bestStart + i * kFootprintBlockSize);
        if (!va) return {};
        run.push_back(*va);
    }
    return run;
}

class Scanner {
public:
    Scanner(const PeFile& exe, const std::set<std::string>& buildingKeys) : exe_(exe), bytes_(exe.Bytes()), buildingKeys_(buildingKeys) {}

    // The building key pushed by `push imm32` at `pos`, if the immediate names one of the user's buildings.
    std::optional<std::string> PushedKey(size_t pos) const
    {
        if (pos + 5 > bytes_.size() || bytes_[pos] != kPushImm32) return std::nullopt;
        auto s = exe_.CString(LoadU32(&bytes_[pos + 1]), 32);
        if (!s || !buildingKeys_.contains(std::string(*s))) return std::nullopt;
        return std::string(*s);
    }

    // `mov [esi + reg*4 + disp32], imm32` at `pos`.
    bool IsStore(size_t pos) const
    {
        if (pos + kStoreSize > bytes_.size() || bytes_[pos] != kMovImm32[0] || bytes_[pos + 1] != kMovImm32[1]) return false;
        const uint8_t sib = bytes_[pos + 2];
        return (sib & 7) == 6 && (sib >> 6) == 2; // base esi, scale 4
    }

    uint32_t StoreImm(size_t pos) const { return LoadU32(&bytes_[pos + 7]); }

    // The key of a call at `pos` into a section: the last building key pushed in the window before it.
    std::optional<std::string> CallKey(size_t pos) const
    {
        if (pos + 5 > bytes_.size() || bytes_[pos] != kCallRel32) return std::nullopt;
        auto va = exe_.VaOfFileOffset(pos);
        if (!va || !exe_.SectionAt(*va + 5 + LoadU32(&bytes_[pos + 1]))) return std::nullopt;
        for (size_t back = 5; back <= kPushWindow && back <= pos; ++back)
            if (auto key = PushedKey(pos - back)) return key;
        return std::nullopt;
    }

    // The key of the store at `pos`: the one of the latest keyed call before it.
    std::optional<std::string> StoreKey(size_t pos) const
    {
        for (size_t back = 1; back <= kMaxKeyDistance && back <= pos; ++back)
            if (auto key = CallKey(pos - back)) return key;
        return std::nullopt;
    }

    size_t Size() const { return bytes_.size(); }

private:
    const PeFile& exe_;
    std::span<const uint8_t> bytes_;
    const std::set<std::string>& buildingKeys_;
};

} // namespace

bool FindFootprints(const PeFile& exe, const std::set<std::string>& buildingKeys, std::vector<ExeFootprint>& out, std::string& error)
{
    out.clear();
    error.clear();
    const std::vector<uint32_t> run = FindGridRun(exe);
    if (run.size() != kFootprintCount) {
        error = "found a run of " + std::to_string(run.size()) + " footprint grids, expected " + std::to_string(kFootprintCount);
        return false;
    }
    std::map<uint32_t, int> storesOfGrid;
    for (uint32_t va : run) storesOfGrid[va] = 0;

    // The grid stores, in file order (one assignment function); every section is a candidate, since the UK/US CD
    // build marks all of them, .text included, as data.
    Scanner scanner(exe, buildingKeys);
    std::vector<size_t> stores;
    for (size_t pos = 0; pos < scanner.Size(); ++pos) {
        if (!scanner.IsStore(pos)) continue;
        auto grid = storesOfGrid.find(scanner.StoreImm(pos));
        if (grid == storesOfGrid.end()) continue;
        if (++grid->second > 1) {
            error = "footprint grid at " + std::to_string(grid->first) + " is stored twice";
            return false;
        }
        stores.push_back(pos);
        pos += kStoreSize - 1;
    }
    if (stores.size() != kFootprintCount) {
        error = "found " + std::to_string(stores.size()) + " footprint grid stores, expected " + std::to_string(kFootprintCount);
        return false;
    }

    std::set<std::string> keys;
    for (size_t store : stores) {
        const uint32_t address = scanner.StoreImm(store);
        auto key = scanner.StoreKey(store);
        if (!key) {
            error = "no building key for the grid at " + std::to_string(address);
            out.clear();
            return false;
        }
        if (!keys.insert(*key).second) {
            error = *key + ": building key assigned twice";
            out.clear();
            return false;
        }
        auto block = exe.Read(address, kFootprintBlockSize);
        if (block.empty()) {
            error = *key + ": grid not backed by file data";
            out.clear();
            return false;
        }
        ExeFootprint fp;
        fp.building = *key;
        fp.address = address;
        fp.width = static_cast<int32_t>(LoadU32(block.data()));
        fp.height = static_cast<int32_t>(LoadU32(block.data() + 4));
        fp.cells.resize(size_t{kFootprintColumns} * kFootprintRows);
        for (size_t i = 0; i < fp.cells.size(); ++i) fp.cells[i] = static_cast<int32_t>(LoadU32(block.data() + 8 + i * 4));
        out.push_back(std::move(fp));
    }
    return true;
}

} // namespace obl::convert
