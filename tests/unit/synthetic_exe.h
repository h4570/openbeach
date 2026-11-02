#pragma once

// A hand-made PE32 executable for the converter tests (convert_exe_tables_test.cpp, convert_gui_tables_test.cpp,
// convert_resource_test.cpp, convert_script_vocabulary_test.cpp, convert_source_test.cpp, convert_import_test.cpp): our own bytes only, built
// section by section. PeFile::Parse reads the headers; the readers find their tables by content in the sections.

#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::test {

inline void PutU16(std::vector<uint8_t>& b, size_t at, uint16_t v)
{
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

inline void PutU32(std::vector<uint8_t>& b, size_t at, uint32_t v)
{
    for (size_t i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

// Headers in the first kHeaderSize bytes, then each added section's raw bytes in order, every one starting on a
// kFileAlignment boundary. `bytes` is the whole file; fixtures write into a section through the returned file offset.
struct SyntheticExe {
    static constexpr uint32_t kImageBase = 0x400000;
    static constexpr size_t kHeaderSize = 0x400;
    static constexpr size_t kFileAlignment = 0x200;
    static constexpr size_t kPe = 0x40;
    static constexpr size_t kOptional = kPe + 24;
    static constexpr size_t kSectionTable = kOptional + 0xE0;
    static constexpr uint32_t kCodeFlags = 0x60000020; // code, execute, read
    static constexpr uint32_t kDataFlags = 0xC0000040; // initialized data, read, write

    std::vector<uint8_t> bytes;

    SyntheticExe() : bytes(kHeaderSize, 0)
    {
        bytes[0] = 'M';
        bytes[1] = 'Z';
        PutU32(bytes, 0x3C, static_cast<uint32_t>(kPe));
        std::memcpy(&bytes[kPe], "PE\0\0", 4);
        PutU16(bytes, kPe + 4, 0x14C); // Machine: i386
        PutU16(bytes, kPe + 20, 0xE0); // SizeOfOptionalHeader
        PutU16(bytes, kPe + 22, 0x102); // Characteristics: executable image, 32-bit
        PutU16(bytes, kOptional, 0x10B); // PE32
        PutU32(bytes, kOptional + 28, kImageBase);
        PutU32(bytes, kOptional + 32, 0x1000); // SectionAlignment
        PutU32(bytes, kOptional + 36, static_cast<uint32_t>(kFileAlignment));
        PutU32(bytes, kOptional + 92, 16); // NumberOfRvaAndSizes
    }

    // Appends a zero-filled section of `size` bytes at `rva`; returns its file offset.
    size_t AddSection(std::string_view name, uint32_t rva, uint32_t size, uint32_t flags)
    {
        const uint16_t index = Sections();
        const size_t entry = kSectionTable + size_t{index} * 40;
        const size_t raw = (bytes.size() + kFileAlignment - 1) & ~(kFileAlignment - 1);
        bytes.resize(raw + size, 0);
        std::memcpy(&bytes[entry], name.data(), name.size() < 8 ? name.size() : 8);
        PutU32(bytes, entry + 8, size); // VirtualSize
        PutU32(bytes, entry + 12, rva);
        PutU32(bytes, entry + 16, size); // SizeOfRawData
        PutU32(bytes, entry + 20, static_cast<uint32_t>(raw));
        PutU32(bytes, entry + 36, flags);
        PutU16(bytes, kPe + 6, static_cast<uint16_t>(index + 1));
        return raw;
    }

    // Appends a section holding `data`; returns its file offset.
    size_t AddSection(std::string_view name, uint32_t rva, std::span<const uint8_t> data, uint32_t flags)
    {
        const size_t raw = AddSection(name, rva, static_cast<uint32_t>(data.size()), flags);
        std::copy(data.begin(), data.end(), bytes.begin() + static_cast<std::ptrdiff_t>(raw));
        return raw;
    }

    uint16_t Sections() const { return static_cast<uint16_t>(bytes[kPe + 6] | (bytes[kPe + 7] << 8)); }
};

// Resources by type, then id: the bytes of each. Keys are 32-bit so brace lists of int literals do not narrow.
using SyntheticResources = std::map<uint32_t, std::map<uint32_t, std::vector<uint8_t>>>;

// The three-level resource tree (type, id, one language) of a .rsrc section placed at `rva`: the root directory, the type
// directories, the language directories, the data entries, then the data.
inline std::vector<uint8_t> ResourceSection(const SyntheticResources& resources, uint32_t rva)
{
    std::vector<uint8_t> rsrc;
    auto put16 = [&](uint32_t v) {
        rsrc.push_back(static_cast<uint8_t>(v));
        rsrc.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto put32 = [&](uint32_t v) {
        put16(v & 0xFFFF);
        put16(v >> 16);
    };
    auto directory = [&](size_t entries) {
        for (int i = 0; i < 3; ++i) put32(0);
        put16(0);
        put16(static_cast<uint32_t>(entries));
    };
    size_t leaves = 0;
    for (const auto& [type, ids] : resources) leaves += ids.size();
    const size_t typeDirs = 16 + resources.size() * 8;
    size_t languageDirs = typeDirs;
    for (const auto& [type, ids] : resources) languageDirs += 16 + ids.size() * 8;
    const size_t entries = languageDirs + leaves * 24;
    const size_t data = entries + leaves * 16;

    directory(resources.size());
    size_t typeAt = typeDirs;
    for (const auto& [type, ids] : resources) {
        put32(type);
        put32(0x80000000u | static_cast<uint32_t>(typeAt));
        typeAt += 16 + ids.size() * 8;
    }
    size_t languageAt = languageDirs;
    for (const auto& [type, ids] : resources) {
        directory(ids.size());
        for (const auto& [id, bytes] : ids) {
            put32(id);
            put32(0x80000000u | static_cast<uint32_t>(languageAt));
            languageAt += 24;
        }
    }
    size_t entryAt = entries;
    for (size_t n = 0; n < leaves; ++n) {
        directory(1);
        put32(0x409);
        put32(static_cast<uint32_t>(entryAt));
        entryAt += 16;
    }
    size_t dataAt = data;
    for (const auto& [type, ids] : resources)
        for (const auto& [id, bytes] : ids) {
            put32(rva + static_cast<uint32_t>(dataAt));
            put32(static_cast<uint32_t>(bytes.size()));
            put32(0);
            put32(0);
            dataAt += bytes.size();
        }
    for (const auto& [type, ids] : resources)
        for (const auto& [id, bytes] : ids) rsrc.insert(rsrc.end(), bytes.begin(), bytes.end());
    return rsrc;
}

// The data of a section at `dataVa` holding the script vocabulary (importer/src/readers/script_vocabulary.cpp):
// `states` state records numbered from 0 (slot 3 empty), then `messages` message records numbered by index, then the
// names they point at.
inline std::vector<uint8_t> ScriptTableData(size_t states, size_t messages, uint32_t dataVa)
{
    const size_t rows = states + messages;
    std::vector<uint8_t> data(8 + rows * 8 + 8, 0); // a null record before and after the run
    std::vector<std::string> names;
    for (size_t i = 0; i < states; ++i) names.push_back("STATE_OWN_" + std::to_string(i));
    for (size_t i = 0; i < messages; ++i) names.push_back("MT_OWN_" + std::to_string(i));
    for (size_t i = 0; i < rows; ++i) {
        const size_t at = 8 + i * 8;
        const uint32_t id = static_cast<uint32_t>(i < states ? i : i - states);
        const uint32_t nameVa = dataVa + static_cast<uint32_t>(data.size());
        data.insert(data.end(), names[i].begin(), names[i].end());
        data.push_back(0);
        PutU32(data, at, id);
        PutU32(data, at + 4, i == 3 ? 0 : nameVa);
    }
    data[0] = 0xFF; // the record before the run has an id over 16 bits, so the run starts at the first state
    data[3] = 0xFF;
    return data;
}

} // namespace obl::test
