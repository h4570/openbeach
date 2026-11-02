#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace obl {

// Section and name point into the snapshot's interned strings, the value bytes into its arena (offset/size), so adding a field
// allocates nothing once the arena and the field vector have their capacity (a Save pass emits ~250k fields).
struct WorldField {
    std::string_view section;
    std::string_view name;
    uint8_t type;
    uint32_t offset;
    uint32_t size;
};

// Type tags of schemas 2 and 3. They name the SerialStream write slot the save used for a record and are part of the pinned
// format, so a model emitter (world_snapshot_model.h) picks the tag the record had, not the one its native type suggests.
enum class RecordType : uint8_t {
    Class = 0,
    String = 1,
    Bool = 2,
    Float = 3,
    Int = 4,
    Short = 5,
    UInt = 6,
    Ints = 7,
    WString = 8,
    Byte = 9,
    Bytes = 10,
    Floats = 11,
    Dword = 12,
    Word = 13,
    Counter = 14, // schema-2 instrumentation (rand-calls), not a SerialStream tag
};

// A versioned, ordered projection of the simulation state, without heap addresses or padding. Records are typed little-endian
// bytes; the typed writers below store the value's bytes as the x86 game wrote them (all supported hosts are little-endian).
class WorldSnapshot {
public:
    void Class(const char* name);
    void Add(const char* name, uint8_t type, const void* bytes, size_t size);

    void UInt(const char* name, uint32_t value) { Add(name, uint8_t(RecordType::UInt), &value, sizeof(value)); }
    void Int(const char* name, int32_t value) { Add(name, uint8_t(RecordType::Int), &value, sizeof(value)); }
    // By reference, so the bits come from memory: a float passed by value may travel through a wider register, which quiets a signalling NaN.
    void Float(const char* name, const float& value) { Add(name, uint8_t(RecordType::Float), &value, sizeof(value)); }
    void Bool(const char* name, uint8_t value) { Add(name, uint8_t(RecordType::Bool), &value, sizeof(value)); }
    void Byte(const char* name, uint8_t value) { Add(name, uint8_t(RecordType::Byte), &value, sizeof(value)); }
    void Short(const char* name, uint16_t value) { Add(name, uint8_t(RecordType::Short), &value, sizeof(value)); }
    void Word(const char* name, uint16_t value) { Add(name, uint8_t(RecordType::Word), &value, sizeof(value)); }
    void Dword(const char* name, uint32_t value) { Add(name, uint8_t(RecordType::Dword), &value, sizeof(value)); }
    void String(const char* name, const char* text);
    // UTF-16 code units up to the terminator, two bytes each on every host: the game's texts are UTF-16 in wchar_t, char16_t or
    // layout::Char16, and wchar_t is 4 bytes in the Linux unit tests.
    template<class Unit> void WString(const char* name, const Unit* text)
    {
        static_assert(sizeof(Unit) >= 2, "WString records hold UTF-16 units");
        size_t length = 0;
        while (text[length]) ++length;
        if constexpr (sizeof(Unit) == 2) {
            Add(name, uint8_t(RecordType::WString), text, length * 2);
        } else {
            std::u16string units(text, text + length);
            Add(name, uint8_t(RecordType::WString), units.data(), length * 2);
        }
    }
    void Ints(const char* name, const void* values, size_t count) { Add(name, uint8_t(RecordType::Ints), values, count * 4); }
    void Floats(const char* name, const float* values, size_t count) { Add(name, uint8_t(RecordType::Floats), values, count * 4); }
    void Bytes(const char* name, const void* values, size_t count) { Add(name, uint8_t(RecordType::Bytes), values, count); }

    void Normalize();
    // Capacity hint for the number of fields a Save pass emits (the vector otherwise regrows ~20 times).
    void Reserve(size_t fields)
    {
        m_fields.reserve(fields);
        m_arena.reserve(fields * 8);
    }
    std::string Text() const;
    std::string Hash() const { return HashOf(Text()); }
    // Hash of an already rendered Text(), so a caller that also writes the text does not build it twice.
    static std::string HashOf(std::string_view text);
    // Calls `mix` with the hashed part of every line of a rendered Text(), newline included: the header whole, of a record only
    // its type and bytes (the names are left out, schema 3). The section digests (world_sections.h) hash the same parts.
    template<class Mix> static void ForEachHashedLine(std::string_view text, Mix&& mix)
    {
        bool header = true;
        for (size_t pos = 0; pos < text.size();) {
            size_t end = text.find('\n', pos);
            end = end == std::string_view::npos ? text.size() : end + 1;
            std::string_view line = text.substr(pos, end - pos);
            pos = end;
            if (!header) {
                const size_t tab1 = line.find('\t');
                const size_t tab2 = tab1 == std::string_view::npos ? tab1 : line.find('\t', tab1 + 1);
                if (tab2 != std::string_view::npos) line.remove_prefix(tab2 + 1);
            }
            header = false;
            mix(line);
        }
    }
    // Empty when two rendered texts agree, otherwise `line N <nameA>=[record] <nameB>=[record]` for the first differing line
    // (1-based, the header is line 1; `<end>` for a text that ended; records cut to 160 characters).
    static std::string FirstDifference(std::string_view a, std::string_view b, std::string_view nameA, std::string_view nameB);
    size_t Count() const { return m_fields.size(); }

private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    struct StringEqual {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const { return a == b; }
    };
    std::string_view Intern(std::string_view s);
    uint8_t* Bytes(const WorldField& f) { return m_arena.data() + f.offset; }
    const uint8_t* Bytes(const WorldField& f) const { return m_arena.data() + f.offset; }

    std::string_view m_section;
    std::unordered_set<std::string, StringHash, StringEqual> m_strings;
    std::vector<uint8_t> m_arena;
    std::vector<WorldField> m_fields;
};

} // namespace obl
