#pragma once

#include "core/char16.h"
#include <cstdint>

namespace obl::text {
struct Table;
}

namespace obl::ui {

// Registers the `call-drive ui.text-table-find-key` fixture.
bool RegisterTextTableDrivers();

// C++ entries of the language table; the forms without a table use `Current()`.
namespace text_table {

// The answer of a lookup that finds no key: one empty string shared by every lookup (its address may be compared).
inline constexpr char16_t kEmptyText[1] = {};

text::Table* Create(const char* language);
void Destroy(text::Table* table);
const text::Table* Current();

uint32_t FindKey(const text::Table* table, const char* key);
uint32_t FindKey(const char* key);
Char16* GetString(const text::Table* table, int id);
Char16* GetString(int id);
const Char16* Lookup(const text::Table* table, const char* key);
const Char16* Lookup(const char* key);
const Char16* LookupW(const text::Table* table, const Char16* key);
const Char16* LookupW(const Char16* key);

} // namespace text_table

} // namespace obl::ui
