#include "openbeach/importer/pack_sink.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace obl::convert {
namespace {

std::filesystem::path FromUtf8(std::string_view relative)
{
    return std::filesystem::path(std::u8string(relative.begin(), relative.end()));
}

class DirectorySink final : public PackSink {
public:
    explicit DirectorySink(std::filesystem::path root) : root_(std::move(root)) {}

    bool Write(std::string_view path, std::span<const uint8_t> bytes, std::string& error) override
    {
        const auto full = Full(path);
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        std::ofstream stream(full, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) {
            error = full.string() + ": cannot write";
            return false;
        }
        return true;
    }

    std::optional<std::vector<uint8_t>> Read(std::string_view path) const override
    {
        // Linux opens a directory as a stream and throws on the first read, so only regular files are read.
        std::error_code ec;
        if (!std::filesystem::is_regular_file(Full(path), ec)) return std::nullopt;
        std::ifstream stream(Full(path), std::ios::binary);
        if (!stream) return std::nullopt;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (stream.bad()) return std::nullopt;
        return bytes;
    }

    bool Remove(std::string_view path) override
    {
        std::error_code ec;
        std::filesystem::remove(Full(path), ec);
        return !ec;
    }

    std::vector<std::string> Files() const override
    {
        std::vector<std::string> files;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
                !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const auto relative = std::filesystem::relative(it->path(), root_).generic_u8string();
            files.emplace_back(reinterpret_cast<const char*>(relative.data()), relative.size());
        }
        return files;
    }

    std::string Describe(std::string_view path) const override { return Full(path).string(); }

private:
    std::filesystem::path Full(std::string_view relative) const { return relative.empty() ? root_ : root_ / FromUtf8(relative); }

    std::filesystem::path root_;
};

} // namespace

std::string PackSink::Describe(std::string_view path) const
{
    return std::string(path);
}

bool PackSink::WriteText(std::string_view path, std::string_view text, std::string& error)
{
    return Write(path, {reinterpret_cast<const uint8_t*>(text.data()), text.size()}, error);
}

std::unique_ptr<PackSink> OpenDirectorySink(const std::filesystem::path& root)
{
    return std::make_unique<DirectorySink>(root);
}

bool MemorySink::Write(std::string_view path, std::span<const uint8_t> bytes, std::string&)
{
    files_.insert_or_assign(std::string(path), std::vector<uint8_t>(bytes.begin(), bytes.end()));
    return true;
}

std::optional<std::vector<uint8_t>> MemorySink::Read(std::string_view path) const
{
    const auto it = files_.find(path);
    if (it == files_.end()) return std::nullopt;
    return it->second;
}

bool MemorySink::Remove(std::string_view path)
{
    if (const auto it = files_.find(path); it != files_.end()) files_.erase(it);
    return true;
}

std::vector<std::string> MemorySink::Files() const
{
    std::vector<std::string> files;
    files.reserve(files_.size());
    for (const auto& [path, bytes] : files_) files.push_back(path);
    return files;
}

} // namespace obl::convert
