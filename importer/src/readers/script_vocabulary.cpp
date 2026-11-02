#include "importer/src/readers/script_vocabulary.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace obl::convert {
namespace {

constexpr size_t kNameLimit = 96; // longer than any name of the tables
constexpr size_t kTableRowSize = 8; // {int id; char* name;}
// Plausibility bounds of the state and message tables: a parser number fits 16 bits, and each table names at least this
// many rows (both supported builds name over 100; the next smaller run of such records names 24).
constexpr uint32_t kMaxTableId = 0xFFFF;
constexpr size_t kMinTableNames = 64;
constexpr size_t kMessageCount = 108; // the world script loader reads exactly these (world_script_load_ids.cpp)
// The engine numbers the states as the October 2002 build does (shark.h's kSharkAttack, for one). The August 2002 CD
// build names one state less: it lacks the October build's state 64 (no script names it), so its ids from 64 on are one
// lower and are moved up here; slot 64 stays empty.
constexpr size_t kStateCount = 135;
constexpr int32_t kAddedStateId = 64;

struct TableRow {
    uint32_t id = 0;
    std::optional<std::string> name; // nullopt for an empty slot
};

// A name of the tables: [A-Za-z0-9_], up to the extractor's limit.
std::optional<std::string> TableName(const PeFile& exe, uint32_t va)
{
    const auto text = exe.CString(va, kNameLimit);
    if (!text || text->empty()) return std::nullopt;
    for (const char c : *text)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return std::nullopt;
    return std::string(*text);
}

// The record {id, name} at `va`: a small id with a name or an empty slot (null name).
std::optional<TableRow> RowAt(const PeFile& exe, uint32_t va)
{
    const auto id = exe.ReadU32(va);
    const auto name = exe.ReadU32(va + 4);
    if (!id || !name || *id > kMaxTableId) return std::nullopt;
    TableRow row{*id, std::nullopt};
    if (*name == 0) return row;
    row.name = TableName(exe, *name);
    if (!row.name) return std::nullopt;
    return row;
}

size_t Named(std::span<const TableRow> rows)
{
    return static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [](const TableRow& row) { return row.name.has_value(); }));
}

std::vector<ScriptName> Names(std::span<const TableRow> rows)
{
    std::vector<ScriptName> out;
    for (const TableRow& row : rows)
        if (row.name) out.push_back({static_cast<int32_t>(row.id), *row.name}); // the extractor skips the empty slots
    return out;
}

// The state and message tables by their structure, without addresses: both are arrays of {int32 id; const char* name}
// (an empty slot has a null name) that lie back to back in a data section. The states start at id 0; the message table
// starts again at 0 and numbers its rows by index to the end. The run of records that splits that way, with both parts
// naming kMinTableNames rows or more, must be unique.
bool FindStateAndMessageTables(const PeFile& exe, std::vector<ScriptName>& states, std::vector<ScriptName>& messages, std::string& error)
{
    std::optional<std::pair<std::vector<ScriptName>, std::vector<ScriptName>>> found;
    for (const PeSection& section : exe.Sections()) {
        const uint32_t begin = exe.ImageBase() + section.rva;
        const uint32_t end = begin + section.rawSize;
        for (uint32_t va = begin; va + kTableRowSize <= end; va += 4) {
            std::vector<TableRow> run;
            for (uint32_t at = va; at + kTableRowSize <= end; at += kTableRowSize) {
                auto row = RowAt(exe, at);
                if (!row) break;
                run.push_back(std::move(*row));
            }
            if (Named(run) < 2 * kMinTableNames) continue;
            va += static_cast<uint32_t>(kTableRowSize * run.size()) - 4; // the next run starts after this one
            while (!run.empty() && !run.back().name) run.pop_back();
            size_t first = 0;
            while (first < run.size() && !run[first].name) ++first;
            const std::span<const TableRow> rows(run.data() + first, run.size() - first);
            if (rows.empty() || rows.front().id != 0) continue;
            // The message table: the first later row from which every id equals its index.
            std::optional<size_t> split;
            for (size_t start = 1; start < rows.size() && !split; ++start) {
                bool indexed = true;
                for (size_t i = start; i < rows.size() && indexed; ++i) indexed = rows[i].id == i - start;
                if (indexed) split = start;
            }
            if (!split || Named(rows.first(*split)) < kMinTableNames || Named(rows.subspan(*split)) < kMinTableNames) continue;
            if (found) {
                error = "the state and message tables are not unique";
                return false;
            }
            found.emplace(Names(rows.first(*split)), Names(rows.subspan(*split)));
        }
    }
    if (!found) {
        error = "no state and message tables ({id, name} records, the messages numbered by index)";
        return false;
    }
    states = std::move(found->first);
    messages = std::move(found->second);
    return true;
}

} // namespace

bool ExtractScriptVocabulary(const PeFile& exe, ScriptTables& out, std::string& error)
{
    out = {};
    if (!FindStateAndMessageTables(exe, out.states, out.messages, error)) {
        out = {};
        return false;
    }
    if (out.messages.size() != kMessageCount) {
        error = "the scripts need " + std::to_string(kMessageCount) + " messages, found " + std::to_string(out.messages.size());
        out = {};
        return false;
    }
    if (out.states.size() == kStateCount - 1) {
        for (ScriptName& state : out.states)
            if (state.id >= kAddedStateId) ++state.id;
    } else if (out.states.size() != kStateCount) {
        error = "the engine numbers " + std::to_string(kStateCount) + " states, found " + std::to_string(out.states.size());
        out = {};
        return false;
    }
    return true;
}

} // namespace obl::convert
