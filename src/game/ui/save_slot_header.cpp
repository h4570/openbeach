#include "save_slot_header.h"

#include "core/os.h"

#include <nlohmann/json.hpp>

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>

namespace obl::save_slots {
namespace {
using Json = nlohmann::json;

// SAX handler that keeps the header's scalar fields and stops the parse when the header object closes (returning false from a
// callback ends nlohmann's parser at once), so the sections after it are never read.
// nlohmann calls the SAX callbacks by their snake_case names.
// NOLINTBEGIN(readability-identifier-naming)
struct HeaderReader {
    int depth = 0;
    std::string name; // the last key at the current depth (1: document, 2: header)
    bool format = false, version = false, inHeader = false, done = false;
    std::optional<uint32_t> savedAt;
    std::optional<int64_t> day, guests;
    std::optional<float> dayTime, cash;
    std::string scenario, title, titleText;
    bool hasScenario = false;

    bool Header() const { return inHeader && depth == 2; }
    template<class T> bool Integer(T value)
    {
        if (depth == 1 && name == "version") version = value == 2;
        if (!Header()) return true;
        if (name == "saved_at" && value >= 0 && uint64_t(value) <= 0xffffffffu) savedAt = uint32_t(value);
        if (name == "day") day = int64_t(value);
        if (name == "guests") guests = int64_t(value);
        if (name == "day_time") dayTime = float(value);
        if (name == "cash") cash = float(value);
        return true;
    }
    bool null() { return true; }
    bool boolean(bool) { return true; }
    bool number_integer(Json::number_integer_t value) { return Integer(value); }
    bool number_unsigned(Json::number_unsigned_t value) { return Integer(int64_t(value)); }
    bool number_float(Json::number_float_t value, const Json::string_t&)
    {
        // A saved f32 is written as its shortest round-tripping number, so the double narrows back to the same bits.
        if (Header() && name == "day_time") dayTime = float(value);
        if (Header() && name == "cash") cash = float(value);
        return true;
    }
    static std::optional<float> F32(const std::string& text)
    {
        // NaN and infinities: "f32:xxxxxxxx" (formats::SaveFloat).
        if (text.size() != 12 || text.compare(0, 4, "f32:")) return std::nullopt;
        char* end = nullptr;
        const unsigned long bits = std::strtoul(text.c_str() + 4, &end, 16);
        if (*end) return std::nullopt;
        return std::bit_cast<float>(uint32_t(bits));
    }
    bool string(Json::string_t& value)
    {
        if (depth == 1 && name == "format") format = value == "obl.save";
        if (!Header()) return true;
        if (name == "scenario") {
            scenario = value;
            hasScenario = true;
        } else if (name == "title")
            title = value;
        else if (name == "title_text")
            titleText = value;
        else if (name == "day_time")
            dayTime = F32(value);
        else if (name == "cash")
            cash = F32(value);
        return true;
    }
    bool binary(Json::binary_t&) { return true; }
    bool start_object(std::size_t)
    {
        ++depth;
        if (depth == 2 && name == "header") {
            // The header follows `format` and `version` (saves.md); a document without them is not a save.
            if (!format || !version) return false;
            inHeader = true;
        }
        name.clear();
        return true;
    }
    bool end_object()
    {
        if (Header()) {
            done = true;
            return false;
        }
        --depth;
        name.clear();
        return true;
    }
    bool start_array(std::size_t)
    {
        ++depth;
        return true;
    }
    bool end_array()
    {
        --depth;
        return true;
    }
    bool key(Json::string_t& value)
    {
        name = value;
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) { return false; }
};
// NOLINTEND(readability-identifier-naming)
} // namespace

bool ReadOblHeader(std::istream& in, Slot& slot)
{
    HeaderReader reader;
    Json::sax_parse(in, &reader, Json::input_format_t::json, false);
    if (!reader.done || !reader.savedAt || !reader.day || !reader.guests || !reader.dayTime || !reader.cash || !reader.hasScenario)
        return false;
    // "s3" / "sandbox3": the 1-based scenario number after the prefix.
    const size_t digits = reader.scenario.find_first_of("0123456789");
    if (digits == std::string::npos) return false;
    slot.m_time = *reader.savedAt;
    slot.m_scenario = std::atoi(reader.scenario.c_str() + digits) - 1;
    slot.m_day = int32_t(*reader.day);
    slot.m_hour = *reader.dayTime;
    slot.m_cash = *reader.cash;
    slot.m_guests = int32_t(*reader.guests);
    // The text-table key when the title is one, else its text (a title the old save stored as plain text).
    const std::string& title = reader.title.empty() ? reader.titleText : reader.title;
    std::snprintf(slot.m_title, sizeof(slot.m_title), "%s", title.c_str());
    return true;
}

std::string OblSlotPath(int number)
{
    char path[128];
    std::snprintf(path, sizeof(path), "savegames\\save%d.obl.save", number);
    return path;
}

std::string SlotPath(const Slot& slot)
{
    return slot.m_oblSave ? OblSlotPath(slot.m_number) : SlotPath(slot.m_number);
}

bool SlotFileExists(int number)
{
    std::error_code error;
    return std::filesystem::exists(os::ResolvePath(SlotPath(number)), error) ||
            std::filesystem::exists(os::ResolvePath(OblSlotPath(number)), error);
}
} // namespace obl::save_slots
