#include "game/world/world_snapshot.h"
#include "core/ascii_case.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace obl {
namespace {

// The ObjectManager records of the snapshot, under their obl.save keys: the header, each object's type and name, the base head, the
// route and queue records and every class record VisitFields marks hashed (Vehicle's ride time is left out: the constructor never
// initializes it, so a fresh boat keeps whatever the heap block held).
constexpr std::string_view kObjectFields[]{"class", "active_objects", "active_buildings", "active_guests", "active_others", "object_ids",
        "next_id", "object.type", "object.name", "base.waiting_for_death", "base.requires_attention", "base.name", "base.id", "base.type",
        "base.owner", "base.position.x", "base.position.y", "base.position.z", "base.state", "moving.moving", "moving.mass",
        "moving.position.x", "moving.position.y", "moving.velocity.x", "moving.velocity.y", "moving.steer_force.x", "moving.steer_force.y",
        "moving.max_force", "moving.max_velocity", "moving.target.x", "moving.target.y", "moving.destination.x", "moving.destination.y",
        "moving.destination.z", "moving.destination_object", "moving.leaving_building", "moving.behaviour", "moving.path",
        "moving.path.time_stamp", "moving.path.steps", "moving.path.steps[].x", "moving.path.steps[].y", "moving.path.start.x",
        "moving.path.start.y", "moving.path.stop.x", "moving.path.stop.y", "moving.path.type", "moving.path.valid", "moving.path.reverse",
        "moving.path.current_step", "guest.tolerance", "guest.person", "guest.desires", "guest.opinion", "guest.drunk", "guest.oxygen",
        "guest.cash", "guest.happiness", "staff.happiness", "guest.vehicle", "guest.activity", "guest.action_state", "builder.action_state",
        "cleaner.action_state", "lifeguard.action_state", "rep.action_state", "security.action_state", "guest.staying_at",
        "guest.wake_time", "guest.bed_time", "guest.nights_to_stay", "guest.day_arrived", "guest.time_arrived", "guest.time_to_leave",
        "guest.sunburn", "guest.food_ill", "guest.ill", "building.staff", "building.male_staff", "building.female_staff",
        "building.non_serve_staff", "building.non_serve_staff_count", "building.staff_busy", "building.current_capacity",
        "building.open_status", "building.guests", "building.satisfaction", "building.beer_strength", "building.beer",
        "building.income_today", "building.cost_today", "building.profit_today", "building.average_profit", "building.age",
        "building.last_fault_day_time", "building.last_fault_day", "building.faults", "building.user_repair_interval",
        "building.repair_interval", "building.repair_state", "scenery.repair_state", "building.was_broken", "building.last_repair_call",
        "building.last_repair_day_time", "building.last_repair_day", "building.visits_today", "building.visits_since_cleaned",
        "building.last_fail_time", "building.power_consumption", "building.cleaned_today", "building.closed_for_power",
        "building.user_closed", "building.called_for_inspection", "building.called_for_repair", "building.built_by_user",
        "building.table_count", "building.queue", "building.queue.owner", "shark.owner", "table.owner", "vehicle.owner",
        "building.queue.max_length", "building.queue.guests", "building.queue.guests[]", "building.queue.commodities",
        "building.queue.commodities[]", "building.lounger_count", "building.influence_area", "lifeguard.influence_area", "static.grid_w",
        "static.grid_h", "static.cleaner_claimed", "static.mechanic_claimed", "static.called_cleaning", "static.urgent_cleaning",
        "static.called_repairing", "static.urgent_repairing", "builder.job_type", "staff.home", "staff.wage", "staff.salary_expectation",
        "staff.start_time", "staff.end_time", "staff.hired_on", "staff.sacked", "staff.ended_shift", "vehicle.current_capacity",
        "vehicle.way_point_count", "vehicle.current_way_point", "vehicle.vehicle_type"};

const std::unordered_set<std::string_view>& ObjectFields()
{
    static const std::unordered_set<std::string_view> fields(std::begin(kObjectFields), std::end(kObjectFields));
    return fields;
}

} // namespace

std::string_view WorldSnapshot::Intern(std::string_view s)
{
    auto it = m_strings.find(s);
    if (it == m_strings.end()) it = m_strings.emplace(s).first;
    return *it;
}

void WorldSnapshot::Class(const char* name)
{
    std::string_view section = name;
    constexpr std::string_view prefix = "Class name : ";
    if (section.starts_with(prefix)) section.remove_prefix(prefix.size());
    m_section = Intern(section);
    Add("class", 0, m_section.data(), m_section.size());
}

void WorldSnapshot::Add(const char* name, uint8_t type, const void* bytes, size_t size)
{
    std::string_view field = name ? name : "";
    // Normalize drops these anyway; skipping them here keeps the ~90% of the stream that is object data out of the arena.
    if (m_section == "ObjectManager" && !ObjectFields().contains(field)) return;
    const auto offset = static_cast<uint32_t>(m_arena.size());
    if (size) {
        const auto* begin = static_cast<const uint8_t*>(bytes);
        m_arena.insert(m_arena.end(), begin, begin + size);
    }
    m_fields.push_back({m_section, Intern(field), type, offset, static_cast<uint32_t>(size)});
}

void WorldSnapshot::String(const char* name, const char* text)
{
    // Content names are hashed in their key's spelling: a saved name is its key, a placed one may keep case and spaces.
    std::string canonical(text);
    for (char& c : canonical) c = c == ' ' ? '_' : ascii::Lower(c);
    Add(name, uint8_t(RecordType::String), canonical.data(), canonical.size());
}

void WorldSnapshot::Normalize()
{
    std::vector<uint32_t> vehicleIds;
    for (size_t start = 0; start < m_fields.size(); ++start) {
        const auto& type = m_fields[start];
        if (type.section != "ObjectManager" || type.name != "object.type" || type.size != 1 || Bytes(type)[0] != 4) continue;
        for (size_t i = start + 1; i < m_fields.size() && m_fields[i].section == "ObjectManager" && m_fields[i].name != "object.type";
                ++i) {
            if (m_fields[i].name == "base.id" && m_fields[i].size == 4) {
                uint32_t id;
                std::memcpy(&id, Bytes(m_fields[i]), sizeof(id));
                vehicleIds.push_back(id);
            }
        }
    }
    // Object records share one manager header; object.type delimits each derived-to-base group for normalization.
    for (size_t start = 0; start < m_fields.size(); ++start) {
        if (m_fields[start].section != "ObjectManager" || m_fields[start].name != "object.type") continue;
        size_t end = start + 1;
        while (end < m_fields.size() && m_fields[end].section == "ObjectManager" && m_fields[end].name != "object.type") ++end;
        auto number = [&](const char* name) {
            uint32_t value = 0;
            for (size_t i = start; i < end; ++i)
                if (m_fields[i].name == name && m_fields[i].size == sizeof(value)) std::memcpy(&value, Bytes(m_fields[i]), sizeof(value));
            return value;
        };
        uint32_t serving = number("building.male_staff") + number("building.female_staff");
        uint32_t other = number("building.non_serve_staff_count");
        for (size_t i = start; i < end; ++i) {
            auto& f = m_fields[i];
            // DismissStaff compacts IDs in the active prefix (both genders combined).
            size_t used = f.size;
            if (f.name == "building.staff") used = size_t(serving) * 4;
            if (f.name == "building.staff_busy") used = serving;
            if (f.name == "building.non_serve_staff") used = size_t(other) * 4;
            if (used < f.size) f.size = static_cast<uint32_t>(used);
            // A guest with no assigned vehicle can retain allocator bytes in this otherwise unused field.
            if (f.name == "guest.vehicle" && f.size == 4) {
                uint32_t id;
                std::memcpy(&id, Bytes(f), sizeof(id));
                if (std::find(vehicleIds.begin(), vehicleIds.end(), id) == vehicleIds.end()) std::fill_n(Bytes(f), f.size, uint8_t(0xff));
            }
        }
        start = end - 1;
    }
    // A daily ferry does not initialize/use Day. The flag follows Day in the stream.
    for (size_t i = 1; i < m_fields.size(); ++i) {
        const auto& f = m_fields[i];
        if (f.section == "FerryManager" && f.name == "time_flags" && f.size == 4 && (Bytes(f)[0] & 1) && m_fields[i - 1].name == "day")
            std::fill_n(Bytes(m_fields[i - 1]), m_fields[i - 1].size, uint8_t(0));
    }
    // Weather shares the construction section because it has no class header. Flash time, strike point and strike area only matter
    // while their effects are active: lighting updates write the time with the flash flag, arm a strike after setting its point,
    // and read the area only during a strike. Normalize inactive values so unused storage cannot affect the checksum.
    // The schedule time likewise becomes meaningful only after the first event has activated.
    for (size_t start = 0; start < m_fields.size(); ++start) {
        if (m_fields[start].section != "ConstructionManager" || m_fields[start].name != "light_index") continue;
        size_t end = start + 1;
        while (end < m_fields.size() && m_fields[end].section == "ConstructionManager" && m_fields[end].name != "light_index") ++end;
        auto nonZero = [&](const char* name) {
            for (size_t i = start; i < end; ++i)
                if (m_fields[i].name == name)
                    return std::any_of(Bytes(m_fields[i]), Bytes(m_fields[i]) + m_fields[i].size, [](uint8_t b) { return b != 0; });
            return true; // a block without the field (older stream) is left as it is
        };
        const bool flashing = nonZero("light_flash"), armed = nonZero("strike_time"), striking = nonZero("strikes");
        const bool scheduled = nonZero("next_event");
        for (size_t i = start; i < end; ++i) {
            auto& f = m_fields[i];
            // Strikes use only the horizontal coordinates; exclude the unused height even while a strike is armed.
            const bool clear = (f.name == "flash_time" && !flashing) || (f.name.starts_with("strike_point.") && !armed) ||
                    f.name == "strike_point.z" || (f.name.starts_with("strike_area.") && !striking) || (f.name == "time" && !scheduled);
            if (clear) std::fill_n(Bytes(f), f.size, uint8_t(0));
        }
        start = end - 1;
    }
    // Work-point collection assigns a nonzero direction to each usable footprint cell. Direction zero marks an unused slot,
    // whose coordinates must not affect the checksum. Normalize those coordinates even when all ten slots came from a save.
    for (size_t i = 2; i < m_fields.size(); ++i) {
        const auto& dir = m_fields[i];
        auto& x = m_fields[i - 2];
        auto& y = m_fields[i - 1];
        if (dir.section != "ConstructionManager" || dir.name != "work_points[].dir" || x.name != "work_points[].x" ||
                y.name != "work_points[].y" || std::any_of(Bytes(dir), Bytes(dir) + dir.size, [](uint8_t b) { return b != 0; }))
            continue;
        std::fill_n(Bytes(x), x.size, uint8_t(0));
        std::fill_n(Bytes(y), y.size, uint8_t(0));
    }
    const auto& objectFields = ObjectFields();
    std::erase_if(m_fields, [&](const WorldField& f) {
        // The camera is presentation state. The history arrays contain uninitialized future days.
        return f.section == "ZEngine" ||
                (f.section == "World" &&
                        (f.name == "boat_list" || f.name == "profit_history" || f.name == "income_history" || f.name == "cost_history")) ||
                (f.section == "ObjectManager" && !objectFields.contains(f.name)) ||
                (f.section == "ConstructionManager" && f.name == "last_colour") ||
                // Scan distance is temporary state; staff hours are meaningful only after the schedule initializes them.
                (f.section == "RepairManager" && f.name == "distance") ||
                (f.section == "MessageHandler" && (f.name == "float_data" || (f.name == "extra" && f.type == 4)));
    });
}

// Rendered once per checkpoint over ~100k fields; the text is megabytes, so it is sized up front and hex is written through a pointer
// (per-character `+=` costs hundreds of milliseconds in a Debug build).
std::string WorldSnapshot::Text() const
{
    constexpr std::string_view kHeader = "OBL-world-v3\n";
    size_t size = kHeader.size();
    for (const auto& f : m_fields) size += f.section.size() + f.name.size() + 3 + 3 + 1 + f.size * 2 + 1;
    std::string text;
    text.reserve(size);
    text += kHeader;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto& f : m_fields) {
        text += f.section;
        text += '\t';
        text += f.name;
        text += '\t';
        char type[3];
        text.append(type, std::to_chars(type, type + sizeof(type), f.type).ptr);
        text += '\t';
        const size_t at = text.size();
        text.resize(at + f.size * 2);
        char* out = text.data() + at;
        for (const uint8_t *p = Bytes(f), *end = p + f.size; p != end; ++p) {
            const uint8_t b = *p;
            *out++ = hex[b >> 4];
            *out++ = hex[b & 15];
        }
        text += '\n';
    }
    return text;
}

std::string WorldSnapshot::FirstDifference(std::string_view a, std::string_view b, std::string_view nameA, std::string_view nameB)
{
    if (a == b) return {};
    size_t at = 0, line = 0, lineStart = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) {
        if (a[at] == '\n') {
            ++line;
            lineStart = at + 1;
        }
        ++at;
    }
    auto record = [&](std::string_view text) {
        std::string_view rest = text.substr(std::min(lineStart, text.size()));
        rest = rest.substr(0, std::min(rest.find('\n'), size_t{160}));
        return std::string(rest.empty() ? "<end>" : rest);
    };
    std::string out = "line " + std::to_string(line + 1) + ' ';
    out.append(nameA).append("=[").append(record(a)).append("] ");
    out.append(nameB).append("=[").append(record(b)).append("]");
    return out;
}

std::string WorldSnapshot::HashOf(std::string_view text)
{
    // FNV-1a is a diagnostic checksum, not a security hash. It covers the header, types, bytes, record boundaries and order, but
    // not the section and field names (the first two columns): renaming a record keeps every reference hash (schema 3).
    uint64_t hash = 14695981039346656037ull;
    ForEachHashedLine(text, [&](std::string_view line) {
        for (unsigned char c : line) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
    });
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

} // namespace obl
