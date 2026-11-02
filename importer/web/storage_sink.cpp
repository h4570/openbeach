#include "importer/web/storage_sink.h"

#include <algorithm>

namespace obl::convert::web {
namespace {

bool PlainPath(std::string_view path)
{
    if (path.empty() || path.find('\\') != std::string_view::npos) return false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = slash + 1;
    }
    return true;
}

class StorageSink final : public PackSink {
public:
    StorageSink(PackStorage& storage, std::string root) : storage_(storage), prefix_(std::move(root) + "/") {}

    bool Write(std::string_view path, std::span<const uint8_t> bytes, std::string& error) override
    {
        if (!PlainPath(path)) {
            error = "not a pack path: " + std::string(path);
            return false;
        }
        if (!storage_.Put(Key(path), bytes)) {
            error = Describe(path) + ": the browser storage refused the file (quota?)";
            return false;
        }
        return true;
    }

    std::optional<std::vector<uint8_t>> Read(std::string_view path) const override
    {
        if (!PlainPath(path)) return std::nullopt;
        return storage_.Get(Key(path));
    }

    bool Remove(std::string_view path) override { return PlainPath(path) && storage_.Delete(Key(path)); }

    std::vector<std::string> Files() const override
    {
        std::vector<std::string> files = storage_.Keys(prefix_);
        for (std::string& key : files) key.erase(0, prefix_.size());
        return files;
    }

    std::string Describe(std::string_view path) const override { return "browser storage " + Key(path); }

private:
    std::string Key(std::string_view path) const { return prefix_ + std::string(path); }

    PackStorage& storage_;
    std::string prefix_;
};

} // namespace

std::unique_ptr<PackSink> OpenStorageSink(PackStorage& storage, std::string root)
{
    return std::make_unique<StorageSink>(storage, std::move(root));
}

} // namespace obl::convert::web
