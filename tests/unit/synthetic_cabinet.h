#pragma once

// A synthetic InstallShield cabinet (data1.hdr and one volume data1.cab, importer/src/source/cabinet.cpp) holding the
// given files in one file group, each compressed as stored deflate chunks: the CD form of a synthetic installation for
// convert_import_test.cpp. convert_cabinet_test.cpp keeps its own fixture for the format's corner cases.

#include "importer/src/source/cabinet.h"
#include "importer/src/source/md5.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace obl::test {

struct SyntheticCabinetFiles {
    std::vector<uint8_t> hdr, cab;
};

namespace cabinet_detail {

inline void Put(std::vector<uint8_t>& b, size_t at, uint64_t v, size_t bytes)
{
    if (b.size() < at + bytes) b.resize(at + bytes);
    for (size_t i = 0; i < bytes; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

inline void PutString(std::vector<uint8_t>& b, size_t at, std::string_view s)
{
    if (b.size() < at + s.size() + 1) b.resize(at + s.size() + 1);
    std::memcpy(&b[at], s.data(), s.size());
    b[at + s.size()] = 0;
}

// The compressed form of `data`: chunks of a u16 length and a raw deflate stream of one stored block, each but the last
// ending like a sync flush, the last with BFINAL.
inline std::vector<uint8_t> Compressed(const std::vector<uint8_t>& data)
{
    constexpr size_t kChunk = 0x4000;
    std::vector<uint8_t> out;
    size_t at = 0;
    do {
        const size_t len = std::min(kChunk, data.size() - at);
        const bool final = at + len == data.size();
        const size_t chunk = out.size();
        Put(out, chunk + 2, final ? 1 : 0, 1);
        Put(out, chunk + 3, len, 2);
        Put(out, chunk + 5, static_cast<uint16_t>(~len), 2);
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + len));
        Put(out, chunk, out.size() - chunk - 2, 2);
        at += len;
    } while (at < data.size());
    return out;
}

} // namespace cabinet_detail

// `files` by installation path ('/'-separated), all in the file group `group`.
inline SyntheticCabinetFiles SyntheticCabinet(const std::map<std::string, std::vector<uint8_t>>& files, std::string_view group)
{
    using cabinet_detail::Put;
    using cabinet_detail::PutString;
    constexpr uint32_t kMagic = 0x28635349, kVersion = 0x0100600C;
    constexpr size_t kDesc = 0x200; // the cabinet descriptor
    constexpr size_t kTable = 0x400; // the file table, relative to the descriptor
    constexpr size_t kDescriptorSize = 0x57;
    SyntheticCabinetFiles out;
    Put(out.hdr, 0, kMagic, 4);
    Put(out.hdr, 4, kVersion, 4);
    Put(out.hdr, 12, kDesc, 4);
    Put(out.cab, 0, kMagic, 4);
    Put(out.cab, 4, kVersion, 4);
    out.cab.resize(0x100);

    // Directories as the header spells them (backslashes), then the names.
    std::vector<std::string> directories;
    std::vector<std::pair<size_t, std::string>> entries; // directory index, name
    for (const auto& [path, bytes] : files) {
        const size_t slash = path.rfind('/');
        std::string directory = slash == std::string::npos ? "" : path.substr(0, slash);
        std::replace(directory.begin(), directory.end(), '/', '\\');
        auto found = std::find(directories.begin(), directories.end(), directory);
        if (found == directories.end()) found = directories.insert(directories.end(), directory);
        entries.emplace_back(static_cast<size_t>(found - directories.begin()), path.substr(slash + 1));
    }
    const size_t table = kDesc + kTable;
    size_t strings = 4 * directories.size();
    auto addString = [&](std::string_view s) {
        const size_t at = strings;
        PutString(out.hdr, table + at, s);
        strings += s.size() + 1;
        return at;
    };
    for (size_t i = 0; i < directories.size(); ++i) Put(out.hdr, table + 4 * i, addString(directories[i]), 4);
    std::vector<size_t> names;
    for (const auto& [directory, name] : entries) names.push_back(addString(name));
    const size_t descriptors = (strings + 0xF) & ~size_t{0xF};
    Put(out.hdr, kDesc + 0x0C, kTable, 4);
    Put(out.hdr, kDesc + 0x1C, directories.size(), 4);
    Put(out.hdr, kDesc + 0x28, files.size(), 4);
    Put(out.hdr, kDesc + 0x2C, descriptors, 4);

    size_t i = 0;
    for (const auto& [path, bytes] : files) {
        const size_t fd = table + descriptors + i * kDescriptorSize;
        out.hdr.resize(std::max(out.hdr.size(), fd + kDescriptorSize));
        const std::vector<uint8_t> stored = cabinet_detail::Compressed(bytes);
        const size_t offset = out.cab.size();
        out.cab.insert(out.cab.end(), stored.begin(), stored.end());
        Put(out.hdr, fd + 0x00, convert::CabinetFile::kCompressed, 2);
        Put(out.hdr, fd + 0x02, bytes.size(), 8);
        Put(out.hdr, fd + 0x0A, stored.size(), 8);
        Put(out.hdr, fd + 0x12, offset, 8);
        const convert::Md5Digest md5 = convert::Md5Of(bytes);
        std::copy(md5.begin(), md5.end(), out.hdr.begin() + static_cast<std::ptrdiff_t>(fd + 0x1A));
        Put(out.hdr, fd + 0x3A, names[i], 4);
        Put(out.hdr, fd + 0x3E, entries[i].first, 2);
        Put(out.hdr, fd + 0x55, 1, 2); // the volume
        ++i;
    }

    // One group node in slot 0 of the group table; its descriptor names the group and owns every file.
    const size_t node = ((kTable + descriptors + files.size() * kDescriptorSize) + 0xFF) & ~size_t{0xFF};
    const size_t descriptor = node + 0x10;
    Put(out.hdr, kDesc + 0x3E, node, 4);
    Put(out.hdr, kDesc + node + 4, descriptor, 4);
    Put(out.hdr, kDesc + node + 8, 0, 4); // no next node
    Put(out.hdr, kDesc + descriptor, descriptor + 0x40, 4);
    PutString(out.hdr, kDesc + descriptor + 0x40, group);
    Put(out.hdr, kDesc + descriptor + 0x16, 0, 4);
    Put(out.hdr, kDesc + descriptor + 0x1A, files.size() - 1, 4);
    out.hdr.resize(std::max(out.hdr.size(), kDesc + 0x3E + 4 * 71)); // every group slot present
    return out;
}

} // namespace obl::test
