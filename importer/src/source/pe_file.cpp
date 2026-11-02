#include "importer/src/source/pe_file.h"

#include <algorithm>
#include <cstring>

namespace obl::convert {

namespace {

uint32_t LoadU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
            static_cast<uint32_t>(p[3]) << 24;
}

uint16_t LoadU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

} // namespace

std::optional<PeFile> PeFile::Parse(std::vector<uint8_t> bytes, std::string& error)
{
    error.clear();
    if (bytes.size() < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') {
        error = "not an MZ executable";
        return std::nullopt;
    }
    const uint32_t pe = LoadU32(&bytes[0x3C]);
    if (pe > bytes.size() || bytes.size() - pe < 24 || std::memcmp(&bytes[pe], "PE\0\0", 4) != 0) {
        error = "no PE header";
        return std::nullopt;
    }
    if (LoadU16(&bytes[pe + 4]) != 0x14C) {
        error = "not an i386 image";
        return std::nullopt;
    }
    const uint16_t count = LoadU16(&bytes[pe + 6]);
    const uint16_t optionalSize = LoadU16(&bytes[pe + 20]);
    const size_t optional = size_t{pe} + 24;
    const size_t table = optional + optionalSize;
    if (optionalSize < 96 || table + size_t{count} * 40 > bytes.size() || LoadU16(&bytes[optional]) != 0x10B) {
        error = "not a PE32 optional header or truncated section table";
        return std::nullopt;
    }

    PeFile file;
    file.imageBase_ = LoadU32(&bytes[optional + 28]);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* s = &bytes[table + size_t{i} * 40];
        PeSection section;
        const char* name = reinterpret_cast<const char*>(s);
        section.name.assign(name, std::find(name, name + 8, '\0'));
        section.virtualSize = LoadU32(s + 8);
        section.rva = LoadU32(s + 12);
        section.rawSize = LoadU32(s + 16);
        section.rawOffset = LoadU32(s + 20);
        section.characteristics = LoadU32(s + 36);
        if (section.rawOffset > bytes.size() || bytes.size() - section.rawOffset < section.rawSize) {
            error = "section " + section.name + " lies outside the file";
            return std::nullopt;
        }
        file.sections_.push_back(std::move(section));
    }
    file.bytes_ = std::move(bytes);
    return file;
}

const PeSection* PeFile::SectionAt(uint32_t va) const
{
    if (va < imageBase_) return nullptr;
    const uint32_t rva = va - imageBase_;
    for (const PeSection& s : sections_) {
        const uint32_t span = s.virtualSize > s.rawSize ? s.virtualSize : s.rawSize;
        if (rva >= s.rva && rva - s.rva < span) return &s;
    }
    return nullptr;
}

std::optional<uint32_t> PeFile::FileOffsetOf(uint32_t va) const
{
    const PeSection* s = SectionAt(va);
    if (!s) return std::nullopt;
    const uint32_t delta = va - imageBase_ - s->rva;
    if (delta >= s->rawSize) return std::nullopt;
    return s->rawOffset + delta;
}

std::optional<uint32_t> PeFile::VaOfFileOffset(size_t offset) const
{
    for (const PeSection& s : sections_)
        if (offset >= s.rawOffset && offset - s.rawOffset < s.rawSize)
            return imageBase_ + s.rva + static_cast<uint32_t>(offset - s.rawOffset);
    return std::nullopt;
}

std::span<const uint8_t> PeFile::Read(uint32_t va, uint32_t size) const
{
    const PeSection* s = SectionAt(va);
    if (!s) return {};
    const uint32_t delta = va - imageBase_ - s->rva;
    if (delta > s->rawSize || s->rawSize - delta < size) return {};
    return std::span<const uint8_t>(bytes_).subspan(s->rawOffset + delta, size);
}

std::optional<uint32_t> PeFile::ReadU32(uint32_t va) const
{
    auto bytes = Read(va, 4);
    if (bytes.empty()) return std::nullopt;
    return LoadU32(bytes.data());
}

std::optional<std::string_view> PeFile::CString(uint32_t va, size_t maxLength) const
{
    auto offset = FileOffsetOf(va);
    if (!offset) return std::nullopt;
    const PeSection* s = SectionAt(va);
    const size_t end = size_t{s->rawOffset} + s->rawSize;
    const char* start = reinterpret_cast<const char*>(bytes_.data() + *offset);
    for (size_t i = 0; i <= maxLength && *offset + i < end; ++i)
        if (start[i] == '\0') return std::string_view(start, i);
    return std::nullopt;
}

} // namespace obl::convert
