#include "core/char16.h"
#include "core/code_page.h"
#include "game/ui/resolutions.h"
#include "core/hash_prime.h"
#include "core/pack_path.h"
#include "game/ui/text_table.h"

#include "game/game/game_accessors.h"
#include "core/ascii_case.h"
#include "core/log.h"
#include "game/game/own_credits.h"
#include "core/call_counter.h"
#include "game/tables/text_load.h"
#include "game/tables/text_table.h"
#include "game/game/diagnostics.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace obl::ui {
namespace {

using text::Table;

std::vector<text::Entry> g_expectedCredits;

// Call counters (`call-report ui.text-table-find-key`).
test::CallCounter g_constructCalls("ui.text-table-construct");
test::CallCounter g_findKeyCalls("ui.text-table-find-key");
test::CallCounter g_getStringCalls("ui.text-table-get-string");

char16_t* EmptyText()
{
    return const_cast<char16_t*>(text_table::kEmptyText);
}

Table* g_current = nullptr;

Table*& GlobalTable()
{
    return g_current;
}

uint32_t SlotModulus()
{
    return hash_prime::NextPrime(static_cast<uint32_t>(text::kMaxPairs));
}

[[noreturn]] void Fatal(const std::string& message)
{
    game::FatalError("%s", message.c_str());
    std::abort(); // FatalError exits the process
}

// ---- call-drive ----------------------------------------------------------------------------------------------
// The simulation looks keys up only for the few thoughts and desires of the guests that happen to exist, so
// `call-drive` asks for every key of the loaded table (also with other letter case) and for misses.

void Drive(int rounds)
{
    const Table* table = GlobalTable();
    if (!table) {
        OBL_LOG("autotest: FAILED call-drive ui.text-table-find-key: no text table loaded");
        return;
    }
    uint32_t seed = 12345;
    for (int round = 0; round < rounds; ++round) {
        for (const text::Entry& entry : table->entries) {
            std::string key = entry.key;
            seed = seed * 1664525u + 1013904223u;
            for (char& c : key) {
                if ((seed >>= 1) & 1) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : (c >= 'a' && c <= 'z' ? c - 32 : c));
            }
            uint32_t found = text_table::FindKey(table, key.c_str());
            text_table::GetString(table, static_cast<int>(found));
        }
        text_table::FindKey(table, "NO_SUCH_TEXT_KEY");
        text_table::FindKey(table, "");
        text_table::GetString(table, -1);
    }
    // Verify the language selected at construction, including the renderer's byte encoding.
    for (const text::Entry& own : g_expectedCredits) {
        const uint32_t id = text_table::FindKey(table, own.key.c_str());
        if (id == 0xffffffffu)
            OBL_LOG("autotest: FAILED call-drive ui.text-table-find-key: own text %s missing", own.key.c_str());
        else if (std::u16string_view(text_table::GetString(table, static_cast<int>(id))) != own.value)
            OBL_LOG("autotest: FAILED call-drive ui.text-table-find-key: own text %s wrong language or encoding", own.key.c_str());
    }
}

} // namespace

namespace text_table {
namespace {
test::CallCounter g_lookupCalls("ui.text-lookup"), g_lookupWideCalls("ui.text-lookup-wide");
}

Table* Create(const char* language)
{
    g_constructCalls.Count();
    std::vector<text::Entry> entries;
    std::string error;
    if (!text_load::LoadPack(pack_path::Selected(), language, entries, error)) Fatal("Texts: " + error + ". Import the game data again");
    const std::vector<text::Entry> ownTexts = own_credits::Texts(language);
    g_expectedCredits = ownTexts;
    for (text::Entry& own : g_expectedCredits)
        for (const text::Entry& entry : entries)
            if (ascii::EqualsNoCase(entry.key.c_str(), own.key.c_str())) own.value = entry.value;
    auto* table = new Table(text::MakeTable(text_load::PackPath(language), SlotModulus(), entries, ownTexts));
    GlobalTable() = table;
    return table;
}

void Destroy(Table* table)
{
    if (GlobalTable() == table) GlobalTable() = nullptr;
    delete table;
}

uint32_t FindKey(const Table* table, const char* key)
{
    g_findKeyCalls.Count();
    return table->FindKey(key);
}

uint32_t FindKey(const char* key)
{
    return FindKey(GlobalTable(), key);
}

Char16* GetString(const Table* table, int id)
{
    g_getStringCalls.Count();
    return id == -1 ? EmptyText() : const_cast<char16_t*>(table->Value(id));
}

Char16* GetString(int id)
{
    return GetString(GlobalTable(), id);
}

const Table* Current()
{
    return GlobalTable();
}

const Char16* Lookup(const text::Table* table, const char* key)
{
    if (const Char16* resolution = ui::ResolutionText(key)) return resolution;
    g_lookupCalls.Count();
    return table->Lookup(key, kEmptyText);
}
const Char16* Lookup(const char* key)
{
    return Lookup(Current(), key);
}
const Char16* LookupW(const text::Table* table, const Char16* key)
{
    g_lookupWideCalls.Count();
    char narrow[256];
    code_page::Narrow(key, narrow, sizeof(narrow));
    return table->Lookup(narrow, kEmptyText);
}
const Char16* LookupW(const Char16* key)
{
    return LookupW(Current(), key);
}

} // namespace text_table

bool RegisterTextTableDrivers()
{
    g_findKeyCalls.SetDriver(&Drive);
    return true;
}

} // namespace obl::ui
