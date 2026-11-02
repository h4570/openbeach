#include "formats/cell_state.h"

#include <bit>
#include <cstddef>
#include <vector>

namespace obl::formats {
namespace {

using nlohmann::json;
using nlohmann::ordered_json;

constexpr CellField kFields[] = {{"terrain", 0xfu}, {"building", 0x10u}, {"border", 0x20u}, {"border_blocked", 0x40u},
        {"around_building", 0x80u}, {"beach", 0x100u}, {"margin", 0x200u}, {"unnamed", 0x400u}, {"built", 0x800u}, {"grass", 0x7000u},
        {"blocked", 0x8000u}, {"neighbours", 0xff0000u}, {"surface", 0xf000000u}, {"path", 0x10000000u}, {"free_deep", 0x20000000u},
        {"free_shallow", 0x40000000u}, {"free_land", 0x80000000u}};

uint32_t WordOfField(const CellField& field, uint32_t value)
{
    return (value << std::countr_zero(field.mask)) & field.mask;
}

} // namespace

std::span<const CellField> CellFields()
{
    return kFields;
}

uint32_t CellFieldValue(uint32_t word, const CellField& field)
{
    return (word & field.mask) >> std::countr_zero(field.mask);
}

ordered_json CellWordToRecord(uint32_t word)
{
    ordered_json record = ordered_json::object();
    for (const CellField& field : kFields)
        if (const uint32_t value = CellFieldValue(word, field)) record[field.name] = value;
    return record;
}

uint32_t CellWordFromRecord(const json& record)
{
    uint32_t word = 0;
    for (const CellField& field : kFields) word |= WordOfField(field, record.value(field.name, uint32_t{0}));
    return word;
}

ordered_json CellWordsToRuns(const ordered_json& words)
{
    ordered_json runs = ordered_json::object();
    for (const CellField& field : kFields) {
        ordered_json list = ordered_json::array();
        size_t count = 0;
        uint32_t value = 0;
        for (const ordered_json& word : words) {
            const uint32_t next = CellFieldValue(word.get<uint32_t>(), field);
            if (count && next != value) {
                list.push_back(ordered_json::array({count, value}));
                count = 0;
            }
            value = next;
            ++count;
        }
        if (count) list.push_back(ordered_json::array({count, value}));
        runs[field.name] = std::move(list);
    }
    return runs;
}

json CellRunsToWords(const json& runs)
{
    std::vector<uint32_t> words;
    for (const CellField& field : kFields) {
        const auto list = runs.find(field.name);
        if (list == runs.end()) continue;
        size_t cell = 0;
        for (const json& run : *list) {
            const size_t end = cell + run.at(0).get<size_t>();
            if (end > words.size()) words.resize(end);
            const uint32_t bits = WordOfField(field, run.at(1).get<uint32_t>());
            for (; cell < end; ++cell) words[cell] |= bits;
        }
    }
    return words;
}

} // namespace obl::formats
