#pragma once

// A synthetic ISO 9660 (ECMA-119) disc image for the converter tests: files are added by path and Build lays out the
// volume descriptors, the file data and the directories itself, so a test can put a whole synthetic CD (cabinets, a
// loose executable) behind web::OpenIsoSource. The primary tree has upper-case names with a ";1" version on files;
// the Joliet tree (on by default) keeps the paths as given. ASCII paths only.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::test {

struct SyntheticIso {
    static constexpr size_t kSector = 2048;

    std::map<std::string, std::vector<uint8_t>> files; // path with '/' -> content

    void Add(const std::string& path, std::span<const uint8_t> content) { files[path].assign(content.begin(), content.end()); }

    std::vector<uint8_t> Build(bool joliet = true) const
    {
        Directory root;
        for (const auto& [path, content] : files) {
            Directory* dir = &root;
            size_t start = 0;
            for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', start)) {
                dir = &dir->directories[path.substr(start, slash - start)];
                start = slash + 1;
            }
            dir->files[path.substr(start)] = {&content, 0};
        }

        // Descriptors from sector 16 (primary, Joliet, terminator), then file data, then each tree's directories.
        uint32_t next = 16 + (joliet ? 3 : 2);
        PlaceFiles(root, next);
        Layout primary = PlaceDirectories(root, false, next);
        Layout supplementary = joliet ? PlaceDirectories(root, true, next) : Layout{};

        std::vector<uint8_t> image(size_t{next} * kSector, 0);
        WriteFiles(root, image);
        WriteDescriptor(image, 16, 1, primary.at(&root), false);
        WriteDirectories(image, root, root, primary, false);
        if (joliet) {
            WriteDescriptor(image, 17, 2, supplementary.at(&root), true);
            WriteDirectories(image, root, root, supplementary, true);
        }
        uint8_t* terminator = &image[size_t{joliet ? 18u : 17u} * kSector];
        terminator[0] = 255;
        std::memcpy(terminator + 1, "CD001", 5);
        terminator[6] = 1;
        return image;
    }

private:
    struct FileEntry {
        const std::vector<uint8_t>* content = nullptr;
        uint32_t sector = 0;
    };
    struct Directory {
        std::map<std::string, Directory> directories;
        std::map<std::string, FileEntry> files;
    };
    struct Extent {
        uint32_t sector = 0;
        uint32_t size = 0;
    };
    using Layout = std::map<const Directory*, Extent>;

    static uint32_t Sectors(size_t bytes) { return static_cast<uint32_t>(std::max<size_t>(1, (bytes + kSector - 1) / kSector)); }

    static std::vector<uint8_t> Identifier(std::string_view name, bool joliet, bool file)
    {
        std::vector<uint8_t> id;
        for (char c : name) {
            if (joliet) id.push_back(0);
            id.push_back(static_cast<uint8_t>(joliet ? c : std::toupper(static_cast<unsigned char>(c))));
        }
        if (file && !joliet)
            for (char c : std::string_view(";1")) id.push_back(static_cast<uint8_t>(c));
        return id;
    }

    static void PutBothEndian32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            p[i] = static_cast<uint8_t>(v >> (8 * i));
            p[7 - i] = static_cast<uint8_t>(v >> (8 * i));
        }
    }

    // One directory record (ECMA-119 9.1); an even-length identifier is followed by a padding byte.
    static std::vector<uint8_t> Record(Extent extent, bool directory, const std::vector<uint8_t>& id)
    {
        std::vector<uint8_t> r(33 + id.size() + (id.size() % 2 == 0 ? 1 : 0));
        r[0] = static_cast<uint8_t>(r.size());
        PutBothEndian32(&r[2], extent.sector);
        PutBothEndian32(&r[10], extent.size);
        r[25] = directory ? 0x02 : 0;
        r[28] = 1;
        r[31] = 1;
        r[32] = static_cast<uint8_t>(id.size());
        std::copy(id.begin(), id.end(), r.begin() + 33);
        return r;
    }

    // The records of `dir` with placeholder extents: their sizes decide the directory's extent.
    static std::vector<std::vector<uint8_t>> Records(const Directory& dir, const Layout* layout, Extent entry, Extent parent, bool joliet)
    {
        std::vector<std::vector<uint8_t>> records = {Record(entry, true, {0}), Record(parent, true, {1})};
        for (const auto& [name, child] : dir.directories)
            records.push_back(Record(layout ? layout->at(&child) : Extent{}, true, Identifier(name, joliet, false)));
        for (const auto& [name, file] : dir.files)
            records.push_back(Record({file.sector, static_cast<uint32_t>(file.content->size())}, false, Identifier(name, joliet, true)));
        return records;
    }

    // Packs records into sectors without letting one cross a sector boundary; returns the byte size (whole sectors).
    static size_t Pack(const std::vector<std::vector<uint8_t>>& records, uint8_t* out)
    {
        size_t at = 0;
        for (const auto& r : records) {
            if (at / kSector != (at + r.size() - 1) / kSector) at = (at / kSector + 1) * kSector;
            if (out) std::copy(r.begin(), r.end(), out + at);
            at += r.size();
        }
        return size_t{Sectors(at)} * kSector;
    }

    static void PlaceFiles(Directory& dir, uint32_t& next)
    {
        for (auto& [name, file] : dir.files) {
            file.sector = next;
            next += Sectors(file.content->size());
        }
        for (auto& [name, child] : dir.directories) PlaceFiles(child, next);
    }

    static void WriteFiles(const Directory& dir, std::vector<uint8_t>& image)
    {
        for (const auto& [name, file] : dir.files)
            std::copy(
                    file.content->begin(), file.content->end(), image.begin() + static_cast<std::ptrdiff_t>(size_t{file.sector} * kSector));
        for (const auto& [name, child] : dir.directories) WriteFiles(child, image);
    }

    static Layout PlaceDirectories(const Directory& root, bool joliet, uint32_t& next)
    {
        Layout layout;
        Place(root, joliet, next, layout);
        return layout;
    }

    static void Place(const Directory& dir, bool joliet, uint32_t& next, Layout& layout)
    {
        const size_t bytes = Pack(Records(dir, nullptr, {}, {}, joliet), nullptr);
        layout[&dir] = {next, static_cast<uint32_t>(bytes)};
        next += Sectors(bytes);
        for (const auto& [name, child] : dir.directories) Place(child, joliet, next, layout);
    }

    static void WriteDirectories(
            std::vector<uint8_t>& image, const Directory& dir, const Directory& parent, const Layout& layout, bool joliet)
    {
        const Extent entry = layout.at(&dir);
        Pack(Records(dir, &layout, entry, layout.at(&parent), joliet), &image[size_t{entry.sector} * kSector]);
        for (const auto& [name, child] : dir.directories) WriteDirectories(image, child, dir, layout, joliet);
    }

    static void WriteDescriptor(std::vector<uint8_t>& image, size_t sector, uint8_t type, Extent root, bool joliet)
    {
        uint8_t* d = &image[sector * kSector];
        d[0] = type;
        std::memcpy(d + 1, "CD001", 5);
        d[6] = 1;
        if (joliet) std::memcpy(d + 88, "%/E", 3); // UCS-2 level 3
        PutBothEndian32(d + 80, static_cast<uint32_t>(image.size() / kSector)); // volume space size
        const auto record = Record(root, true, {0});
        std::copy(record.begin(), record.end(), d + 156);
    }
};

} // namespace obl::test
