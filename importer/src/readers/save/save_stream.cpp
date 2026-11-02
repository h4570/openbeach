#include "importer/src/readers/save/save_stream.h"

#include <array>
#include <bit>
#include <cstdio>
#include <fstream>

namespace obl::convert {
namespace {

size_t Width(SaveTag tag)
{
    switch (tag) {
    case SaveTag::Class:
    case SaveTag::String:
    case SaveTag::Bool:
    case SaveTag::Byte:
    case SaveTag::ByteArray: return 1;
    case SaveTag::Short:
    case SaveTag::WideString:
    case SaveTag::Word: return 2;
    default: return 4;
    }
}

bool Counted(SaveTag tag)
{
    return tag == SaveTag::Class || tag == SaveTag::String || tag == SaveTag::WideString || tag == SaveTag::IntArray ||
            tag == SaveTag::ByteArray || tag == SaveTag::FloatArray;
}

uint32_t LittleEndian(std::span<const uint8_t> bytes)
{
    uint32_t value = 0;
    for (size_t i = 0; i < bytes.size(); ++i) value |= uint32_t(bytes[i]) << (i * 8);
    return value;
}

bool Fail(size_t offset, std::string_view why, std::string& error)
{
    error = "offset " + std::to_string(offset) + ": " + std::string(why);
    return false;
}

bool Expect(SaveStream& stream, SaveTag tag, std::string_view field, SaveRecord& record, std::string& error)
{
    if (!stream.Next(record, error)) {
        if (error.empty()) Fail(stream.Offset(), "missing " + std::string(field), error);
        return false;
    }
    if (record.tag != tag) return Fail(record.offset, std::string(field) + ": expected " + SaveTagName(tag), error);
    return true;
}

bool Class(SaveStream& stream, std::string_view name, SaveRecord& record, std::string& error)
{
    return Expect(stream, SaveTag::Class, name, record, error) &&
            (record.ClassName() == name || Fail(record.offset, "expected class " + std::string(name), error));
}

} // namespace

uint32_t SaveRecord::Bits(size_t index) const
{
    const size_t width = Width(tag);
    if (index >= count || index >= bytes.size() / width) return 0;
    return LittleEndian(bytes.subspan(index * width, width));
}

std::string_view SaveRecord::Text() const
{
    if (bytes.empty()) return {};
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string_view SaveRecord::ClassName() const
{
    auto text = Text();
    constexpr std::string_view kPrefix = "Class name : ";
    if (text.starts_with(kPrefix)) text.remove_prefix(kPrefix.size());
    return text;
}

bool SaveStream::Next(SaveRecord& record, std::string& error)
{
    error.clear();
    record = {};
    if (m_offset == m_bytes.size()) return false;
    size_t pos = m_offset;
    const uint8_t rawTag = m_bytes[pos++];
    if (rawTag > static_cast<uint8_t>(SaveTag::Word)) return Fail(m_offset, "unrecognized record tag " + std::to_string(rawTag), error);
    const auto tag = static_cast<SaveTag>(rawTag);
    uint32_t count = 1;
    if (Counted(tag)) {
        if (m_bytes.size() - pos < 4) return Fail(m_offset, "truncated record count", error);
        count = LittleEndian(m_bytes.subspan(pos, 4));
        pos += 4;
        if (count > INT32_MAX) return Fail(m_offset, "negative record count", error);
    }
    const size_t width = Width(tag);
    // Divide before multiplying: hostile counts must be safe on 32-bit hosts too.
    if (count > (m_bytes.size() - pos) / width) return Fail(m_offset, "truncated record payload", error);
    const size_t size = size_t(count) * width;
    // Bool bytes other than 0/1 occur in real saves (uninitialized members); the game copies them raw, so keep them.
    record = {m_offset, tag, count, m_bytes.subspan(pos, size)};
    m_offset = pos + size;
    return true;
}

bool ReadOldSaveHeader(SaveStream& stream, OldSaveHeader& header, std::string& error)
{
    header = {};
    SaveRecord record;
    OldSaveHeader parsed;
    if (!Class(stream, "Binary Serial Stream", record, error) || !Class(stream, "BeachLife Save", record, error) ||
            !Expect(stream, SaveTag::Float, "version", record, error))
        return false;
    if (record.Bits() != 0x3fcccccd) return Fail(record.offset, "unsupported save version (expected 1.6)", error);
    if (!Expect(stream, SaveTag::Int, "saved_at", record, error)) return false;
    parsed.savedAt = record.Bits();
    if (!Expect(stream, SaveTag::Int, "scenario", record, error)) return false;
    parsed.scenario = std::bit_cast<int32_t>(record.Bits());
    if (parsed.scenario < 0) return Fail(record.offset, "negative scenario", error);
    if (!Expect(stream, SaveTag::Int, "day", record, error)) return false;
    parsed.day = std::bit_cast<int32_t>(record.Bits());
    if (!Expect(stream, SaveTag::Float, "day_time", record, error)) return false;
    parsed.dayTimeBits = record.Bits();
    if (!Expect(stream, SaveTag::Float, "cash", record, error)) return false;
    parsed.cashBits = record.Bits();
    if (!Expect(stream, SaveTag::Int, "guests", record, error)) return false;
    parsed.guests = std::bit_cast<int32_t>(record.Bits());
    if (parsed.guests < 0) return Fail(record.offset, "negative guest count", error);
    if (!Expect(stream, SaveTag::String, "title", parsed.title, error) || !Expect(stream, SaveTag::Bool, "sandbox", record, error))
        return false;
    parsed.sandbox = record.Bits() != 0;
    if (!Class(stream, "World", record, error)) return false;
    parsed.worldOffset = record.offset;
    header = parsed;
    return true;
}

const char* SaveTagName(SaveTag tag)
{
    constexpr const char* kNames[] = {
            "class", "string", "bool", "float", "int", "short", "uint", "int[]", "wstring", "byte", "byte[]", "float[]", "dword", "word"};
    const auto index = static_cast<uint8_t>(tag);
    return index < sizeof(kNames) / sizeof(kNames[0]) ? kNames[index] : "unrecognized";
}

bool ReadBoundedFile(const std::string& path, size_t limit, std::vector<uint8_t>& bytes, std::string& error)
{
    bytes.clear();
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > limit) {
        error = path + ": larger than " + std::to_string(limit) + " bytes";
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    in.seekg(0);
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        bytes.clear();
        error = "cannot read " + path;
        return false;
    }
    return true;
}

bool ReadBoundedFile(const ByteSource& source, std::string_view path, size_t limit, std::vector<uint8_t>& bytes, std::string& error)
{
    bytes.clear();
    const auto size = source.Size(path);
    if (!size) {
        error = "cannot read " + source.Describe(path);
        return false;
    }
    if (*size > limit) {
        error = source.Describe(path) + ": larger than " + std::to_string(limit) + " bytes";
        return false;
    }
    bytes.resize(static_cast<size_t>(*size));
    if (!bytes.empty() && !source.Read(path, 0, bytes)) {
        bytes.clear();
        error = "cannot read " + source.Describe(path);
        return false;
    }
    return true;
}

bool DescribeOldSave(std::span<const uint8_t> bytes, std::string& report, std::string& error)
{
    report.clear();
    SaveStream stream(bytes);
    OldSaveHeader header;
    if (!ReadOldSaveHeader(stream, header, error)) return false;
    std::array<size_t, 14> counts{};
    size_t records = 0;
    SaveRecord record;
    while (stream.Next(record, error)) {
        ++counts[static_cast<uint8_t>(record.tag)];
        ++records;
    }
    if (!error.empty()) return false;

    std::string title;
    for (const char c : header.title.Text()) {
        char escaped[8];
        const auto byte = static_cast<uint8_t>(c);
        // Undecoded bytes: the language comes from the pack, so only ASCII prints as is.
        if (byte >= 0x20 && byte < 0x7f && c != '\\') {
            title += c;
        } else {
            std::snprintf(escaped, sizeof(escaped), "\\x%02x", byte);
            title += escaped;
        }
    }
    char line[160];
    const auto add = [&](const char* format, auto... args) {
        std::snprintf(line, sizeof(line), format, args...);
        report += line;
    };
    report += "version   1.6\n";
    add("saved_at  %u\n", header.savedAt);
    add("scenario  %d\n", header.scenario);
    add("day       %d\n", header.day);
    add("day_time  %.9g (0x%08x)\n", double(std::bit_cast<float>(header.dayTimeBits)), header.dayTimeBits);
    add("cash      %.9g (0x%08x)\n", double(std::bit_cast<float>(header.cashBits)), header.cashBits);
    add("guests    %d\n", header.guests);
    report += "title     \"" + title + "\"\n";
    add("sandbox   %s\n", header.sandbox ? "yes" : "no");
    // The World marker belongs to the body, so it counts with the records after it.
    add("world at  %zu, %zu body records, %zu bytes\n", header.worldOffset, records + 1, bytes.size());
    ++counts[static_cast<uint8_t>(SaveTag::Class)];
    for (uint8_t tag = 0; tag < counts.size(); ++tag)
        if (counts[tag]) add("  %-8s %zu\n", SaveTagName(static_cast<SaveTag>(tag)), counts[tag]);
    return true;
}

} // namespace obl::convert
