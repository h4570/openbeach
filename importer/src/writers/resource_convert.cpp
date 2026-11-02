#include "importer/src/writers/resource_convert.h"

#include "importer/src/source/pe_file.h"
#include "importer/src/source/source.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

namespace obl::convert {

namespace {

constexpr uint16_t kResCursor = 1;
constexpr uint16_t kResIcon = 3;
constexpr uint16_t kResGroupCursor = 12;
constexpr uint16_t kResGroupIcon = 14;

uint16_t U16(std::span<const uint8_t> b, size_t at)
{
    return uint16_t(b[at] | b[at + 1] << 8);
}

uint32_t U32(std::span<const uint8_t> b, size_t at)
{
    return uint32_t(U16(b, at)) | uint32_t(U16(b, at + 2)) << 16;
}

void Put16(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t v)
{
    Put16(out, v & 0xFFFF);
    Put16(out, v >> 16);
}

// Walks the resource directory of the .rsrc section: type -> id -> first language leaf. Returns the leaf's data bytes.
class Resources {
public:
    Resources(const PeFile& pe, const PeSection& section) : m_section(section)
    {
        const auto bytes = pe.Bytes();
        if (section.rawOffset <= bytes.size())
            m_data = bytes.subspan(section.rawOffset, std::min<size_t>(section.rawSize, bytes.size() - section.rawOffset));
    }

    std::optional<std::span<const uint8_t>> Find(uint16_t type, uint16_t id) const
    {
        const auto typeDir = Child(0, type);
        if (!typeDir) return std::nullopt;
        const auto idDir = Child(*typeDir, id);
        if (!idDir) return std::nullopt;
        return Leaf(*idDir);
    }

private:
    // The entry named by integer `key` in the directory at `dir`; its offset (high bit: another directory, else a data entry).
    std::optional<uint32_t> Entry(uint32_t dir, uint32_t key) const
    {
        if (dir + 16 > m_data.size()) return std::nullopt;
        const size_t named = U16(m_data, dir + 12);
        const size_t ids = U16(m_data, dir + 14);
        for (size_t i = named; i < named + ids; ++i) {
            const size_t at = dir + 16 + i * 8;
            if (at + 8 > m_data.size()) return std::nullopt;
            if (U32(m_data, at) == key) return U32(m_data, at + 4);
        }
        return std::nullopt;
    }

    std::optional<uint32_t> Child(uint32_t dir, uint32_t key) const
    {
        const auto entry = Entry(dir, key);
        if (!entry || !(*entry & 0x80000000u)) return std::nullopt;
        return *entry & 0x7FFFFFFFu;
    }

    // The first entry (any language) of the directory at `dir` must be a data entry.
    std::optional<std::span<const uint8_t>> Leaf(uint32_t dir) const
    {
        if (dir + 24 > m_data.size() || U16(m_data, dir + 12) + U16(m_data, dir + 14) == 0) return std::nullopt;
        const uint32_t entry = U32(m_data, dir + 20);
        if (entry & 0x80000000u || entry + 16 > m_data.size()) return std::nullopt;
        const uint32_t rva = U32(m_data, entry);
        const uint32_t size = U32(m_data, entry + 4);
        if (rva < m_section.rva || rva - m_section.rva > m_data.size() || size > m_data.size() - (rva - m_section.rva)) return std::nullopt;
        return m_data.subspan(rva - m_section.rva, size);
    }

    const PeSection& m_section;
    std::span<const uint8_t> m_data;
};

std::optional<PeFile> ReadExe(const ByteSource& source, std::string& error)
{
    const auto path = FindFile(source, kExePath);
    if (!path) {
        error = "BeachLife.exe not found in " + source.Describe("");
        return std::nullopt;
    }
    auto exe = PeFile::Parse(source.ReadAll(*path).value_or(std::vector<uint8_t>()), error);
    if (!exe) error = source.Describe(*path) + ": " + error;
    return exe;
}

} // namespace

bool BuildIconFile(const PeFile& pe, uint16_t groupId, bool cursor, std::vector<uint8_t>& file, std::string& error)
{
    const PeSection* rsrc = nullptr;
    for (const auto& section : pe.Sections())
        if (section.name == ".rsrc") rsrc = &section;
    if (!rsrc) {
        error = "no .rsrc section";
        return false;
    }
    const Resources resources(pe, *rsrc);
    const auto group = resources.Find(cursor ? kResGroupCursor : kResGroupIcon, groupId);
    const char* kind = cursor ? "cursor" : "icon";
    if (!group || group->size() < 6 || U16(*group, 2) != (cursor ? 2 : 1)) {
        error = std::string("no group ") + kind + " resource " + std::to_string(groupId);
        return false;
    }
    const size_t count = U16(*group, 4);
    if (count == 0 || group->size() < 6 + count * 14) {
        error = std::string("group ") + kind + " " + std::to_string(groupId) + " is truncated";
        return false;
    }
    // ICONDIR + entries first, then each image; an RT_CURSOR image starts with the hotspot, which moves into the entry.
    std::vector<std::vector<uint8_t>> images;
    file.clear();
    Put16(file, 0);
    Put16(file, cursor ? 2 : 1);
    Put16(file, uint32_t(count));
    size_t offset = 6 + count * 16;
    for (size_t i = 0; i < count; ++i) {
        const size_t at = 6 + i * 14;
        const uint16_t id = U16(*group, at + 12);
        const auto image = resources.Find(cursor ? kResCursor : kResIcon, id);
        if (!image || (cursor && image->size() < 4)) {
            error = std::string("group ") + kind + " " + std::to_string(groupId) + " names the missing image " + std::to_string(id);
            return false;
        }
        std::vector<uint8_t> body(image->begin() + (cursor ? 4 : 0), image->end());
        if (cursor) {
            // Cursor group entry: width, height (doubled, mask included), planes, bits, bytes, id. File entry: the
            // height is the image's, planes and bits hold the hotspot.
            file.push_back(group->data()[at]);
            file.push_back(uint8_t(U16(*group, at + 2) / 2));
            file.push_back(0);
            file.push_back(0);
            Put16(file, U16(*image, 0));
            Put16(file, U16(*image, 2));
        } else {
            file.insert(file.end(), group->begin() + at, group->begin() + at + 8);
        }
        Put32(file, uint32_t(body.size()));
        Put32(file, uint32_t(offset));
        offset += body.size();
        images.push_back(std::move(body));
    }
    for (const auto& body : images) file.insert(file.end(), body.begin(), body.end());
    return true;
}

bool ConvertResources(const ByteSource& source, PackSink& out, int& files, std::string& error)
{
    files = 0;
    try {
        const auto pe = ReadExe(source, error);
        if (!pe) return false;
        std::vector<uint8_t> file;
        if (!BuildIconFile(*pe, kGameIconId, false, file, error) || !out.Write("icon.ico", file, error)) return false;
        ++files;
        for (const uint16_t id : kCursorIds) {
            if (!BuildIconFile(*pe, id, true, file, error) || !out.Write("cursor_" + std::to_string(id) + ".cur", file, error))
                return false;
            ++files;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
