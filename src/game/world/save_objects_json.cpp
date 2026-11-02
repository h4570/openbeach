#include "core/char16.h"
#include "game/world/save_objects_json.h"

#include "game/map/path_data.h"
#include "game/objects/building_events_state.h"
#include "game/objects/object_building.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_performers.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_static.h"
#include "game/objects/object_table.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/staff_serial.h"
#include "game/ui/sprite.h"
#include "formats/save_states.h"
#include "core/code_page.h"
#include "core/content_key.h"
#include "core/json_float.h"

#include <bit>
#include <cstring>
#include <functional>
#include <map>
#include <string_view>

namespace obl::world_save {
namespace {

using nlohmann::ordered_json;
using object::Hashed;
using object::ObjectType;

ordered_json Float(float value)
{
    return json_float::Bits(std::bit_cast<uint32_t>(value));
}

template<size_t N> std::string_view CText(const char (&text)[N])
{
    return {text, strnlen(text, N)};
}

// The obl.save kind of an object type byte (kObjectKinds of the converter).
const char* KindOf(ObjectType type)
{
    switch (type) {
    case ObjectType::Scenery: return "scenery";
    case ObjectType::Building: return "building";
    case ObjectType::Guest: return "guest";
    case ObjectType::Vehicle: return "vehicle";
    case ObjectType::Table: return "table";
    case ObjectType::Shark: return "animal";
    case ObjectType::Cabaret:
    case ObjectType::Singer:
    case ObjectType::WeightLifter:
    case ObjectType::Catwalk:
    case ObjectType::WetTShirt:
    case ObjectType::Aerobic: return "performer";
    default: return "staff";
    }
}

// The `class` of a staff member or performer: what `kind` and `type` leave open.
const char* ClassOf(ObjectType type)
{
    switch (type) {
    case ObjectType::ServiceStaff: return "service";
    case ObjectType::SnootyStaff: return "waiter";
    case ObjectType::Lifeguard: return "lifeguard";
    case ObjectType::Mechanic: return "mechanic";
    case ObjectType::Rep: return "rep";
    case ObjectType::Builder: return "builder";
    case ObjectType::Cleaner: return "cleaner";
    case ObjectType::SecurityGuard: return "security";
    case ObjectType::Cabaret: return "cabaret";
    case ObjectType::Singer: return "singer";
    case ObjectType::WeightLifter: return "weightlifter";
    case ObjectType::Catwalk: return "catwalk";
    case ObjectType::WetTShirt: return "wettshirt";
    case ObjectType::Aerobic: return "aerobic";
    default: return nullptr;
    }
}

// The stand points of a queue with a points table (limit 1..10): limit + 1 of them, the serve side first.
uint32_t QueuePointCount(const object::BuildingQueue& queue)
{
    return queue.m_points && queue.m_maxLength >= 1 && queue.m_maxLength <= 10 ? queue.m_maxLength + 1 : 0;
}

ordered_json Point(float x, float y)
{
    return {{"x", world_save::Float(x)}, {"y", world_save::Float(y)}};
}

// The guest and staff state the binary records drop, after the saved records of their sections (saves.md, the dropped-state
// section); every profession, the restaurant waiter included, shares Staff and its `staff` section.
void GuestExtras(const object::Guest& guest, ordered_json& out)
{
    out["topless"] = guest.m_topless;
    out["times_blocked"] = guest.m_NumTimesBlocked;
    out["mood_icon_time"] = world_save::Float(guest.m_moodIconTime);
    out["worst"] = guest.m_Worst;
}

void StaffExtras(const object::Staff& staff, ordered_json& out)
{
    out["target_happiness"] = world_save::Float(staff.m_targetHappiness);
    out["happiness_rate"] = world_save::Float(staff.m_happinessRate);
    out["happiness_changing"] = staff.m_happinessChanging != 0;
}

// The JSON visitor over the object classes' VisitFields: each record goes under its key in the object. The key forms are the
// converter's (PlaceSaveValue): "a.b" nests objects, "list[]" appends one value per occurrence, "list[].field" fills element i at
// the key's i-th occurrence. Bools above 1 are refused, floats follow the float rule.
class ObjectWriter {
public:
    ObjectWriter(const std::string& where, const JsonContext& context, ordered_json& out, std::string& error)
        : m_where(where), m_context(context), m_out(out), m_error(error)
    {}

    template<class T> void Int(const char* key, const T& field, Hashed)
    {
        static_assert(sizeof(T) == 4);
        if (IsBuildingKey(key)) {
            CatalogValue(key, static_cast<int32_t>(field), "buildings", m_context.buildingKey);
            return;
        }
        if (std::string_view(key) == "building.beer") {
            CatalogValue(key, static_cast<int32_t>(field), "beers", m_context.beerKey);
            return;
        }
        State(key, static_cast<int32_t>(field));
    }
    void Floats(const char* key, const float* values, int count, Hashed)
    {
        ordered_json& out = Place(key);
        out = ordered_json::array();
        for (int i = 0; i < count; ++i) out.push_back(world_save::Float(values[i]));
    }
    void Bytes(const char* key, const uint8_t* values, int count, Hashed)
    {
        ordered_json& out = Place(key);
        out = ordered_json::array();
        for (int i = 0; i < count; ++i) out.push_back(values[i]);
    }
    // A guest's heap name (wide text, the converter's WideText).
    void Name(const char* key, const Char16* name, Hashed)
    {
        std::u16string wide;
        for (const Char16* c = name; c && *c; ++c) wide.push_back(static_cast<char16_t>(*c));
        Place(key) = code_page::Utf16ToUtf8(wide);
    }
    template<class T> void UInt(const char* key, const T& field, Hashed)
    {
        if (formats::SaveStateSetOf(key) != formats::SaveStateSet::None)
            State(key, static_cast<int32_t>(field));
        else
            Place(key) = static_cast<uint32_t>(field);
    }
    void Byte(const char* key, uint8_t field, Hashed) { Place(key) = field; }
    void Float(const char* key, float field, Hashed) { Place(key) = world_save::Float(field); }
    void Ints(const char* key, const int32_t* values, int count, Hashed)
    {
        ordered_json& out = Place(key);
        out = ordered_json::array();
        for (int i = 0; i < count; ++i) out.push_back(values[i]);
    }
    // A sprite's own records (kSpriteFields) under `key`; one list element per call for a "list[]" key.
    // A performer's attached sprite: present or not, then its name and records (kAttachedSpriteFields) under `key`.
    template<class P> void Attached(const char* key, P& sprite, object::AttachedMode)
    {
        if (!sprite) return;
        const auto& attached = *static_cast<const ui::SpriteInstance*>(sprite);
        ordered_json fields;
        SpriteFields(attached, fields);
        ordered_json& out = Place(key);
        out = {{"sprite", m_context.narrowToUtf8(attached.m_name)}};
        out.update(fields);
    }
    // A staff member's attached sprite: the same records under `key`.
    template<class P> void Attached(const char* key, P& sprite, Hashed) { Attached(key, sprite, object::AttachedMode{}); }
    // Wide text up to its terminator, in UTF-8 (the converter's WideText).
    void Wide(const char* key, const Char16* text, int capacity, Hashed)
    {
        std::u16string wide;
        for (int i = 0; i < capacity && text[i]; ++i) wide.push_back(static_cast<char16_t>(text[i]));
        Place(key) = code_page::Utf16ToUtf8(wide);
    }
    // Narrow text up to its terminator, through the pack's code page (the converter's Text).
    void Text(const char* key, const char* text, int capacity, Hashed)
    {
        Place(key) = m_context.narrowToUtf8(std::string_view(text, strnlen(text, static_cast<size_t>(capacity))));
    }
    // A class-wide staff counter (game::Context), written by every member of the class.
    void Counter(const char* key, Hashed) { Place(key) = staff::lifecycle::CounterValue(key); }
    // One bit of a job mask as the bool record the binary save writes: the upper bytes stay.
    void MaskBit(const char* key, uint32_t mask, int bit, Hashed)
    {
        const uint32_t value = bit ? (mask >> bit) & 0xffffff01u : mask & 1u;
        BoolValue(key, value > 0xff ? 0xff : static_cast<uint8_t>(value));
    }
    // The lifeguard's area pointer is rebuilt on load from its index; nothing to write.
    void BeachArea(const map::Area*, int32_t) {}
    // A rectangle's four records under `key` (the converter's ".left" and so on).
    void Rect(const char* key, const screen::Rect& r, Hashed)
    {
        ordered_json& out = Place(key);
        out = {{"left", static_cast<uint32_t>(r.left)}, {"top", static_cast<uint32_t>(r.top)}, {"right", static_cast<uint32_t>(r.right)},
                {"bottom", static_cast<uint32_t>(r.bottom)}};
    }
    void Sprite(const char* key, const void* sprite) { SpriteFields(*static_cast<const ui::SpriteInstance*>(sprite), Place(key)); }
    void Bool(const char* key, uint8_t field, Hashed)
    {
        // The traversal flag is a byte whose uninitialized values old saves keep (the converter's ByteFlag).
        if (std::string_view(key) == "moving.building_traversal") {
            Place(key) = field;
            return;
        }
        BoolValue(key, field);
    }
    // The "on path" flag is the presence of `moving.path`, the route's records its keys (kPathFields).
    void Route(const char*, const path::RouteCursor* cursor, Hashed)
    {
        if (!cursor) return;
        const path::Route& route = *cursor->m_route;
        ordered_json& out = Place("moving.path");
        out = ordered_json::object();
        out["time_stamp"] = world_save::Float(route.m_timeStamp);
        out["ref_count"] = static_cast<uint32_t>(route.m_referenceCount);
        ordered_json& steps = out["steps"] = ordered_json::array();
        for (const map::TilePoint& step : route.m_steps) steps.push_back({{"x", step.x}, {"y", step.y}});
        out["start"] = {{"x", static_cast<uint32_t>(route.m_start[0])}, {"y", static_cast<uint32_t>(route.m_start[1])}};
        out["stop"] = {{"x", static_cast<uint32_t>(route.m_stop[0])}, {"y", static_cast<uint32_t>(route.m_stop[1])}};
        out["type"] = formats::SaveStateValue(formats::SaveStateSet::RouteType, route.m_pathType);
        BoolValue("moving.path.valid", route.m_valid);
        BoolValue("moving.path.reverse", cursor->m_reverse);
        out["current_step"] = static_cast<uint32_t>(cursor->m_currentStep);
    }

    // A building's animation sprites (as many as its definition names), then the optional extra one, one list element each.
    void Sprites(const char* key, const object::Building& b)
    {
        for (int32_t i = 0; i < b.m_definition->m_NumAnimSprites; ++i) SpriteFields(*b.m_animSprites[i], Place(key));
        if (b.m_constructionSprite) SpriteFields(*static_cast<const ui::SpriteInstance*>(b.m_constructionSprite), Place(key));
    }
    // The two flags the building save asks the ConstructionManager for.
    void Construction(const object::Building& b)
    {
        Place("building.under_construction") = m_context.underConstruction && m_context.underConstruction(b.m_id);
        Place("building.under_demolition") = m_context.underDemolition && m_context.underDemolition(b.m_id);
    }
    // The show schedule, only when the building has one.
    void Events(const char* key, const object::Building& b)
    {
        if (!b.m_events) return;
        const object::BuildingEvents& events = *b.m_events;
        const std::string base = key;
        Place(base + ".owner") = events.m_OwnerID;
        BoolValue(base + ".active", Place(base + ".active"), events.m_eventActive);
        for (const object::EventSlot& slot : events.m_slots) {
            CatalogValue((base + ".slots[].event").c_str(), slot.m_EventID, "events", m_context.eventKey);
            Place(base + ".slots[].actor_count") = static_cast<uint32_t>(slot.m_NumActors);
            ordered_json& actors = Place(base + ".slots[].actors") = ordered_json::array();
            for (size_t i = 0; i < slot.m_actors.size(); ++i) actors.push_back(slot.m_actors[i]);
            Place(base + ".slots[].guest_count") = static_cast<uint32_t>(slot.m_NumGuests);
            ordered_json& guests = Place(base + ".slots[].guests") = ordered_json::array();
            for (size_t i = 0; i < slot.m_guests.size(); ++i) guests.push_back(slot.m_guests[i]);
            Place(base + ".slots[].start") = world_save::Float(slot.m_start);
            Place(base + ".slots[].duration") = world_save::Float(slot.m_duration);
            Place(base + ".slots[].price") = world_save::Float(slot.m_price);
        }
    }
    // The thought list: the thinking object and the text of each entry.
    void Thoughts(const char* key, const object::Building& b)
    {
        ordered_json& out = Place(key) = ordered_json::array();
        for (const object::ThoughtEntry* entry : b.m_ThoughtList) {
            std::u16string wide;
            for (const Char16* c = entry->m_text; *c; ++c) wide.push_back(static_cast<char16_t>(*c));
            out.push_back({{"object", entry->m_objectId}, {"text", code_page::Utf16ToUtf8(wide)}});
        }
    }
    // The guest queue (the building queue save), only when the building has one, then what the binary record drops: the tail and the points
    // table (saves.md, the dropped-state section). A load lays the points out in a straight line from the serve point, while
    // the live table bends around what RebuildPoints met (chaos bot, s12_queue_points_save).
    void Queue(const char*, const object::Building& b, Hashed)
    {
        if (!b.m_queue) return;
        const auto& queue = *static_cast<const object::BuildingQueue*>(b.m_queue);
        ordered_json& out = Place("building.queue");
        out["owner"] = static_cast<int32_t>(queue.m_ownerId);
        out["max_length"] = static_cast<int32_t>(queue.m_maxLength);
        out["serve_point"] = {{"x", world_save::Float(queue.m_ServePoint[0])}, {"y", world_save::Float(queue.m_ServePoint[1])}};
        out["direction"] = {{"x", world_save::Float(queue.m_QueueDirection[0])}, {"y", world_save::Float(queue.m_QueueDirection[1])}};
        ordered_json& guests = out["guests"] = ordered_json::array();
        for (size_t i = 0; i < queue.m_qlQueue.size(); ++i) guests.push_back(queue.m_qlQueue[i]);
        ordered_json& commodities = out["commodities"] = ordered_json::array();
        for (size_t i = 0; i < queue.m_qlCommmod.size(); ++i) commodities.push_back(queue.m_qlCommmod[i]);
        out["tail"] = Point(queue.m_tail[0], queue.m_tail[1]);
        if (const uint32_t count = QueuePointCount(queue)) {
            ordered_json& points = out["points"] = ordered_json::array();
            for (uint32_t i = 0; i < count; ++i) points.push_back(Point(queue.m_points[i * 2], queue.m_points[i * 2 + 1]));
        }
    }

    // The sprite instance's records (kSpriteFields) into `out`.
    void SpriteFields(const ui::SpriteInstance& sprite, ordered_json& out)
    {
        out = ordered_json::object();
        out["start_time"] = world_save::Float(sprite.m_startTime);
        out["anim_type"] = static_cast<uint32_t>(sprite.m_animType);
        out["fps"] = static_cast<uint32_t>(sprite.m_fps);
        out["anim"] = static_cast<uint32_t>(sprite.m_animIndex);
        out["frame"] = static_cast<uint32_t>(sprite.m_frameIndex);
        out["dir"] = static_cast<uint32_t>(sprite.m_direction);
        out["palette"] = static_cast<uint32_t>(sprite.m_paletteIndex);
        BoolValue("sprite.lit", out["lit"], sprite.m_lit);
        BoolValue("sprite.keyline", out["keyline"], sprite.m_keyline);
        BoolValue("sprite.triggered_sound", out["triggered_sound"], sprite.m_triggeredSound);
    }

    // The base object's records after the identity records (kBaseFields): the head's flags and state, the draw fields, the sprite.
    void BaseFields(const object::Base& o)
    {
        BoolValue("base.waiting_for_death", o.m_waitingForDeath);
        BoolValue("base.requires_attention", o.m_requiresAttention);
        Place("base.owner") = o.m_owner;
        Place("base.state") = formats::SaveStateValue(formats::SaveStateSet::ObjectState, o.m_state, &m_context.objectStates);
        BoolValue("base.visible", o.m_visible);
        Place("base.draw_offset_x") = static_cast<uint32_t>(o.m_XDrawOffset);
        Place("base.draw_offset_y") = static_cast<uint32_t>(o.m_YDrawOffset);
        const auto& sprite = *static_cast<const ui::SpriteInstance*>(o.m_sprite);
        Place("base.sprite") = m_context.narrowToUtf8(sprite.m_name);
        SpriteFields(sprite, Place("sprite"));
        BoolValue("base.debug_break", o.m_debugBreak);
        Place("base.select_level") = static_cast<uint32_t>(o.m_SelectLevel);
    }

private:
    // The guest's building types are catalog building keys, -1 none.
    static bool IsBuildingKey(std::string_view key)
    {
        return key == "guest.best_type" || key == "guest.worst_type" || key == "guest.current_type";
    }
    // A catalog list position as its key (the converter's Building, Beer and Event), null for -1, refused without an entry.
    void CatalogValue(const char* key, int32_t id, const char* list, const std::function<std::string(int32_t)>& lookup)
    {
        ordered_json& out = Place(key);
        if (id == -1) return; // null
        std::string entry = lookup ? lookup(id) : std::string();
        if (entry.empty() && m_error.empty()) m_error = m_where + "." + key + ": no catalog " + list + " entry " + std::to_string(id);
        out = std::move(entry);
    }
    void State(const char* key, int32_t value) { Place(key) = formats::SaveStateValue(formats::SaveStateSetOf(key), value); }
    void BoolValue(const char* key, uint8_t field) { BoolValue(key, Place(key), field); }
    void BoolValue(std::string_view key, ordered_json& out, uint8_t field)
    {
        if (field > 1 && m_error.empty()) m_error = m_where + "." + std::string(key) + ": bool value " + std::to_string(field);
        out = field != 0;
    }
    // The value's place for `key` (the converter's key forms), created on first use.
    ordered_json& Place(std::string_view key)
    {
        const uint32_t occurrence = m_seen[std::string(key)]++;
        ordered_json* target = &m_out;
        const size_t list = key.find("[]");
        std::string_view path = key.substr(0, list);
        for (size_t dot = path.find('.'); dot != std::string_view::npos; dot = path.find('.')) {
            target = &(*target)[std::string(path.substr(0, dot))];
            path.remove_prefix(dot + 1);
        }
        target = &(*target)[std::string(path)];
        if (list == std::string_view::npos) return *target;
        if (!target->is_array()) *target = ordered_json::array();
        while (target->size() <= occurrence) target->push_back(nullptr);
        target = &(*target)[occurrence];
        std::string_view rest = key.substr(list + 2);
        if (rest.empty()) return *target;
        rest.remove_prefix(1); // the '.'
        for (size_t dot = rest.find('.'); dot != std::string_view::npos; dot = rest.find('.')) {
            target = &(*target)[std::string(rest.substr(0, dot))];
            rest.remove_prefix(dot + 1);
        }
        return (*target)[std::string(rest)];
    }

    const std::string& m_where;
    const JsonContext& m_context;
    ordered_json& m_out;
    std::string& m_error;
    std::map<std::string, uint32_t> m_seen; // occurrences of each key so far
};

void WriteMoving(const object::Mover& o, ObjectWriter& w)
{
    object::VisitFields(o, w);
    w.BaseFields(o);
}

void WriteStatic(const object::Prop& o, ObjectWriter& w)
{
    object::VisitFields(o, w);
    w.BaseFields(o);
}

// A moving class: its own fields, then Mover's and the base.
template<class T> void WriteClass(const T& o, ObjectWriter& w)
{
    object::VisitFields(o, w);
    WriteMoving(o, w);
}

// A staff class: its own fields, Staff's, then the moving layer and the base (Person saves nothing of its own).
template<class T> void WriteStaff(const T& o, ObjectWriter& w, ordered_json& out)
{
    object::VisitFields(o, w);
    object::VisitFields(static_cast<const object::Staff&>(o), w);
    WriteMoving(o, w);
    StaffExtras(o, out["staff"]);
}

} // namespace

bool HasObjectWriter(const object::Base& object)
{
    switch (object.m_type) {
    case ObjectType::Shark:
    case ObjectType::Scenery:
    case ObjectType::Building:
    case ObjectType::Vehicle:
    case ObjectType::Table:
    case ObjectType::Cabaret:
    case ObjectType::Singer:
    case ObjectType::WeightLifter:
    case ObjectType::Catwalk:
    case ObjectType::WetTShirt:
    case ObjectType::Aerobic:
    case ObjectType::Guest:
    case ObjectType::ServiceStaff:
    case ObjectType::Lifeguard:
    case ObjectType::Mechanic:
    case ObjectType::Rep:
    case ObjectType::Builder:
    case ObjectType::Cleaner:
    case ObjectType::SecurityGuard:
    case ObjectType::SnootyStaff: return true;
    default: return false;
    }
}

bool WriteObjectJson(const object::Base& o, const JsonContext& context, ordered_json& out, std::string& error)
{
    error.clear();
    const std::string where = std::string(KindOf(o.m_type)) + " " + std::to_string(o.m_id);
    if (!HasObjectWriter(o)) {
        error = where + ": no writer for object type " + std::to_string(static_cast<int>(o.m_type));
        return false;
    }
    out = ordered_json::object();
    const std::string_view name = CText(o.m_Name);
    const std::string key = content_key::ContentKey(name);
    out["id"] = o.m_id;
    out["kind"] = KindOf(o.m_type);
    out["type"] = key;
    if (const char* objectClass = ClassOf(o.m_type)) out["class"] = objectClass;
    out["pos"] = ordered_json::array({Float(o.m_WorldXYZ[0]), Float(o.m_WorldXYZ[1]), Float(o.m_WorldXYZ[2])});
    // The name the manager creates the object by: m_Name for every class but scenery (the object manager's "Object Name" record).
    const std::string_view created = o.m_type == ObjectType::Scenery ? CText(static_cast<const object::Scenery&>(o).m_ObjectName) : name;
    if (std::string createdKey = content_key::ContentKey(created); createdKey != key) out["created_as"] = std::move(createdKey);

    ObjectWriter writer(where, context, out, error);
    switch (o.m_type) {
    case ObjectType::Shark: {
        const auto& shark = static_cast<const object::Shark&>(o);
        object::VisitFields(shark, writer);
        WriteMoving(shark, writer);
        break;
    }
    case ObjectType::Vehicle: {
        const auto& vehicle = static_cast<const object::Vehicle&>(o);
        object::VisitFields(vehicle, writer);
        WriteMoving(vehicle, writer);
        break;
    }
    case ObjectType::Scenery: {
        const auto& scenery = static_cast<const object::Scenery&>(o);
        object::VisitFields(scenery, writer);
        WriteStatic(scenery, writer);
        break;
    }
    case ObjectType::Building: {
        const auto& building = static_cast<const object::Building&>(o);
        object::VisitFields(building, writer);
        WriteStatic(building, writer);
        // The rotation is the sprite direction (0..3, a quarter turn each), the object's `dir` after the records (the converter's).
        const uint32_t dir = out["sprite"]["dir"].get<uint32_t>();
        if (dir > 3) {
            error = where + ": building rotation out of range";
            return false;
        }
        static constexpr const char* kQuarterTurns[] = {"n", "e", "s", "w"};
        out["dir"] = kQuarterTurns[dir];
        out["sprite"].erase("dir");
        break;
    }
    case ObjectType::Table: {
        const auto& table = static_cast<const object::Furniture&>(o);
        object::VisitFields(table, writer);
        WriteStatic(table, writer);
        break;
    }
    case ObjectType::Cabaret: WriteClass(static_cast<const object::Cabaret&>(o), writer); break;
    case ObjectType::Singer: WriteClass(static_cast<const object::Singer&>(o), writer); break;
    case ObjectType::WeightLifter: WriteClass(static_cast<const object::WeightLifter&>(o), writer); break;
    case ObjectType::Catwalk: WriteClass(static_cast<const object::Catwalk&>(o), writer); break;
    case ObjectType::WetTShirt: WriteClass(static_cast<const object::WetTShirt&>(o), writer); break;
    case ObjectType::Aerobic: WriteClass(static_cast<const object::Aerobic&>(o), writer); break;
    case ObjectType::Guest: {
        const auto& guest = static_cast<const object::Guest&>(o);
        WriteClass(guest, writer);
        GuestExtras(guest, out["guest"]);
        break;
    }
    case ObjectType::ServiceStaff: WriteStaff(static_cast<const object::ServiceStaff&>(o), writer, out); break;
    // The waiter saves through the service staff's function (the same layout up to the service fields).
    case ObjectType::SnootyStaff: WriteStaff(reinterpret_cast<const object::ServiceStaff&>(o), writer, out); break;
    case ObjectType::Lifeguard: WriteStaff(static_cast<const object::Lifeguard&>(o), writer, out); break;
    case ObjectType::Mechanic: WriteStaff(static_cast<const object::Mechanic&>(o), writer, out); break;
    case ObjectType::Rep: WriteStaff(static_cast<const object::Rep&>(o), writer, out); break;
    case ObjectType::Builder: WriteStaff(static_cast<const object::Builder&>(o), writer, out); break;
    case ObjectType::Cleaner: WriteStaff(static_cast<const object::Cleaner&>(o), writer, out); break;
    case ObjectType::SecurityGuard: WriteStaff(static_cast<const object::SecurityGuard&>(o), writer, out); break;
    default: break;
    }
    return error.empty();
}

} // namespace obl::world_save
