#include "importer/src/source/cd_source.h"

#include "importer/src/source/cabinet.h"
#include "importer/src/source/source.h"

#include <algorithm>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace obl::convert {
namespace {

class CdSource final : public ByteSource {
public:
    CdSource(const ByteSource& cd, Spool& spool, Cabinet cabinet, std::string headerPath, CdSourceLimits limits)
        : cd_(cd), spool_(spool), cabinet_(std::move(cabinet)), headerPath_(std::move(headerPath)), limits_(limits)
    {}

    ~CdSource() override
    {
        for (const uint32_t index : spooled_) spool_.Remove(SpoolName(index));
    }

    // Presents `file` (an index into the cabinet's files) at its installed path.
    void AddStored(size_t file)
    {
        const std::string& path = cabinet_.Files()[file].path;
        AddParents(path);
        stored_.emplace(path, file);
    }

    // Presents the CD's loose file `cdPath` at the same path.
    void AddLoose(const std::string& cdPath)
    {
        if (stored_.contains(cdPath)) return;
        AddParents(cdPath);
        loose_.insert(cdPath);
    }

    std::optional<std::vector<SourceEntry>> List(std::string_view directory) const override
    {
        const auto it = directories_.find(directory);
        if (it == directories_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<uint64_t> Size(std::string_view path) const override
    {
        if (loose_.contains(path)) return cd_.Size(path);
        const auto it = stored_.find(path);
        if (it == stored_.end()) return std::nullopt;
        return cabinet_.Files()[it->second].size;
    }

    bool Read(std::string_view path, uint64_t offset, std::span<uint8_t> out) const override
    {
        if (loose_.contains(path)) return cd_.Read(path, offset, out);
        const auto it = stored_.find(path);
        if (it == stored_.end()) return false;
        const CabinetFile& file = cabinet_.Files()[it->second];
        if (offset > file.size || out.size() > file.size - offset) return false;
        const std::lock_guard lock(mutex_);
        if (file.size > limits_.memory) return Spooled(file) && spool_.Read(SpoolName(file.index), offset, out);
        const std::vector<uint8_t>* bytes = Cached(file);
        if (!bytes) return false;
        std::copy_n(bytes->begin() + static_cast<ptrdiff_t>(offset), out.size(), out.begin());
        return true;
    }

    std::string Describe(std::string_view path) const override
    {
        if (path.empty() || loose_.contains(path)) return cd_.Describe(path);
        return cd_.Describe(headerPath_) + ": " + std::string(path);
    }

private:
    static std::string SpoolName(uint32_t index) { return "cabinet-" + std::to_string(index); }

    void AddParents(std::string_view path)
    {
        std::string_view parent;
        for (size_t start = 0;;) {
            const size_t slash = path.find('/', start);
            const bool last = slash == std::string_view::npos;
            const std::string_view name = path.substr(start, last ? std::string_view::npos : slash - start);
            auto& entries = directories_[std::string(parent)];
            if (std::none_of(entries.begin(), entries.end(), [&](const SourceEntry& e) { return e.name == name; }))
                entries.push_back({std::string(name), !last});
            if (last) return;
            parent = path.substr(0, slash);
            start = slash + 1;
        }
    }

    // The whole file from the memory cache, extracting it (and dropping the oldest files past the budget) when needed.
    const std::vector<uint8_t>* Cached(const CabinetFile& file) const
    {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->first != file.index) continue;
            cache_.splice(cache_.begin(), cache_, it);
            return &cache_.front().second;
        }
        std::vector<uint8_t> bytes;
        std::string error;
        if (!cabinet_.ExtractToMemory(file, bytes, error, limits_.memory)) return nullptr;
        cached_ += bytes.size();
        cache_.emplace_front(file.index, std::move(bytes));
        while (cache_.size() > 1 && cached_ > limits_.memory) {
            cached_ -= cache_.back().second.size();
            cache_.pop_back();
        }
        return &cache_.front().second;
    }

    // Extracts the file into the spool once.
    bool Spooled(const CabinetFile& file) const
    {
        if (spooled_.contains(file.index)) return true;
        const std::string name = SpoolName(file.index);
        std::string error;
        const bool ok = spool_.Create(name) &&
                cabinet_.Extract(file, [&](std::span<const uint8_t> bytes) { return spool_.Append(name, bytes); }, error);
        if (!ok) {
            spool_.Remove(name);
            return false;
        }
        spooled_.insert(file.index);
        return true;
    }

    const ByteSource& cd_;
    Spool& spool_;
    Cabinet cabinet_;
    std::string headerPath_;
    CdSourceLimits limits_;
    std::map<std::string, size_t, std::less<>> stored_; // installed path -> index into cabinet_.Files()
    std::set<std::string, std::less<>> loose_;
    std::map<std::string, std::vector<SourceEntry>, std::less<>> directories_ = {{"", {}}};

    mutable std::mutex mutex_;
    mutable std::list<std::pair<uint32_t, std::vector<uint8_t>>> cache_; // most recently read first
    mutable uint64_t cached_ = 0;
    mutable std::set<uint32_t> spooled_;
};

} // namespace

bool IsCdRoot(const ByteSource& source)
{
    return !FindFile(source, kSpriteHeaderPath) && FindFile(source, kCabinetHeaderPath);
}

std::unique_ptr<ByteSource> OpenCdSource(const ByteSource& cd, Spool& spool, std::string& error, CdSourceLimits limits)
{
    const auto headerPath = FindFile(cd, kCabinetHeaderPath);
    if (!headerPath) {
        error = std::string(kCabinetHeaderPath) + " not found under " + cd.Describe("");
        return nullptr;
    }
    auto cabinet = Cabinet::Open(cd, *headerPath, error);
    if (!cabinet) return nullptr;
    // The game's group is the one holding the sprite header; the other groups are setup files and language variants.
    const CabinetFile* spriteHeader = cabinet->Find(kSpriteHeaderPath, error);
    if (!spriteHeader) return nullptr;
    const std::string group = spriteHeader->group;
    std::vector<size_t> files;
    for (size_t i = 0; i < cabinet->Files().size(); ++i) {
        const CabinetFile& file = cabinet->Files()[i];
        if (file.group == group && !file.External()) files.push_back(i);
    }
    auto source = std::make_unique<CdSource>(cd, spool, std::move(*cabinet), *headerPath, limits);
    for (const size_t file : files) source->AddStored(file);
    if (const auto exe = FindFile(cd, kExePath)) source->AddLoose(*exe);
    return source;
}

} // namespace obl::convert
