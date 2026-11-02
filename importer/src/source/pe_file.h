#pragma once

// Read-only view of a PE32 executable as a file, for the converter (docs/architecture/data-formats/converter/index.md). It maps
// virtual addresses to file bytes and nothing more: no loader, no relocations, no Windows API, so it builds on Linux.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

struct PeSection {
    std::string name;
    uint32_t rva = 0;
    uint32_t virtualSize = 0;
    uint32_t rawOffset = 0;
    uint32_t rawSize = 0;
    // Scan without relying on code flags: the UK/US CD labels every section as read/write data.
    uint32_t characteristics = 0;
};

class PeFile {
public:
    // Parses the headers of a PE32 i386 file; `error` names the first problem.
    static std::optional<PeFile> Parse(std::vector<uint8_t> bytes, std::string& error);

    std::span<const uint8_t> Bytes() const { return bytes_; }
    uint32_t ImageBase() const { return imageBase_; }
    const std::vector<PeSection>& Sections() const { return sections_; }
    const PeSection* SectionAt(uint32_t va) const;

    // File bytes of [va, va + size), or an empty span when the range is not backed by one section's raw data.
    std::span<const uint8_t> Read(uint32_t va, uint32_t size) const;
    std::optional<uint32_t> FileOffsetOf(uint32_t va) const;
    std::optional<uint32_t> VaOfFileOffset(size_t offset) const;
    std::optional<uint32_t> ReadU32(uint32_t va) const;
    // The NUL-terminated string at `va` (at most `maxLength` characters), or nullopt without a terminator.
    std::optional<std::string_view> CString(uint32_t va, size_t maxLength = 256) const;

private:
    std::vector<uint8_t> bytes_;
    uint32_t imageBase_ = 0;
    std::vector<PeSection> sections_;
};

} // namespace obl::convert
