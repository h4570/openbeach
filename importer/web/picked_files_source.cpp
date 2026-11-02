// The files of a folder the player picked (<input webkitdirectory> or a drop), rooted at the game folder among them.

#include "importer/src/source/source.h"
#include "importer/web/web_source.h"

#include "core/ascii_case.h"

#include <algorithm>
#include <map>
#include <set>

namespace obl::convert::web {
namespace {

struct File {
    size_t index = 0; // into the picked list
    uint64_t size = 0;
};

// The '/'-separated parts of a browser path (some browsers give '\' on Windows); empty when a part is unsafe.
std::vector<std::string> SplitPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = std::min(path.find('/', start), path.size());
        std::string part = path.substr(start, slash - start);
        if (part.empty() || part == "." || part == "..") return {};
        parts.push_back(std::move(part));
        start = slash + 1;
    }
    return parts;
}

std::string Join(const std::vector<std::string>& parts, size_t first, size_t last)
{
    std::string joined;
    for (size_t i = first; i < last; ++i) joined += (joined.empty() ? "" : "/") + parts[i];
    return joined;
}

class PickedFilesSource final : public ByteSource {
public:
    PickedFilesSource(std::map<std::string, File, std::less<>> files, std::map<std::string, std::vector<SourceEntry>, std::less<>> dirs,
            ReadPicked read)
        : files_(std::move(files)), dirs_(std::move(dirs)), read_(std::move(read))
    {}

    std::optional<std::vector<SourceEntry>> List(std::string_view directory) const override
    {
        const auto it = dirs_.find(directory);
        if (it == dirs_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<uint64_t> Size(std::string_view path) const override
    {
        const auto it = files_.find(path);
        if (it == files_.end()) return std::nullopt;
        return it->second.size;
    }

    bool Read(std::string_view path, uint64_t offset, std::span<uint8_t> out) const override
    {
        const auto it = files_.find(path);
        if (it == files_.end() || offset > it->second.size || out.size() > it->second.size - offset) return false;
        return out.empty() || read_(it->second.index, offset, out);
    }

    std::string Describe(std::string_view path) const override { return "picked file " + std::string(path); }

private:
    std::map<std::string, File, std::less<>> files_;
    std::map<std::string, std::vector<SourceEntry>, std::less<>> dirs_;
    ReadPicked read_;
};

} // namespace

std::unique_ptr<ByteSource> OpenPickedFilesSource(const std::vector<PickedFile>& files, ReadPicked read, std::string& error)
{
    std::vector<std::vector<std::string>> paths;
    paths.reserve(files.size());
    std::optional<size_t> rootDepth;
    std::vector<std::string> root;
    for (const PickedFile& file : files) {
        auto parts = SplitPath(file.path);
        if (parts.empty()) {
            error = "unsafe picked path: " + file.path;
            return nullptr;
        }
        const std::string& leaf = parts.back();
        if ((ascii::EqualsNoCase(leaf, kExePath) || ascii::EqualsNoCase(leaf, kCabinetHeaderPath)) &&
                (!rootDepth || parts.size() - 1 < *rootDepth)) {
            rootDepth = parts.size() - 1;
            root.assign(parts.begin(), parts.end() - 1);
        }
        paths.push_back(std::move(parts));
    }
    if (!rootDepth) {
        error = "no Beach Life installation or CD among the picked files (no BeachLife.exe or data1.hdr)";
        return nullptr;
    }
    std::map<std::string, File, std::less<>> byPath;
    std::map<std::string, std::vector<SourceEntry>, std::less<>> dirs;
    std::set<std::string, std::less<>> listed; // "<dir>/<name>" already in dirs
    dirs[""];
    for (size_t index = 0; index < paths.size(); ++index) {
        const auto& parts = paths[index];
        if (parts.size() <= root.size() || !std::equal(root.begin(), root.end(), parts.begin())) continue; // outside the game folder
        const std::string path = Join(parts, root.size(), parts.size());
        if (!byPath.emplace(path, File{index, files[index].size}).second || dirs.contains(path)) {
            error = "picked twice or both file and folder: " + path;
            return nullptr;
        }
        for (size_t depth = root.size(); depth < parts.size(); ++depth) {
            const std::string parent = Join(parts, root.size(), depth);
            const std::string child = Join(parts, root.size(), depth + 1);
            const bool directory = depth + 1 < parts.size();
            if (directory && byPath.contains(child)) {
                error = "picked twice or both file and folder: " + child;
                return nullptr;
            }
            if (listed.insert(child).second) dirs[parent].push_back({parts[depth], directory});
            if (directory) dirs[child];
        }
    }
    return std::make_unique<PickedFilesSource>(std::move(byPath), std::move(dirs), std::move(read));
}

} // namespace obl::convert::web
