#include "openbeach/importer/byte_source.h"

#include "core/ascii_case.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <system_error>

namespace obl::convert {
namespace {

std::string_view Utf8View(const std::u8string& text)
{
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path FromUtf8(std::string_view relative)
{
    return std::filesystem::path(std::u8string(relative.begin(), relative.end()));
}

class DirectorySource final : public ByteSource {
public:
    explicit DirectorySource(std::filesystem::path root) : root_(std::move(root)) {}

    std::optional<std::vector<SourceEntry>> List(std::string_view directory) const override
    {
        std::error_code ec;
        std::vector<SourceEntry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(Full(directory), ec)) {
            // UTF-8, not the narrow code page: installations hold names such as "Léeme.htm".
            entries.push_back({std::string(Utf8View(entry.path().filename().u8string())), entry.is_directory(ec)});
        }
        if (ec) return std::nullopt;
        return entries;
    }

    std::optional<uint64_t> Size(std::string_view path) const override
    {
        std::error_code ec;
        const auto full = Full(path);
        if (!std::filesystem::is_regular_file(full, ec)) return std::nullopt;
        const uint64_t size = std::filesystem::file_size(full, ec);
        if (ec) return std::nullopt;
        return size;
    }

    bool Read(std::string_view path, uint64_t offset, std::span<uint8_t> out) const override
    {
        // Archives are read in many small blocks, so the last file stays open rather than being reopened per block.
        const std::lock_guard lock(mutex_);
        if (!stream_.is_open() || openPath_ != path) {
            stream_.close();
            stream_.clear();
            openPath_.clear();
            stream_.open(Full(path), std::ios::binary);
            if (!stream_) return false;
            openPath_ = path;
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset));
        stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return stream_.good() || (stream_.eof() && static_cast<size_t>(stream_.gcount()) == out.size());
    }

    std::string Describe(std::string_view path) const override { return Full(path).string(); }

private:
    std::filesystem::path Full(std::string_view relative) const { return relative.empty() ? root_ : root_ / FromUtf8(relative); }

    std::filesystem::path root_;
    mutable std::mutex mutex_;
    mutable std::ifstream stream_;
    mutable std::string openPath_;
};

// "a/b/c" -> "a/b"; "c" -> "".
std::string_view Parent(std::string_view path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
}

} // namespace

std::string ByteSource::Describe(std::string_view path) const
{
    return std::string(path);
}

std::optional<std::vector<uint8_t>> ByteSource::ReadAll(std::string_view path) const
{
    const auto size = Size(path);
    if (!size) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(*size));
    if (!Read(path, 0, bytes)) return std::nullopt;
    return bytes;
}

std::optional<std::string> FindFile(const ByteSource& source, std::string_view relative)
{
    std::string current;
    while (!relative.empty()) {
        const size_t slash = relative.find('/');
        const std::string_view part = relative.substr(0, slash);
        relative = slash == std::string_view::npos ? std::string_view() : relative.substr(slash + 1);
        const auto entries = source.List(current);
        if (!entries) return std::nullopt;
        const SourceEntry* match = nullptr;
        for (const SourceEntry& entry : *entries) {
            if (entry.name == part) {
                match = &entry;
                break;
            }
            if (!match && ascii::EqualsNoCase(entry.name, part)) match = &entry;
        }
        if (!match) return std::nullopt;
        current = current.empty() ? match->name : current + "/" + match->name;
    }
    return current;
}

std::unique_ptr<ByteSource> OpenDirectorySource(const std::filesystem::path& root)
{
    return std::make_unique<DirectorySource>(root);
}

void MemorySource::Add(std::string_view path, std::vector<uint8_t> bytes)
{
    files_.insert_or_assign(std::string(path), std::move(bytes));
}

std::optional<std::vector<SourceEntry>> MemorySource::List(std::string_view directory) const
{
    std::vector<SourceEntry> entries;
    bool found = directory.empty();
    for (const auto& [path, bytes] : files_) {
        // Walk up from the file: every ancestor of `path` directly under `directory` is one entry.
        std::string_view child = path;
        for (std::string_view parent = Parent(child);; child = parent, parent = Parent(child)) {
            if (parent == directory) {
                const std::string_view name = parent.empty() ? child : child.substr(parent.size() + 1);
                const bool isDirectory = child.size() != path.size();
                if (entries.empty() || entries.back().name != name) entries.push_back({std::string(name), isDirectory});
                found = true;
                break;
            }
            if (parent.empty()) break;
        }
    }
    if (!found) return std::nullopt;
    return entries;
}

std::optional<uint64_t> MemorySource::Size(std::string_view path) const
{
    const auto it = files_.find(path);
    if (it == files_.end()) return std::nullopt;
    return it->second.size();
}

bool MemorySource::Read(std::string_view path, uint64_t offset, std::span<uint8_t> out) const
{
    const auto it = files_.find(path);
    if (it == files_.end() || offset > it->second.size() || it->second.size() - offset < out.size()) return false;
    std::copy_n(it->second.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    return true;
}

} // namespace obl::convert
