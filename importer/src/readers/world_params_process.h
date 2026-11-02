#pragma once

#include "importer/src/readers/world_params_add.h"
#include "game/tables/world_params_reset.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace obl::world_params_process {

using tables::WorldParams;
using world_params_add::Env;
namespace queries = world_params_queries;

namespace detail {

using namespace world_params_add::detail;

inline constexpr size_t kKeywordSize = 256;

inline bool ScanKeyword(const char* data, uint32_t size, uint32_t open, char* keyword, uint32_t* terminator)
{
    char* write = keyword;
    uint32_t position = open;
    for (;;) {
        const uint32_t next = position + 1;
        if (next >= size) return false;
        const char c = data[next];
        if (c == '_' && !Is(keyword, "JETTY") && !Is(keyword, "SECURITY")) {
            *terminator = next;
            return true;
        }
        if (c == '"') {
            *terminator = next;
            return true;
        }
        if (write < keyword + kKeywordSize - 1) *write++ = c;
        position = next;
    }
}

inline bool IsGuestRangeKeyword(const char* keyword)
{
    if (Is(keyword, "SLEEPTIME") || Is(keyword, "WAKETIME")) return true;
    for (const GuestPairField& field : kGuestPairs)
        if (Is(keyword, field.keyword)) return true;
    return false;
}

// The keywords AddGuestData handles, plus the ones Process lets through (GENDER, DRUNK_TYPE, NAMESET).
inline bool IsGuestDataKeyword(const char* keyword)
{
    if (Is(keyword, "GENDER") || Is(keyword, "DRUNK_TYPE") || Is(keyword, "NAMESET")) return true;
    for (const char* name : kGuestDataKeywords)
        if (Is(keyword, name)) return true;
    for (const char* name : kGuestMaxKeywords)
        if (Is(keyword, name)) return true;
    return false;
}

struct Line {
    WorldParams& p;
    const Env& env;
    char* data;
    const char* keyword;
    uint32_t terminator;
    uint32_t cursor = 0;

    char* Field() const { return data + terminator + 1; }
    uint32_t IndexOf(const char* position) const { return static_cast<uint32_t>(position - data); }
    // The value after `from`: the text behind the next opening quote; the cursor follows the last value taken.
    char* Value(const char* from)
    {
        char* value = NextToken(const_cast<char*>(from));
        cursor = IndexOf(value);
        return value;
    }
    bool Fail(const char* format, const char* argument)
    {
        env.fatal(format, argument);
        return false;
    }
    bool FamilyNotFound(const char* key) { return Fail("Unrecognized commodity family \"%s\" in the gameplay data", key); }
};

// A record line (`COMFAM_x "name"`, `BUILD_x`, ...): appends the key at `count`, the cursor on the key text.
inline auto& NewRecord(Line& line, auto& table, int& count)
{
    auto& record = Record(table, count);
    record.m_id = count;
    Copy(record.m_key, line.Field());
    line.cursor = line.terminator + 1;
    ++count;
    return record;
}

// `<key>_FIELD "value"`: the id of the record whose key is the prefix, kNotFound when no record of the table has it.
inline int FindRecord(const Line& line, const auto& table, int count)
{
    return queries::FindIdByKey(table, count, line.keyword);
}

} // namespace detail

// Parses the parameter text in `data`. Returns false after a FatalError.
inline bool Process(WorldParams& p, const Env& env, char* data, uint32_t size)
{
    using namespace detail;
    bool quoted = false;
    for (uint32_t i = 0; i < size; ++i) {
        if (data[i] == '"') {
            quoted = !quoted;
            if (!quoted) data[i] = 0;
        } else if (!quoted) {
            data[i] = 0;
        }
    }
    if (quoted) {
        env.fatal("The gameplay data has an unclosed quote", nullptr);
        return false;
    }
    if (size == 0) return true;

    uint32_t cursor = 0;
    for (;;) {
        if (data[cursor] == '"') {
            char keyword[kKeywordSize] = {};
            Line line{p, env, data, keyword, 0};
            if (!ScanKeyword(data, size, cursor, keyword, &line.terminator)) return false;
            {
                char* const field = line.Field();
                int id = 0;
                if (Is(keyword, "COMFAM")) {
                    tables::CommodityFamilyDef& r = NewRecord(line, p.m_commodityFamilies, p.m_numCommodityFamilies);
                    Copy(r.m_name, line.Value(field));
                } else if (Is(keyword, "COM")) {
                    char* const name = line.Value(field);
                    char* const family = line.Value(name);
                    const int index = p.m_numCommodities;
                    tables::CommodityDef& r = Record(p.m_commodities, index);
                    r.m_id = index;
                    Copy(r.m_key, field);
                    Copy(r.m_name, name);
                    id = queries::FindCommodityFamilyId(p, family);
                    if (id == queries::kNotFound) return line.FamilyNotFound(family);
                    r.m_family = id;
                    ++p.m_numCommodities;
                } else if (Is(keyword, "HOURS")) {
                    char* const open = line.Value(field);
                    char* const close = line.Value(open);
                    const int index = p.m_numOpeningHours;
                    tables::OpeningHoursDef& r = Record(p.m_openingHours, index);
                    r.m_id = index;
                    Copy(r.m_key, field);
                    r.m_open = ToFloat(open);
                    r.m_close = ToFloat(close);
                    ++p.m_numOpeningHours;
                } else if (Is(keyword, "BUILD")) {
                    tables::BuildingDef& r = NewRecord(line, p.m_buildings, p.m_numBuildings);
                    r.m_footprint = nullptr;
                } else if ((id = FindRecord(line, p.m_buildings, p.m_numBuildings)) != queries::kNotFound) {
                    char* const value = line.Value(field);
                    world_params_add::AddBuildingData(p, env, id, field, value);
                } else if (Is(keyword, "DESIRE")) {
                    tables::DesireDef& r = NewRecord(line, p.m_desires, p.m_numDesires);
                    Copy(r.m_name, line.Value(field));
                    r.m_numFamilies = 0;
                } else if ((id = FindRecord(line, p.m_desires, p.m_numDesires)) != queries::kNotFound) {
                    char* const family = line.Value(field);
                    if (!Is(field, "COMFAM")) return line.Fail("A desire pull needs a commodity family field, got \"%s\"", field);
                    tables::DesireDef& r = Record(p.m_desires, id);
                    const int familyId = queries::FindCommodityFamilyId(p, family);
                    if (familyId == queries::kNotFound) return line.FamilyNotFound(family);
                    r.m_families.push_back(familyId);
                    ++r.m_numFamilies;
                } else if (Is(keyword, "STAFF")) {
                    char* values[12];
                    const char* from = field;
                    for (char*& value : values) {
                        value = line.Value(from);
                        from = value;
                    }
                    world_params_add::AddStaff(p, env, field, values[0], values[1], values[2], values[3], values[4], values[5], values[6],
                            values[7], values[8], values[9], values[10], values[11]);
                } else if (Is(keyword, "GUEST")) {
                    NewRecord(line, p.m_guests, p.m_numGuests);
                } else if ((id = FindRecord(line, p.m_guests, p.m_numGuests)) != queries::kNotFound) {
                    char* const first = line.Value(field);
                    if (IsGuestRangeKeyword(field)) {
                        char* const second = line.Value(first);
                        world_params_add::AddGuestFloatRange(p, env, id, field, first, second);
                    } else if (IsGuestDataKeyword(field)) {
                        world_params_add::AddGuestData(p, env, id, field, first);
                    } else {
                        return line.Fail("Unrecognized guest section \"%s\" in the gameplay data", field);
                    }
                } else if (Is(keyword, "VEHICLE")) {
                    NewRecord(line, p.m_vehicles, p.m_numVehicles);
                } else if ((id = FindRecord(line, p.m_vehicles, p.m_numVehicles)) != queries::kNotFound) {
                    world_params_add::AddVehicleData(p, env, id, field, line.Value(field));
                } else if (Is(keyword, "TABLE")) {
                    NewRecord(line, p.m_tables, p.m_numTables);
                } else if ((id = FindRecord(line, p.m_tables, p.m_numTables)) != queries::kNotFound) {
                    world_params_add::AddTableData(p, env, id, field, line.Value(field));
                } else if (Is(keyword, "SURNAME") || Is(keyword, "MALENAME") || Is(keyword, "FEMALENAME")) {
                    line.cursor = line.terminator + 1;
                    world_params_add::AddNameData(p, env, keyword, field);
                } else if (Is(keyword, "EVENT")) {
                    NewRecord(line, p.m_events, p.m_numEvents);
                } else if ((id = FindRecord(line, p.m_events, p.m_numEvents)) != queries::kNotFound) {
                    world_params_add::AddEventData(p, env, id, field, line.Value(field));
                } else if (Is(keyword, "BEER")) {
                    NewRecord(line, p.m_beers, p.m_numBeers);
                } else if ((id = FindRecord(line, p.m_beers, p.m_numBeers)) != queries::kNotFound) {
                    world_params_add::AddBeerData(p, env, id, field, line.Value(field));
                } else if (Is(keyword, "FINE")) {
                    NewRecord(line, p.m_fines, p.m_numFines);
                } else if ((id = FindRecord(line, p.m_fines, p.m_numFines)) != queries::kNotFound) {
                    char* const value = line.Value(field);
                    if (!Is(field, "COST")) return line.Fail("Unrecognized fine field \"%s\" in the gameplay data", field);
                    Record(p.m_fines, id).m_cost = ToFloat(value);
                } else if (Is(keyword, "SCENERY")) {
                    NewRecord(line, p.m_scenery, p.m_numScenery);
                } else if ((id = FindRecord(line, p.m_scenery, p.m_numScenery)) != queries::kNotFound) {
                    world_params_add::AddSceneryData(p, env, id, field, line.Value(field));
                } else {
                    return line.Fail("Unrecognized section \"%s\" in the gameplay data", keyword);
                }
            }
            cursor = line.cursor;
        }
        ++cursor;
        if (cursor >= size) return true;
    }
}

} // namespace obl::world_params_process
