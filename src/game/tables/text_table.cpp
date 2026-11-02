#include "game/tables/text_table.h"

#include "core/ascii_case.h"
#include "core/code_page.h"

#include <algorithm>
#include <string>

namespace obl::text {

uint32_t HashKey(const char* key)
{
    uint32_t hash = 0;
    for (; *key; ++key) hash = hash * 5 + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(*key)));
    return hash;
}

std::string Narrow(const char16_t* units, size_t count)
{
    size_t length = 0;
    bool ascii = true;
    while (length < count && units[length] != 0) {
        ascii = ascii && units[length] < 0x80;
        ++length;
    }
    if (ascii || length == 0) return std::string(units, units + length);
    // The conversion used the system ANSI code page; now it is the game's fixed one (code_page.h); keys in the shipped files
    // are ASCII, so this only matters for mods.
    return code_page::Narrow(std::u16string_view(units, length));
}

namespace {

// Linear probing from the key's home slot, like the hash insert.
void Add(Parsed& parsed, Entry entry, uint32_t modulus)
{
    uint32_t home = HashKey(entry.key.c_str()) % modulus;
    uint32_t slot = home;
    bool placed = true;
    while (parsed.slots[slot] != -1) {
        if (++slot >= modulus) slot = 0;
        if (slot == home) {
            placed = false; // full table: the entry exists but is not findable through the hash
            break;
        }
    }
    if (placed) {
        parsed.slots[slot] = static_cast<int32_t>(parsed.entries.size());
        ++parsed.hashed;
    }
    parsed.entries.push_back(std::move(entry));
}

} // namespace

Parsed Parse(const char16_t* units, int unitCount, uint32_t modulus, size_t maxEntries)
{
    Parsed parsed;
    parsed.slots.assign(modulus, -1);
    auto at = [&](int i) -> char16_t { return i >= 0 && i < unitCount ? units[i] : u'\0'; };

    // One counter always equals the position: a string ends on the closing quote, and the next
    // iteration looks at the unit after it. A quote at unit 0 is never seen (the file starts with a BOM).
    const int limit = unitCount - 1;
    bool haveKey = false;
    std::string key;
    for (int pos = 0; pos < limit;) {
        if (at(pos + 1) != u'"') {
            ++pos;
            continue;
        }
        int start = pos + 2;
        int end = start;
        while (end < unitCount && units[end] != u'"') ++end;
        if (!haveKey) {
            key = Narrow(units + start, static_cast<size_t>(end - start));
            haveKey = true;
        } else {
            Entry entry;
            entry.key = std::move(key);
            size_t length = 0;
            while (start + static_cast<int>(length) < end && units[start + length] != 0) ++length;
            entry.value.assign(units + start, length);
            haveKey = false;

            Add(parsed, std::move(entry), modulus);
            if (parsed.entries.size() >= maxEntries) break;
        }
        pos = end;
    }
    return parsed;
}

void Append(Parsed& parsed, std::span<const Entry> extra, uint32_t modulus, size_t maxEntries)
{
    if (modulus == 0) return;
    for (const Entry& entry : extra) {
        if (parsed.entries.size() >= maxEntries) return;
        const bool present = std::any_of(parsed.entries.begin(), parsed.entries.end(),
                [&](const Entry& e) { return ascii::EqualsNoCase(e.key.c_str(), entry.key.c_str()); });
        if (!present) Add(parsed, entry, modulus);
    }
}

Parsed FromEntries(std::span<const Entry> entries, uint32_t modulus, size_t maxEntries)
{
    Parsed parsed;
    if (modulus == 0) return parsed;
    parsed.slots.assign(modulus, -1);
    for (const Entry& entry : entries) {
        if (parsed.entries.size() >= maxEntries) break;
        Add(parsed, entry, modulus);
    }
    return parsed;
}

Table MakeTable(std::string path, uint32_t modulus, std::span<const Entry> entries, std::span<const Entry> extra)
{
    Parsed parsed = FromEntries(entries, modulus, kMaxPairs);
    Append(parsed, extra, modulus, kMaxPairs);
    Table table;
    table.entries = std::move(parsed.entries);
    table.slots = std::move(parsed.slots);
    table.modulus = modulus;
    table.hashed = parsed.hashed;
    table.path = std::move(path);
    return table;
}

uint32_t Table::FindKey(const char* key) const
{
    for (size_t i = 0; i < entries.size(); ++i) {
        if (ascii::EqualsNoCase(entries[i].key.c_str(), key)) return static_cast<uint32_t>(i);
    }
    return 0xffffffffu;
}

const char16_t* Table::Lookup(const char* key, const char16_t* fallback) const
{
    if (!key || modulus == 0) return fallback;
    const uint32_t home = HashKey(key) % modulus;
    uint32_t i = home;
    do {
        const int32_t index = slots[i];
        if (index >= 0 && ascii::EqualsNoCase(entries[static_cast<size_t>(index)].key.c_str(), key))
            return entries[static_cast<size_t>(index)].value.c_str();
        if (++i >= modulus) i = 0;
    } while (i != home);
    return fallback;
}

} // namespace obl::text
