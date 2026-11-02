#include "game/world/save_sections_json.h"

#include "game/cleaning/cleaning_lifecycle.h"
#include "game/construction/construction_lifecycle.h"
#include "game/events/event_lifecycle.h"
#include "game/feedback/feedback_lifecycle.h"
#include "game/map/grid_state.h"
#include "game/ferry/ferry_lifecycle.h"
#include "game/messages/message_lifecycle.h"
#include "game/player/player_state.h"
#include "game/repair/repair_lifecycle.h"
#include "game/ui/map_view.h"
#include "game/weather/weather_save.h"
#include "formats/save_states.h"
#include "core/code_page.h"
#include "core/json_float.h"

#include <array>
#include <bit>
#include <cstring>
#include <map>
#include <span>
#include <string_view>

namespace obl::world_save {
namespace {

using nlohmann::ordered_json;

ordered_json Float(float value)
{
    return json_float::Bits(std::bit_cast<uint32_t>(value));
}

// The binary save writes the bools as 32-bit words; obl.save holds only 0 and 1.
bool Bool(const char* name, uint8_t value, ordered_json& out, std::string& error)
{
    if (value > 1) {
        error = std::string("ZEngine.") + name + ": bool value " + std::to_string(value);
        return false;
    }
    out = value != 0;
    return true;
}

// The UTF-8 of game text up to its first NUL, as the binary records keep it (WriteWString/WriteString stop there).
template<class C> std::u16string ToTerminator(const std::basic_string<C>& text)
{
    std::u16string out;
    for (const C c : text) {
        if (!c) break;
        out.push_back(static_cast<char16_t>(c));
    }
    return out;
}

// The JSON visitor over a module's VisitFields: each record goes under its name, the obl.save key, in the current scope, the section or the list
// element being written. The value rules and the key forms are the converter's (MapFields and PlaceSaveValue in
// save_writer.cpp): a list record is an array at its place, "a.b" nests objects, "list[]" appends one value per occurrence,
// "list[].field" fills one element per occurrence, bools above 1 are refused, floats follow the float rule. The visitor methods
// are the union of the modules' field-table interfaces.
class TableWriter {
public:
    TableWriter(const char* section, const JsonContext* context, ordered_json& out, std::string& error)
        : m_section(section), m_context(context), m_scope(&out), m_error(error)
    {}

    // A record of a named set (save_states.h) as its name, any other as its number.
    ordered_json StateValue(const char* name, int32_t value) const
    {
        return formats::SaveStateValue(formats::SaveStateSetOf(std::string(m_section) + "." + name), value);
    }
    template<class Seq, class Apply> void List(const char* name, const Seq& list, Apply apply)
    {
        Elements(At(name), list, [&](const auto& element) { apply(*this, element); });
    }
    template<class T> void Int(const char* name, const T& field)
    {
        static_assert(sizeof(T) == 4);
        Put(name, StateValue(name, static_cast<int32_t>(field)));
    }
    template<class T> void UInt(const char* name, const T& field)
    {
        static_assert(sizeof(T) == 4);
        if (formats::SaveStateSetOf(std::string(m_section) + "." + name) == formats::SaveStateSet::None)
            Put(name, static_cast<uint32_t>(field));
        else
            Put(name, StateValue(name, static_cast<int32_t>(field)));
    }
    void Bool(const char* name, uint8_t field)
    {
        if (field > 1) Fail(std::string(name) + ": bool value " + std::to_string(field));
        Put(name, field != 0);
    }
    void Float(const char* name, const float& field) { Put(name, world_save::Float(field)); }
    void Scratch(const char* name, const float& field) { Float(name, field); }
    void Payload(const char* name, int32_t field) { Put(name, field); }
    void Payload(const char* name, const float& field) { Float(name, field); }
    template<size_t N> void Ints(const char* name, const std::array<uint32_t, N>& values)
    {
        Ints(name, values.data(), static_cast<int>(N));
    }
    template<class T> void Ints(const char* name, const T* values, int count)
    {
        ordered_json list = ordered_json::array();
        for (int i = 0; i < count; ++i) list.push_back(static_cast<int32_t>(values[i]));
        Put(name, std::move(list));
    }
    void Floats(const char* name, const float* values, int count)
    {
        ordered_json list = ordered_json::array();
        for (int i = 0; i < count; ++i) list.push_back(world_save::Float(values[i]));
        Put(name, std::move(list));
    }
    // A feedback text: the `wide` flag, then the wide text or the text key in the pack's code page.
    void Text(const feedback::Text& text)
    {
        Put("wide", text.wide);
        if (text.wide)
            Put("text", code_page::Utf16ToUtf8(ToTerminator(text.wideText)));
        else
            Put("text", m_context->narrowToUtf8(text.key.c_str()));
    }

    // Construction: the jobs (owned pointers), their hoardings, the builder table of `count` used entries. The count's record is
    // the builders' list and a count above the table is refused, as the converter does.
    template<class Seq, class Apply> void Jobs(const char* name, const Seq& jobs, Apply apply)
    {
        Elements(At(name), jobs, [&](const auto& job) { apply(*this, *job); });
    }
    template<class Job> void Define(const Job&) {}
    template<class Seq, class Apply> void Hoardings(const char* name, const Seq& hoardings, Apply apply) { List(name, hoardings, apply); }
    template<class Table, class Apply> void Builders(int32_t count, const Table& builders, Apply apply)
    {
        if (static_cast<uint32_t>(count) > builders.size()) {
            Fail("builders: " + std::to_string(static_cast<uint32_t>(count)) + " over " + std::to_string(builders.size()));
            return;
        }
        ordered_json* out = &(*m_scope)["builders"]; // the count's record put the count there
        Elements(out, std::span(builders.data(), static_cast<size_t>(count)), [&](const auto& builder) { apply(*this, builder); });
    }

    // Ferry: the boat lists, the schedule cursor as its index (kNoCursor without one, as Save writes it), the keywords.
    template<class Seq> void Boats(const char* name, const Seq& boats)
    {
        Elements(At(name), boats, [&](const auto& boat) { VisitBoat(boat, *this); });
    }
    template<class M> void Cursor(const char* name, const M& m)
    {
        uint32_t current = ferry::kNoCursor, index = 0;
        if (m.cursor)
            for (auto node = m.schedule.begin(); node != m.schedule.end(); ++node, ++index)
                if (node == *m.cursor) current = index;
        Put(name, static_cast<int32_t>(current));
    }
    template<size_t N> void Keyword(const char* name, const char (&text)[N])
    {
        Put(name, m_context->narrowToUtf8({text, strnlen(text, N)}));
    }

    // Events: the count record is the list, then one schedule index per running event.
    template<class Seq> void Active(const char* countName, const char* idName, const Seq& ids)
    {
        if (ordered_json* out = At(countName)) *out = ordered_json::array();
        for (const int id : ids) Put(idName, id);
    }

    // Weather: the strike area's unsigned records, the cached tint, the lightning bitmap's presence.
    void Area(const char* name, int value) { Put(name, static_cast<uint32_t>(value)); }
    void Dword(const char* name, uint32_t value) { Put(name, value); }
    template<class W> void Bitmap(const char* name, const W& weather) { Put(name, weather.m_lightningLightmap != nullptr); }

private:
    void Fail(const std::string& message)
    {
        if (m_error.empty()) m_error = std::string(m_section) + "." + message;
    }
    // Writes `elements` into the array at `out` (null: the list's record is dropped), one object scope per element.
    template<class Seq, class Apply> void Elements(ordered_json* out, const Seq& elements, Apply each)
    {
        if (!out) return;
        *out = ordered_json::array();
        ordered_json* parentScope = m_scope;
        auto parentSeen = std::move(m_seen);
        for (const auto& element : elements) {
            m_scope = &out->emplace_back(ordered_json::object());
            m_seen.clear();
            each(element);
        }
        m_scope = parentScope;
        m_seen = std::move(parentSeen);
    }
    // The value's place: a record's name is its key (the modules' VisitFields). A key with "[]" serves every occurrence of its
    // name (the occurrence is the element index).
    ordered_json* At(const char* name)
    {
        const uint32_t occurrence = m_seen[name]++;
        return Place(name, std::string_view(name).find("[]") != std::string_view::npos ? occurrence : 0);
    }
    ordered_json* Place(std::string_view key, uint32_t index)
    {
        ordered_json* target = m_scope;
        for (size_t dot = key.find('.'); dot < key.find("[]"); dot = key.find('.')) { // the objects before the first list
            target = &(*target)[std::string(key.substr(0, dot))];
            key.remove_prefix(dot + 1);
        }
        const size_t at = key.find("[]");
        if (at == std::string_view::npos) return &(*target)[std::string(key)];
        ordered_json& list = (*target)[std::string(key.substr(0, at))];
        if (!list.is_array()) list = ordered_json::array();
        if (key.size() == at + 2) return &list.emplace_back();
        if (index >= list.size()) list.push_back(ordered_json::object());
        return &list[index][std::string(key.substr(at + 3))];
    }
    template<class Value> void Put(const char* name, Value&& value)
    {
        if (ordered_json* out = At(name)) *out = std::forward<Value>(value);
    }

    const char* m_section;
    const JsonContext* m_context;
    ordered_json* m_scope;
    std::map<std::string, uint32_t> m_seen; // occurrences of each "[]" name in the current scope
    std::string& m_error;
};

template<class M> bool WriteTable(const char* section, const M& manager, const JsonContext* context, ordered_json& out, std::string& error)
{
    error.clear();
    out = ordered_json::object();
    TableWriter writer(section, context, out, error);
    VisitFields(manager, writer);
    return error.empty();
}

} // namespace

bool WriteConstructionJson(const construction::Manager& manager, ordered_json& out, std::string& error)
{
    if (!WriteTable("ConstructionManager", manager, nullptr, out, error)) return false;
    // Which builder token holds each work point, which the binary record drops: without them a reloaded builder claims the first free
    // point instead of its own (chaos bot, s1_tricky: two sites, saved at tick 40).
    ordered_json& jobs = out["jobs"];
    size_t i = 0;
    for (const auto& job : manager.jobs) {
        if (i < jobs.size()) jobs[i]["work_point_owners"] = job->m_workPointOwners;
        ++i;
    }
    return true;
}

void WriteCommoditiesJson(std::span<const uint32_t> complaints, ordered_json& out)
{
    out = {{"complaints", std::vector<uint32_t>(complaints.begin(), complaints.end())}};
}

void WriteBeerJson(int32_t served, ordered_json& out)
{
    out = {{"served", served}};
}

bool WriteFerryJson(const ferry::Manager& manager, const JsonContext& context, ordered_json& out, std::string& error)
{
    return WriteTable("FerryManager", manager, &context, out, error);
}

bool WriteEventsJson(const disaster::Manager& manager, ordered_json& out, std::string& error)
{
    return WriteTable("EventManager", manager, nullptr, out, error);
}

bool WriteWeatherJson(const weather::Weather& weather, ordered_json& out, std::string& error)
{
    return WriteTable("Weather", weather, nullptr, out, error);
}

bool WriteObjectManagerJson(uint32_t nextId, uint8_t keyline, ordered_json& out, std::string& error)
{
    error.clear();
    out = ordered_json::object();
    out["next_id"] = nextId;
    if (keyline > 1) error = "ObjectManager.keyline: bool value " + std::to_string(keyline);
    out["keyline"] = keyline != 0;
    return error.empty();
}

bool WriteFeedbackJson(const feedback::Manager& manager, const JsonContext& context, ordered_json& out, std::string& error)
{
    return WriteTable("FeedbackManager", manager, &context, out, error);
}

bool WriteMessagesJson(const messages::Handler& handler, ordered_json& out, std::string& error)
{
    return WriteTable("MessageHandler", handler, nullptr, out, error);
}

bool WriteRepairJson(const repair::Manager& manager, ordered_json& out, std::string& error)
{
    return WriteTable("RepairManager", manager, nullptr, out, error);
}

bool WriteCleaningJson(const cleaning::Manager& manager, ordered_json& out, std::string& error)
{
    return WriteTable("CleaningManager", manager, nullptr, out, error);
}

void WritePlayerJson(const player::Player& player, ordered_json& out)
{
    out = ordered_json::object();
    out["name"] = code_page::Utf16ToUtf8(ToTerminator(player.m_name));
    out["cash"] = Float(player.m_cash);
}

bool WriteViewJson(const ui::MapView& view, ordered_json& out, std::string& error)
{
    error.clear();
    out = ordered_json::object();
    out["camera"] = ordered_json::array({view.m_cameraX, view.m_cameraY});
    out["position"] = ordered_json::array({Float(view.m_cameraPositionX), Float(view.m_cameraPositionY)});
    out["velocity"] = ordered_json::array({Float(view.m_velocityX), Float(view.m_velocityY)});
    // The size as the map view save computes it, read signed like its loop (a "size" of 2^31 or more writes no entries).
    ordered_json queue = ordered_json::array();
    const auto queued = static_cast<int32_t>(view.m_burnQueue.size());
    for (int32_t i = 0; i < queued; ++i) queue.push_back(static_cast<uint32_t>(view.m_burnQueue[static_cast<size_t>(i)]));
    // The converter places a list without records after the section's other keys; the same order keeps the sections equal.
    const bool emptyQueue = queue.empty();
    if (!emptyQueue) out["burn_queue"] = std::move(queue);
    if (!Bool("locked", view.m_mapLocked, out["locked"], error) || !Bool("moving", view.m_movingMap, out["moving"], error)) return false;
    out["move_duration"] = Float(view.m_moveMapDuration);
    out["move_start_time"] = Float(view.m_moveMapStartTime);
    out["move_start"] = ordered_json::array({view.m_moveMapStartX, view.m_moveMapStartY});
    out["move_destination"] = ordered_json::array({view.m_moveMapDestX, view.m_moveMapDestY});
    out["last_unburn_time"] = Float(view.m_lastUnBurnTime);
    out["guests_drawn"] = view.m_guestObjectsDrawn;
    if (emptyQueue) out["burn_queue"] = ordered_json::array();
    return true;
}

bool WriteMapJson(const map::Grid& grid, ordered_json& out, std::string& error)
{
    error.clear();
    // The binary save does not store the size; the converter takes the side of a square cell count.
    if (grid.m_width == 0 || grid.m_width != grid.m_height || grid.m_cells.size() != grid.NumCells()) {
        error = "WorldMap: " + std::to_string(grid.m_width) + " x " + std::to_string(grid.m_height) + " cells are not a square grid";
        return false;
    }
    out = ordered_json::object();
    out["cells"] = ordered_json::array({grid.m_width, grid.m_height});
    out["island_size"] = grid.m_islandSize;
    ordered_json areas = ordered_json::array();
    for (const auto* list : {&grid.m_beachAreas, &grid.m_seaAreas})
        for (const map::Area& area : *list) areas.push_back(!area.m_shut);
    out["areas_open"] = std::move(areas);
    ordered_json flags = ordered_json::array(), heights = ordered_json::array();
    for (const map::GridCell& cell : grid.m_cells) {
        flags.push_back(cell.m_flags);
        heights.push_back(cell.m_zOffset);
    }
    out["flags"] = std::move(flags);
    out["heights"] = std::move(heights);
    return true;
}

} // namespace obl::world_save
