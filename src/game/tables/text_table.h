#pragma once

#include "core/file_io.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

// Language text table (`Table`; docs/specs/text-table.md). Pure logic, no game memory.
//
// The installed file (`data/text_<language>.ble`) is UTF-16LE XOR 0xFF; the engine reads the converted pack instead (text_load.h), and
// `Parse` here is the walk the converter reproduces. The walk does not tokenize lines:
// it walks the units and treats every pair of double quotes as one string, alternating key (narrowed to bytes) and
// value (kept wide). Quotes inside `//` comments therefore take part in the alternation.
namespace obl::text {

struct Entry {
    std::string key;
    std::u16string value;
};

// Hash of a key as the game computes it: h = h * 5 + (signed char) c, in 32-bit unsigned arithmetic.
uint32_t HashKey(const char* key);

struct Parsed {
    std::vector<Entry> entries; // completed key/value pairs, in file order (a trailing key without value is dropped)
    std::vector<int32_t> slots; // `modulus` hash slots, each the entry index stored there or -1 when empty
    int32_t hashed = 0;
};

// MakeTable after the file is read: `units` = the whole file as UTF-16 units (bytes already XOR-ed
// back), `unitCount` = file size / 2. `modulus` = the next prime from 4500, the slot count. An unterminated quote
// ends the walk (the walk would run past the buffer there). `maxEntries` stops it after that many completed pairs: the
// native constructor does not write pairs past the 4500 the object has room for.
Parsed Parse(const char16_t* units, int unitCount, uint32_t modulus, size_t maxEntries = std::numeric_limits<size_t>::max());

// Adds the `extra` entries whose key the table lacks (ignoring ASCII case), so a language file can override them. Our
// own texts (the OpenBeach credits) enter the table this way; nothing past `maxEntries`.
void Append(Parsed& parsed, std::span<const Entry> extra, uint32_t modulus, size_t maxEntries = std::numeric_limits<size_t>::max());

// The pairs of a table as the constructor placed them: in order, each into its hash slot (Add), duplicates kept, nothing past
// `maxEntries`. `modulus` = the next prime from 4500; a zero modulus gives an empty table.
Parsed FromEntries(std::span<const Entry> entries, uint32_t modulus, size_t maxEntries = std::numeric_limits<size_t>::max());

// The text table holds this many pairs; the rest are dropped.
inline constexpr size_t kMaxPairs = 4500;

// The language table (`Game::m_textTable`, docs/specs/text-table.md): the pairs in file order and the hash slots
// over them, owned by the game for its whole lifetime, so the values handed out stay valid.
struct Table {
    std::vector<Entry> entries;
    std::vector<int32_t> slots; // `modulus` slots, each an index into `entries` or -1
    uint32_t modulus = 0;
    int32_t hashed = 0; // occupied slots
    std::string path; // the pack file the pairs came from

    // Table::FindKey: index of the first key equal ignoring ASCII case, 0xffffffff when none.
    uint32_t FindKey(const char* key) const;
    // Table::Value without the -1 case: the value of entry `id` (no bounds check).
    const char16_t* Value(int id) const { return entries[static_cast<size_t>(id)].value.c_str(); }
    // Table::Lookup: the hash slot of `key` (case-insensitive compare), probed from HashKey(key) % modulus with
    // wrap-around until the start is reached again. A miss or a null key answers `fallback` (a static empty
    // text). A modulus of 0 divides by zero; here it is a miss.
    const char16_t* Lookup(const char* key, const char16_t* fallback) const;
};

// MakeTable without reading a file: `entries` (the language's pairs, text_load.h), then `extra` appended
// with Append, at most kMaxPairs. `path` is kept as the table's path (the pack file the pairs came from).
Table MakeTable(std::string path, uint32_t modulus, std::span<const Entry> entries, std::span<const Entry> extra = {});

} // namespace obl::text
