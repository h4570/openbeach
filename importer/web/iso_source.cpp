// ISO 9660 (ECMA-119) with the Joliet extension, read-only and lazily: a directory is parsed when it is first listed.
// Every extent is checked against the image size, so a damaged image gives errors, never reads past its end.

#include "importer/web/web_source.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>

namespace obl::convert::web {
namespace {

constexpr uint64_t kSectorSize = 2048;
constexpr uint64_t kFirstDescriptor = 16;
// A volume descriptor set longer than this is damage, not a disc.
constexpr uint64_t kMaxDescriptors = 64;
// No directory of a game disc comes near this; a larger one is damage.
constexpr uint32_t kMaxDirectoryBytes = 16u << 20;
constexpr size_t kRecordHeader = 33;

uint32_t Le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
            static_cast<uint32_t>(p[3]) << 24;
}

struct Extent {
    uint64_t offset = 0; // byte offset in the image
    uint64_t size = 0;
};

struct Node {
    std::string name;
    bool directory = false;
    std::vector<Extent> extents; // a multi-extent file has several, in order
    uint64_t Size() const
    {
        uint64_t total = 0;
        for (const Extent& e : extents) total += e.size;
        return total;
    }
};

void AppendUtf8(std::string& out, uint32_t c)
{
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | c >> 6);
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | c >> 12);
        out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | c >> 18);
        out += static_cast<char>(0x80 | (c >> 12 & 0x3F));
        out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Joliet names are UCS-2 big-endian (surrogate pairs pass through as UTF-16); primary names are d-characters.
std::string DecodeName(const uint8_t* id, size_t length, bool joliet)
{
    std::string name;
    if (joliet) {
        for (size_t i = 0; i + 1 < length; i += 2) {
            uint32_t c = static_cast<uint32_t>(id[i]) << 8 | id[i + 1];
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < length) {
                const uint32_t low = static_cast<uint32_t>(id[i + 2]) << 8 | id[i + 3];
                if (low >= 0xDC00 && low < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                }
            }
            AppendUtf8(name, c);
        }
    } else {
        name.assign(reinterpret_cast<const char*>(id), length);
    }
    if (const auto semicolon = name.rfind(';'); semicolon != std::string::npos) name.resize(semicolon);
    if (!name.empty() && name.back() == '.') name.pop_back();
    return name;
}

class IsoSource final : public ByteSource {
public:
    IsoSource(uint64_t size, ReadAt read, Node root, bool joliet)
        : size_(size), read_(std::move(read)), joliet_(joliet), root_(std::move(root))
    {}

    std::optional<std::vector<SourceEntry>> List(std::string_view directory) const override
    {
        const Node* node = Resolve(directory);
        if (!node || !node->directory) return std::nullopt;
        const auto* children = Children(*node, std::string(directory));
        if (!children) return std::nullopt;
        std::vector<SourceEntry> entries;
        entries.reserve(children->size());
        for (const Node& child : *children) entries.push_back({child.name, child.directory});
        return entries;
    }

    std::optional<uint64_t> Size(std::string_view path) const override
    {
        const Node* node = Resolve(path);
        if (!node || node->directory) return std::nullopt;
        return node->Size();
    }

    bool Read(std::string_view path, uint64_t offset, std::span<uint8_t> out) const override
    {
        const Node* node = Resolve(path);
        if (!node || node->directory || offset > node->Size() || out.size() > node->Size() - offset) return false;
        size_t done = 0;
        for (const Extent& extent : node->extents) {
            if (done == out.size()) break;
            if (offset >= extent.size) {
                offset -= extent.size;
                continue;
            }
            const size_t take = static_cast<size_t>(std::min<uint64_t>(extent.size - offset, out.size() - done));
            if (!read_(extent.offset + offset, out.subspan(done, take))) return false;
            done += take;
            offset = 0;
        }
        return done == out.size();
    }

    std::string Describe(std::string_view path) const override { return "disc image: " + std::string(path); }

private:
    const Node* Resolve(std::string_view path) const
    {
        const Node* node = &root_;
        std::string prefix;
        while (!path.empty()) {
            const size_t slash = path.find('/');
            const std::string_view part = path.substr(0, slash);
            const auto* children = node->directory ? Children(*node, prefix) : nullptr;
            if (!children) return nullptr;
            const auto it = std::find_if(children->begin(), children->end(), [&](const Node& n) { return n.name == part; });
            if (it == children->end()) return nullptr;
            node = &*it;
            prefix += prefix.empty() ? std::string(part) : "/" + std::string(part);
            path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
        }
        return node;
    }

    // The parsed records of `node` (cached by path); nullptr when the directory cannot be read.
    const std::vector<Node>* Children(const Node& node, const std::string& path) const
    {
        if (const auto it = cache_.find(path); it != cache_.end()) return it->second ? &*it->second : nullptr;
        auto& slot = cache_[path];
        if (node.extents.size() != 1 || node.extents[0].size > kMaxDirectoryBytes) return nullptr;
        std::vector<uint8_t> bytes(static_cast<size_t>(node.extents[0].size));
        if (!read_(node.extents[0].offset, bytes)) return nullptr;
        std::vector<Node> children;
        bool continued = false;
        size_t at = 0;
        while (at < bytes.size()) {
            const size_t length = bytes[at];
            if (length == 0) { // records never cross a sector: the rest of this one is padding
                at = (at / kSectorSize + 1) * kSectorSize;
                continue;
            }
            if (length < kRecordHeader || at + length > bytes.size()) return nullptr;
            const uint8_t* record = bytes.data() + at;
            at += length;
            const size_t idLength = record[32];
            if (kRecordHeader + idLength > length) return nullptr;
            if (idLength == 1 && (record[33] == 0 || record[33] == 1)) continue; // "." and ".."
            const uint8_t flags = record[25];
            const Extent extent{static_cast<uint64_t>(Le32(record + 2)) * kSectorSize, Le32(record + 10)};
            if (extent.offset > size_ || extent.size > size_ - extent.offset) return nullptr;
            std::string name = DecodeName(record + 33, idLength, joliet_);
            if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) return nullptr;
            // The records of a multi-extent file follow each other; all but the last carry flag 0x80.
            if (!children.empty() && continued && children.back().name == name) {
                children.back().extents.push_back(extent);
            } else {
                children.push_back({std::move(name), (flags & 0x02) != 0, {extent}});
            }
            continued = (flags & 0x80) != 0;
        }
        slot = std::move(children);
        return &*slot;
    }

    uint64_t size_;
    ReadAt read_;
    bool joliet_;
    Node root_;
    mutable std::map<std::string, std::optional<std::vector<Node>>, std::less<>> cache_;
};

} // namespace

std::unique_ptr<ByteSource> OpenIsoSource(uint64_t size, ReadAt read, std::string& error)
{
    std::optional<Node> primary, joliet;
    std::array<uint8_t, kSectorSize> sector{};
    for (uint64_t index = kFirstDescriptor; index < kFirstDescriptor + kMaxDescriptors; ++index) {
        if ((index + 1) * kSectorSize > size || !read(index * kSectorSize, sector)) break;
        if (std::string_view(reinterpret_cast<const char*>(sector.data() + 1), 5) != "CD001") break;
        const uint8_t type = sector[0];
        if (type == 255) break; // the set terminator
        if (type != 1 && type != 2) continue;
        const uint8_t* root = sector.data() + 156;
        const Extent extent{static_cast<uint64_t>(Le32(root + 2)) * kSectorSize, Le32(root + 10)};
        if (root[0] < kRecordHeader || (root[25] & 0x02) == 0 || extent.offset > size || extent.size > size - extent.offset) continue;
        Node node{"", true, {extent}};
        // Joliet: a supplementary descriptor whose escape sequence names UCS-2 level 1, 2 or 3.
        const uint8_t* escape = sector.data() + 88;
        if (type == 1 && !primary)
            primary = std::move(node);
        else if (type == 2 && escape[0] == '%' && escape[1] == '/' && (escape[2] == '@' || escape[2] == 'C' || escape[2] == 'E') && !joliet)
            joliet = std::move(node);
    }
    if (!primary) {
        error = "not an ISO 9660 disc image (no primary volume descriptor)";
        return nullptr;
    }
    const bool useJoliet = joliet.has_value();
    return std::make_unique<IsoSource>(size, std::move(read), useJoliet ? std::move(*joliet) : std::move(*primary), useJoliet);
}

} // namespace obl::convert::web
