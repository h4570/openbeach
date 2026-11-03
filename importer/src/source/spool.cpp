#include "openbeach/importer/spool.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace obl::convert {
namespace {

class DirectorySpool final : public Spool {
public:
    explicit DirectorySpool(std::filesystem::path directory)
        : directory_(std::move(directory)), prefix_("spool-" + std::to_string(std::random_device{}()) + "-")
    {}

    ~DirectorySpool() override
    {
        for (auto& [name, file] : files_) {
            file.close();
            std::error_code ec;
            std::filesystem::remove(Full(name), ec);
        }
    }

    bool Create(std::string_view name) override
    {
        Remove(name);
        std::error_code ec;
        std::filesystem::create_directories(directory_, ec);
        // Opened for reading and writing at once, so the ranged reads that follow need no reopening.
        auto& file = files_[std::string(name)];
        file.open(Full(name), std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        return static_cast<bool>(file);
    }

    bool Append(std::string_view name, std::span<const uint8_t> bytes) override
    {
        const auto it = files_.find(name);
        if (it == files_.end()) return false;
        it->second.seekp(0, std::ios::end);
        it->second.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(it->second);
    }

    bool Read(std::string_view name, uint64_t offset, std::span<uint8_t> out) override
    {
        const auto it = files_.find(name);
        if (it == files_.end()) return false;
        auto& file = it->second;
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        const bool ok = file.gcount() == static_cast<std::streamsize>(out.size());
        file.clear();
        return ok;
    }

    void Remove(std::string_view name) override
    {
        const auto it = files_.find(name);
        if (it == files_.end()) return;
        it->second.close();
        std::error_code ec;
        std::filesystem::remove(Full(name), ec);
        files_.erase(it);
    }

private:
    std::filesystem::path Full(std::string_view name) const { return directory_ / (prefix_ + std::string(name)); }

    std::filesystem::path directory_;
    // Other imports may spool into the same directory.
    std::string prefix_;
    std::map<std::string, std::fstream, std::less<>> files_;
};

class MemorySpool final : public Spool {
public:
    bool Create(std::string_view name) override
    {
        files_[std::string(name)].clear();
        return true;
    }

    bool Append(std::string_view name, std::span<const uint8_t> bytes) override
    {
        const auto it = files_.find(name);
        if (it == files_.end()) return false;
        it->second.insert(it->second.end(), bytes.begin(), bytes.end());
        return true;
    }

    bool Read(std::string_view name, uint64_t offset, std::span<uint8_t> out) override
    {
        const auto it = files_.find(name);
        if (it == files_.end() || offset > it->second.size() || out.size() > it->second.size() - offset) return false;
        std::copy_n(it->second.begin() + static_cast<ptrdiff_t>(offset), out.size(), out.begin());
        return true;
    }

    void Remove(std::string_view name) override
    {
        if (const auto it = files_.find(name); it != files_.end()) files_.erase(it);
    }

    bool Has(std::string_view name) const override { return files_.contains(name); }

private:
    std::map<std::string, std::vector<uint8_t>, std::less<>> files_;
};

} // namespace

std::unique_ptr<Spool> OpenDirectorySpool(const std::filesystem::path& directory)
{
    return std::make_unique<DirectorySpool>(directory);
}

std::unique_ptr<Spool> OpenMemorySpool()
{
    return std::make_unique<MemorySpool>();
}

} // namespace obl::convert
