#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <span>

// The named fields of a map cell word, shared by the save's `map.cell_state` and the footprint layers of a building
// (docs/architecture/data-formats/saves.md "Map cells").
namespace obl::formats {

struct CellField {
    const char* name;
    uint32_t mask; // adjacent bits; together the fields cover all 32
};

std::span<const CellField> CellFields();

uint32_t CellFieldValue(uint32_t word, const CellField& field);

// A word as a record of its fields that are not 0, and back; a missing field reads as 0.
nlohmann::ordered_json CellWordToRecord(uint32_t word);
uint32_t CellWordFromRecord(const nlohmann::json& record);

// One run-length list of [count, value] per field, and back.
nlohmann::ordered_json CellWordsToRuns(const nlohmann::ordered_json& words);
nlohmann::json CellRunsToWords(const nlohmann::json& runs);

} // namespace obl::formats
