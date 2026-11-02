#pragma once

#include "openbeach/importer/byte_source.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

enum class SaveTag : uint8_t {
    Class,
    String,
    Bool,
    Float,
    Int,
    Short,
    UInt,
    IntArray,
    WideString,
    Byte,
    ByteArray,
    FloatArray,
    Dword,
    Word
};

struct SaveRecord {
    size_t offset = 0;
    SaveTag tag = SaveTag::Class;
    uint32_t count = 0;
    std::span<const uint8_t> bytes;

    // Integer bits, including f32 payloads, read without alignment or host-endian assumptions.
    uint32_t Bits(size_t index = 0) const;
    std::string_view Text() const;
    std::string_view ClassName() const;
};

class SaveStream {
public:
    explicit SaveStream(std::span<const uint8_t> bytes) : m_bytes(bytes) {}
    // false with empty error is clean EOF; false with error is malformed input. Failure leaves the cursor unchanged.
    bool Next(SaveRecord& record, std::string& error);
    size_t Offset() const { return m_offset; }

private:
    std::span<const uint8_t> m_bytes;
    size_t m_offset = 0;
};

struct OldSaveHeader {
    uint32_t savedAt = 0; // timestamp is written with the signed-int tag, but interpreted as unsigned
    int32_t scenario = 0; // zero based; conversion through the pack is a later step
    int32_t day = 0;
    uint32_t dayTimeBits = 0;
    uint32_t cashBits = 0;
    int32_t guests = 0;
    SaveRecord title; // undecoded narrow bytes; language is supplied by the pack, not guessed
    bool sandbox = false;
    size_t worldOffset = 0;
};

// Reads the stream/save signatures, exact 1.6 version, nine slot fields and World marker.
// Does not interpret object/manager state or claim that structurally valid records form a loadable world.
bool ReadOldSaveHeader(SaveStream& stream, OldSaveHeader& header, std::string& error);
const char* SaveTagName(SaveTag tag);

// Old slots are well under 1 MiB; the cap refuses unrelated or hostile files before reading them whole.
constexpr size_t kMaxOldSaveBytes = 64u << 20;
bool ReadBoundedFile(const std::string& path, size_t limit, std::vector<uint8_t>& bytes, std::string& error);
// The same for the file `path` of `source`; errors name it with source.Describe.
bool ReadBoundedFile(const ByteSource& source, std::string_view path, size_t limit, std::vector<uint8_t>& bytes, std::string& error);

// Text report of `obl-convert save-info`: header fields (floats as value and bits) and record counts per tag.
// Fails on a bad header or a malformed record anywhere in the body.
bool DescribeOldSave(std::span<const uint8_t> bytes, std::string& report, std::string& error);

} // namespace obl::convert
