#include "game/world/save_objects_load.h"

#include "game/map/path_data.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_queries.h"
#include "game/objects/object_manager_serial.h"
#include "game/objects/object_building.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/vehicle_serial.h"
#include "game/objects/performer_serial.h"
#include "game/objects/object_static.h"
#include "game/objects/furniture.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_staff.h"
#include "game/ui/sprite.h"
#include "game/world/save_guests_load.h"
#include "game/world/save_json_values.h"
#include "formats/save_states.h"
#include "core/code_page.h"
#include "core/content_key.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace obl::world_save {
namespace {

using nlohmann::json;
using object::Hashed;
using object::ObjectType;

constexpr int kSpriteNameSize = 0x80; // the base object's sprite name buffer (entity_lifecycle.h)

// The JSON visitor over the classes' VisitFields, the inverse of ObjectWriter: each record's value from its key in the
// writer's forms ("a.b" nests objects, "list[]" takes the key's next occurrence, "list[].field" the field of that element).
class ObjectReader {
public:
    ObjectReader(
            const std::string& where, const json& in, const ReadContext& context, const ObjectLoadCallbacks& callbacks, std::string& error)
        : m_where(where), m_in(in), m_context(context), m_callbacks(callbacks), m_error(error)
    {}

    template<class T> void Int(const char* key, T& field, Hashed)
    {
        static_assert(sizeof(T) == 4);
        if (std::string_view(key) == "building.beer") {
            field = static_cast<T>(CatalogId(key, Find(key), "beers", m_context.beerId));
            return;
        }
        field = static_cast<T>(StateOr(key, Find(key)));
    }
    template<class T> void UInt(const char* key, T& field, Hashed)
    {
        static_assert(sizeof(T) == 4);
        field = static_cast<T>(StateOr(key, Find(key)));
    }
    void Float(const char* key, float& field, Hashed) { field = FloatValue(key, Find(key)); }
    // A bool record; the traversal flag is a byte whose old values the writer keeps as a number (IntegerBits takes both).
    void Bool(const char* key, uint8_t& field, Hashed)
    {
        const uint32_t value = Integer(key, Find(key));
        if (value > 0xff) Fail(key, "not a byte");
        field = static_cast<uint8_t>(value);
    }
    void Byte(const char* key, uint8_t& field, Hashed) { field = ByteValue(key); }
    void Ints(const char* key, int32_t* values, int count, Hashed)
    {
        const json* list = List(key, count);
        for (int i = 0; i < count; ++i) values[i] = static_cast<int32_t>(Integer(key, list ? &(*list)[static_cast<size_t>(i)] : nullptr));
    }
    void Bytes(const char* key, uint8_t* values, int count, Hashed)
    {
        const json* list = List(key, count);
        for (int i = 0; i < count; ++i) {
            const uint32_t value = Integer(key, list ? &(*list)[static_cast<size_t>(i)] : nullptr);
            if (value > 0xff) return Fail(key, "not a byte");
            values[i] = static_cast<uint8_t>(value);
        }
    }
    void Floats(const char* key, float* values, int count, Hashed)
    {
        const json* list = List(key, count);
        for (int i = 0; i < count; ++i) values[i] = FloatValue(key, list ? &(*list)[static_cast<size_t>(i)] : nullptr);
    }
    // A sprite the object made when it was constructed: its records from the key's next occurrence.
    void Sprite(const char* key, void* sprite) { SpriteFields(*static_cast<ui::SpriteInstance*>(sprite), Find(key)); }

    // A rectangle's four records under `key` (`.left`, `.top`, `.right`, `.bottom`).
    void Rect(const char* key, screen::Rect& r, Hashed)
    {
        const json* v = Find(key);
        if (v && !v->is_null() && !v->is_object()) return Fail(key, "not a rectangle");
        auto side = [&](const char* name) { return static_cast<int32_t>(Integer(key, v ? Member(*v, name) : nullptr)); };
        r.left = side("left");
        r.top = side("top");
        r.right = side("right");
        r.bottom = side("bottom");
    }

    // A performer's attached sprite: absent keeps the constructor's pointer; present, a new one by name (Create) or the
    // constructor's (Fill), then its records.
    void Attached(const char* key, void*& sprite, object::AttachedMode mode)
    {
        const json* v = Find(key);
        if (!v || v->is_null()) return;
        if (!v->is_object()) return Fail(key, "not an object");
        const std::string name = TextOf(key, Member(*v, "sprite"), kSpriteNameSize);
        if (!m_error.empty()) return;
        if (mode == object::AttachedMode::Create) sprite = m_callbacks.createSprite(name.c_str());
        if (!sprite) return Fail(key, "no sprite " + name);
        SpriteFields(*static_cast<ui::SpriteInstance*>(sprite), v);
    }

    // A staff member's attached sprite (the staff stream records, gone): present, always a new one by name, then its records.
    void Attached(const char* key, void*& sprite, Hashed) { Attached(key, sprite, object::AttachedMode::Create); }
    // Narrow text into its buffer, refused at `capacity` bytes or more (ReadString).
    void Text(const char* key, char* text, int capacity, Hashed)
    {
        const std::string narrow = Text(key, capacity);
        std::memset(text, 0, static_cast<size_t>(capacity));
        std::memcpy(text, narrow.data(), narrow.size());
    }
    // Wide text from UTF-8, refused at `capacity` units or more (ReadWString's buffer and terminator).
    void Wide(const char* key, Char16* text, int capacity, Hashed)
    {
        const json* v = Find(key);
        std::u16string wide;
        if (v && !v->is_null()) {
            if (!v->is_string()) return Fail(key, "not a text");
            wide = code_page::Utf8ToUtf16(v->get_ref<const std::string&>());
        }
        if (wide.size() >= static_cast<size_t>(capacity)) return Fail(key, "text of " + std::to_string(wide.size()) + " units");
        std::fill_n(text, capacity, Char16{});
        std::copy(wide.begin(), wide.end(), text);
    }
    // A staff class-wide count: every member's record carries it, the last one read stays (Staff LoadEnv's Counter).
    void Counter(const char* key, Hashed)
    {
        const auto value = static_cast<int32_t>(Integer(key, Find(key)));
        if (m_error.empty()) m_callbacks.staffCounter(key, value);
    }
    // One job-mask bit from its bool record (staff_lifecycle.cpp LoadEnv: bit 0 of the byte).
    void MaskBit(const char* key, uint32_t& mask, int bit, Hashed)
    {
        const uint8_t value = ByteValue(key);
        mask = (mask & ~(1u << bit)) | (static_cast<uint32_t>(value & 1) << bit);
    }
    // The lifeguard's area pointer from the index read just before (no record of its own).
    void BeachArea(const map::Area*& area, int32_t index) { area = m_callbacks.beachArea(index); }

    // The building's parts with their own records (object_building.h VisitFields).
    // The definition's animation sprites, then the extra one when the building has it, one list element each.
    void Sprites(const char* key, object::Building& b)
    {
        for (int32_t i = 0; i < b.m_definition->m_NumAnimSprites; ++i) SpriteFields(*b.m_animSprites[i], Find(key));
        if (b.m_constructionSprite) SpriteFields(*static_cast<ui::SpriteInstance*>(b.m_constructionSprite), Find(key));
    }
    // A building neither under construction nor being demolished (nor in burn limbo, read before) is burned in.
    void Construction(object::Building& b)
    {
        const uint8_t building = ByteValue("building.under_construction");
        const uint8_t demolishing = ByteValue("building.under_demolition");
        if (!building && !demolishing && !b.m_burnLimbo) b.m_burn = 1;
    }
    void Events(const char* key, object::Building& b)
    {
        const json* v = Find(key);
        if (!v || v->is_null()) return;
        if (!v->is_object()) return Fail(key, "not an object");
        BuildingEventsJson events;
        events.owner = static_cast<int32_t>(Integer("building.events.owner", Member(*v, "owner")));
        const uint32_t active = Integer("building.events.active", Member(*v, "active"));
        if (active > 0xff) return Fail("building.events.active", "not a byte");
        events.active = static_cast<uint8_t>(active);
        const json* slots = Member(*v, "slots");
        if (slots && (!slots->is_array() || slots->size() != 3)) return Fail("building.events.slots", "not a list of 3");
        for (size_t i = 0; i < 3 && slots; ++i) {
            const json& in = (*slots)[i];
            if (!in.is_object()) return Fail("building.events.slots", "not an object");
            BuildingEventsJson::Slot& slot = events.slots[i];
            slot.event = CatalogId("building.events.slots[].event", Member(in, "event"), "events", m_context.eventId);
            slot.actorCount = Integer("building.events.slots[].actor_count", Member(in, "actor_count"));
            slot.actors = IdList("building.events.slots[].actors", Member(in, "actors"));
            slot.guestCount = Integer("building.events.slots[].guest_count", Member(in, "guest_count"));
            slot.guests = IdList("building.events.slots[].guests", Member(in, "guests"));
            slot.start = FloatValue("building.events.slots[].start", Member(in, "start"));
            slot.duration = FloatValue("building.events.slots[].duration", Member(in, "duration"));
            slot.price = FloatValue("building.events.slots[].price", Member(in, "price"));
        }
        if (m_error.empty()) m_callbacks.buildingEvents(b, events);
    }
    // The thought list is cleared and refilled even when the entry has none (the building load always clears it).
    void Thoughts(const char* key, object::Building& b)
    {
        const json* v = Find(key);
        if (v && !v->is_null() && !v->is_array()) return Fail(key, "not a list");
        std::vector<BuildingThoughtJson> thoughts;
        for (size_t i = 0; v && v->is_array() && i < v->size(); ++i) {
            const json& in = (*v)[i];
            const json* text = Member(in, "text");
            if (!in.is_object() || (text && !text->is_string())) return Fail(key, "not an object with a text");
            BuildingThoughtJson& thought = thoughts.emplace_back();
            thought.object = static_cast<int32_t>(Integer("building.thoughts[].object", Member(in, "object")));
            if (text) thought.text = code_page::Utf8ToUtf16(text->get_ref<const std::string&>());
            // The stream stores the length with the terminator, so a text cannot hold one.
            if (thought.text.find(char16_t{0}) != std::u16string::npos) return Fail(key, "text with a terminator inside");
        }
        if (m_error.empty()) m_callbacks.buildingThoughts(b, thoughts);
    }
    void Queue(const char*, object::Building& b, Hashed)
    {
        const json* v = Find("building.queue");
        if (!v || v->is_null()) return;
        if (!v->is_object()) return Fail("building.queue", "not an object");
        BuildingQueueJson queue;
        queue.owner = static_cast<int32_t>(Integer("building.queue.owner", Member(*v, "owner")));
        queue.maxLength = static_cast<int32_t>(Integer("building.queue.max_length", Member(*v, "max_length")));
        for (auto [name, xy] : {std::pair{"serve_point", queue.servePoint}, std::pair{"direction", queue.direction}}) {
            const json* point = Member(*v, name);
            xy[0] = FloatValue(name, point ? Member(*point, "x") : nullptr);
            xy[1] = FloatValue(name, point ? Member(*point, "y") : nullptr);
        }
        queue.guests = IdList("building.queue.guests", Member(*v, "guests"));
        queue.commodities = IdList("building.queue.commodities", Member(*v, "commodities"));
        if (m_error.empty()) m_callbacks.buildingQueue(b, queue);
    }
    // The building's rotation: the object's `dir` (a quarter turn each) is the direction of its base sprite.
    void Rotation(object::Building& b)
    {
        const json* v = Find("dir");
        if (!v || v->is_null() || !b.m_sprite) return;
        static constexpr std::string_view kQuarterTurns[] = {"n", "e", "s", "w"};
        const auto* turn = v->is_string() ? std::find(std::begin(kQuarterTurns), std::end(kQuarterTurns), v->get_ref<const std::string&>())
                                          : std::end(kQuarterTurns);
        if (turn == std::end(kQuarterTurns)) return Fail("dir", "not n, e, s or w");
        static_cast<ui::SpriteInstance*>(b.m_sprite)->m_direction = static_cast<int32_t>(turn - std::begin(kQuarterTurns));
    }

    // The class Load chains' calls besides the fields (vehicle_serial.h Fields: Circuit, then Base).
    uint32_t CircuitPointCount(int32_t type) { return m_callbacks.circuitPointCount(type); }
    void* CircuitPoints(int32_t type) { return m_callbacks.circuitPoints(type); }
    void Circuit(object::Vehicle& o) { vehicle::RestoreCircuit(o, *this); }
    void Base(object::Mover& o)
    {
        object::VisitFields(o, *this);
        BaseLayer(o);
    }
    // The static layer: its grid size and claim flags, then the base.
    void Base(object::Prop& o)
    {
        object::VisitFields(o, *this);
        BaseLayer(o);
    }

    // A path cursor: a new route from the record, a reverse cursor on it, then the cursor's own two records. Without
    // `moving.path` the field keeps its value (null on a constructed object), as the "on path" flag 0 does.
    void Route(const char*, path::RouteCursor*& cursor, Hashed)
    {
        const json* p = Find("moving.path");
        if (!p) return;
        if (!p->is_object()) return Fail("moving.path", "not an object");
        auto route = std::make_shared<path::Route>();
        route->m_timeStamp = FloatValue("moving.path.time_stamp", Member(*p, "time_stamp"));
        route->m_referenceCount = static_cast<int32_t>(Integer("moving.path.ref_count", Member(*p, "ref_count")));
        if (const json* steps = Member(*p, "steps")) {
            if (!steps->is_array()) return Fail("moving.path.steps", "not a list");
            for (const json& step : *steps)
                route->m_steps.push_back(
                        {Integer("moving.path.steps[].x", Member(step, "x")), Integer("moving.path.steps[].y", Member(step, "y"))});
        }
        for (auto [name, xy] : {std::pair{"start", route->m_start}, std::pair{"stop", route->m_stop}}) {
            const json* point = Member(*p, name);
            xy[0] = static_cast<int32_t>(Integer(name, point ? Member(*point, "x") : nullptr));
            xy[1] = static_cast<int32_t>(Integer(name, point ? Member(*point, "y") : nullptr));
        }
        route->m_pathType = static_cast<int32_t>(StateOr("moving.path.type", Member(*p, "type")));
        route->m_valid = static_cast<uint8_t>(Integer("moving.path.valid", Member(*p, "valid")));
        auto* loaded = new path::RouteCursor(path::MakeCursor(std::move(route), true));
        loaded->m_reverse = static_cast<uint8_t>(Integer("moving.path.reverse", Member(*p, "reverse")));
        loaded->m_currentStep = static_cast<int32_t>(Integer("moving.path.current_step", Member(*p, "current_step")));
        cursor = loaded;
    }

    // The base object (entity_lifecycle.h): the head records, the position to SetPosition before the state, the draw
    // fields, the sprite (made by name when the object has none) and its records, the last two fields.
    void BaseLayer(object::Base& o)
    {
        o.m_waitingForDeath = ByteValue("base.waiting_for_death");
        o.m_requiresAttention = ByteValue("base.requires_attention");
        Name(o);
        o.m_id = Integer("id", Find("id"));
        o.m_owner = ByteValue("base.owner");
        uint32_t xyz[3] = {};
        const json* pos = Find("pos");
        if (pos && (!pos->is_array() || pos->size() != 3)) return Fail("pos", "not an [x, y, z] list");
        for (size_t i = 0; i < 3; ++i) xyz[i] = std::bit_cast<uint32_t>(FloatValue("pos", pos ? &(*pos)[i] : nullptr));
        if (m_error.empty()) m_callbacks.setPosition(o, xyz);
        o.m_state = static_cast<int>(StateOr("base.state", Find("base.state")));
        o.m_visible = ByteValue("base.visible");
        o.m_XDrawOffset = static_cast<int>(Integer("base.draw_offset_x", Find("base.draw_offset_x")));
        o.m_YDrawOffset = static_cast<int>(Integer("base.draw_offset_y", Find("base.draw_offset_y")));
        const std::string spriteName = Text("base.sprite", kSpriteNameSize);
        if (!m_error.empty()) return;
        if (!o.m_sprite) o.m_sprite = m_callbacks.createSprite(spriteName.c_str());
        if (!o.m_sprite) return Fail("base.sprite", "no sprite " + spriteName);
        SpriteFields(*static_cast<ui::SpriteInstance*>(o.m_sprite), Find("sprite"));
        o.m_debugBreak = ByteValue("base.debug_break");
        o.m_SelectLevel = static_cast<int>(Integer("base.select_level", Find("base.select_level")));
    }

private:
    // A record that may hold a named state (save_states.h); a plain integer otherwise.
    uint32_t StateOr(const char* key, const json* v)
    {
        const formats::SaveStateSet set = formats::SaveStateSetOf(key);
        if (set == formats::SaveStateSet::None || !v || !v->is_string()) return Integer(key, v);
        const auto number = formats::SaveStateNumber(set, *v, &m_context.objectStates);
        if (!number) Fail(key, "no state " + v->get<std::string>());
        return static_cast<uint32_t>(number.value_or(0));
    }

    // The sprite instance's records (the writer's SpriteFields).
    void SpriteFields(ui::SpriteInstance& sprite, const json* in)
    {
        if (in && !in->is_object()) return Fail("sprite", "not an object");
        auto value = [&](const char* name) { return Integer(name, in ? Member(*in, name) : nullptr); };
        sprite.m_startTime = FloatValue("sprite.start_time", in ? Member(*in, "start_time") : nullptr);
        sprite.m_animType = static_cast<int32_t>(value("anim_type"));
        sprite.m_fps = static_cast<int32_t>(value("fps"));
        sprite.m_animIndex = static_cast<int32_t>(value("anim"));
        sprite.m_frameIndex = static_cast<int32_t>(value("frame"));
        sprite.m_direction = static_cast<int32_t>(value("dir"));
        sprite.m_paletteIndex = static_cast<int32_t>(value("palette"));
        sprite.m_lit = static_cast<uint8_t>(value("lit"));
        sprite.m_keyline = static_cast<uint8_t>(value("keyline"));
        sprite.m_triggeredSound = static_cast<uint8_t>(value("triggered_sound"));
    }

    // m_Name is the content key: name compares fold case, and the world hash folds case and spaces (WorldSnapshot::String).
    void Name(object::Base& o)
    {
        const std::string name = Text("type", object::kNameSize);
        if (!m_error.empty()) return;
        std::memcpy(o.m_Name, name.data(), name.size());
        o.m_Name[name.size()] = 0;
    }
    // Narrow text through the pack's code page, refused at `capacity` bytes or more (ReadString's buffer and terminator).
    std::string Text(const char* key, int capacity) { return TextOf(key, Find(key), capacity); }
    std::string TextOf(const char* key, const json* v, int capacity)
    {
        std::string narrow;
        if (v && (!v->is_string() || !m_context.utf8ToNarrow(v->get_ref<const std::string&>(), narrow)))
            Fail(key, "text outside the pack's code page");
        if (narrow.size() >= static_cast<size_t>(capacity)) Fail(key, "text of " + std::to_string(narrow.size()) + " bytes");
        return m_error.empty() ? narrow : std::string();
    }
    // A catalog key back to its list position (the writer's CatalogValue in reverse): null or missing is -1.
    int32_t CatalogId(const char* key, const json* v, const char* list, const std::function<int32_t(std::string_view)>& lookup)
    {
        if (!v || v->is_null()) return -1;
        const int32_t id = v->is_string() && lookup ? lookup(v->get_ref<const std::string&>()) : -1;
        if (id < 0) Fail(key, std::string("no catalog ") + list + " entry " + v->dump());
        return id;
    }
    // A list of ids of any length (a deque's elements); missing reads empty.
    std::vector<int32_t> IdList(const char* key, const json* v)
    {
        std::vector<int32_t> ids;
        if (!v || v->is_null()) return ids;
        if (!v->is_array()) {
            Fail(key, "not a list");
            return ids;
        }
        for (const json& id : *v) ids.push_back(static_cast<int32_t>(Integer(key, &id)));
        return ids;
    }
    // The fixed-size list at `key`, null when missing (each element then reads 0).
    const json* List(const char* key, int count)
    {
        const json* list = Find(key);
        if (!list || list->is_null()) return nullptr;
        if (!list->is_array() || list->size() != static_cast<size_t>(count)) {
            Fail(key, "not a list of " + std::to_string(count));
            return nullptr;
        }
        return list;
    }
    uint8_t ByteValue(const char* key)
    {
        const uint32_t value = Integer(key, Find(key));
        if (value > 0xff) Fail(key, "not a byte");
        return static_cast<uint8_t>(value);
    }
    uint32_t Integer(const char* key, const json* v)
    {
        if (!v || v->is_null()) return 0;
        const auto bits = IntegerBits(*v);
        if (!bits) Fail(key, "not an integer");
        return bits.value_or(0);
    }
    float FloatValue(const char* key, const json* v)
    {
        if (!v || v->is_null()) return 0.0f;
        const auto bits = FloatBits(*v);
        if (!bits) Fail(key, "not a float");
        return std::bit_cast<float>(bits.value_or(0));
    }
    static const json* Member(const json& in, const char* name)
    {
        if (!in.is_object()) return nullptr;
        const auto it = in.find(name);
        return it == in.end() ? nullptr : &*it;
    }
    // The value at `key`, null when missing (the writer's Place in reverse; each call takes the key's next occurrence).
    const json* Find(std::string_view key)
    {
        const uint32_t occurrence = m_seen[std::string(key)]++;
        const json* target = &m_in;
        const size_t list = key.find("[]");
        std::string_view path = key.substr(0, list);
        auto descend = [&](std::string_view name) { target = target ? Member(*target, std::string(name).c_str()) : nullptr; };
        for (size_t dot = path.find('.'); dot != std::string_view::npos; dot = path.find('.')) {
            descend(path.substr(0, dot));
            path.remove_prefix(dot + 1);
        }
        descend(path);
        if (list == std::string_view::npos || !target) return target;
        if (!target->is_array() || occurrence >= target->size()) return nullptr;
        target = &(*target)[occurrence];
        std::string_view rest = key.substr(list + 2);
        if (rest.empty()) return target;
        rest.remove_prefix(1); // the '.'
        for (size_t dot = rest.find('.'); dot != std::string_view::npos; dot = rest.find('.')) {
            descend(rest.substr(0, dot));
            rest.remove_prefix(dot + 1);
        }
        descend(rest);
        return target;
    }
    void Fail(const char* key, const std::string& why)
    {
        if (m_error.empty()) m_error = m_where + "." + key + ": " + why;
    }

    const std::string& m_where;
    const json& m_in;
    const ReadContext& m_context;
    const ObjectLoadCallbacks& m_callbacks;
    std::string& m_error;
    std::map<std::string, uint32_t> m_seen; // occurrences of each key so far
};

// A staff class's Load: the profession's records, Staff's, then
// Person's (nothing of its own) chain to the moving layer and the base.
template<class T> void ReadStaff(T& o, ObjectReader& reader)
{
    object::VisitFields(o, reader);
    object::VisitFields(static_cast<object::Staff&>(o), reader);
    reader.Base(static_cast<object::Mover&>(o));
}

} // namespace

bool HasObjectReader(uint8_t type)
{
    switch (static_cast<ObjectType>(type)) {
    case ObjectType::Shark:
    case ObjectType::Vehicle:
    case ObjectType::Cabaret:
    case ObjectType::Singer:
    case ObjectType::WeightLifter:
    case ObjectType::Catwalk:
    case ObjectType::WetTShirt:
    case ObjectType::Aerobic:
    case ObjectType::Table:
    case ObjectType::Scenery:
    case ObjectType::ServiceStaff:
    case ObjectType::SnootyStaff:
    case ObjectType::Lifeguard:
    case ObjectType::Mechanic:
    case ObjectType::Rep:
    case ObjectType::Builder:
    case ObjectType::Cleaner:
    case ObjectType::SecurityGuard:
    case ObjectType::Building:
    case ObjectType::Guest: return true;
    default: return false;
    }
}

bool ReadObjectJson(const json& in, const ReadContext& context, const ObjectLoadCallbacks& callbacks, object::Base& o, std::string& error)
{
    error.clear();
    const std::string where = "object " + std::to_string(o.m_id);
    if (!in.is_object()) {
        error = where + ": not an object";
        return false;
    }
    ObjectReader reader(where, in, context, callbacks, error);
    switch (o.m_type) {
    case ObjectType::Shark: {
        auto& shark = static_cast<object::Shark&>(o);
        object::VisitFields(shark, reader);
        reader.Base(shark); // the moving layer: its fields, then the base
        break;
    }
    case ObjectType::Vehicle: vehicle::Fields(static_cast<object::Vehicle&>(o), reader); break;
    case ObjectType::Cabaret: performer::Fields(static_cast<object::Cabaret&>(o), reader); break;
    case ObjectType::Singer: performer::Fields(static_cast<object::Singer&>(o), reader); break;
    case ObjectType::WeightLifter: performer::Fields(static_cast<object::WeightLifter&>(o), reader); break;
    case ObjectType::Catwalk: performer::Fields(static_cast<object::Catwalk&>(o), reader); break;
    case ObjectType::WetTShirt: performer::Fields(static_cast<object::WetTShirt&>(o), reader); break;
    case ObjectType::Aerobic: performer::Fields(static_cast<object::Aerobic&>(o), reader); break;
    case ObjectType::Table: table::Fields(static_cast<object::Furniture&>(o), reader); break;
    case ObjectType::Scenery: {
        // A scenery object (scenery_record.h): the fields, the draw routine from the constructed sprite, the static layer, the footprint.
        auto& scenery = static_cast<object::Scenery&>(o);
        object::VisitFields(scenery, reader);
        if (!error.empty()) break;
        callbacks.sceneryDraw(scenery);
        reader.Base(scenery);
        if (error.empty()) callbacks.sceneryFootprint(scenery);
        break;
    }
    case ObjectType::ServiceStaff: ReadStaff(static_cast<object::ServiceStaff&>(o), reader); break;
    // The restaurant waiter (SnootyStaff) shares ServiceStaff's records (the writer's cast).
    case ObjectType::SnootyStaff: ReadStaff(reinterpret_cast<object::ServiceStaff&>(o), reader); break;
    case ObjectType::Lifeguard: ReadStaff(static_cast<object::Lifeguard&>(o), reader); break;
    case ObjectType::Mechanic: ReadStaff(static_cast<object::Mechanic&>(o), reader); break;
    case ObjectType::Rep: ReadStaff(static_cast<object::Rep&>(o), reader); break;
    case ObjectType::Builder: ReadStaff(static_cast<object::Builder&>(o), reader); break;
    case ObjectType::Cleaner: ReadStaff(static_cast<object::Cleaner&>(o), reader); break;
    case ObjectType::SecurityGuard: ReadStaff(static_cast<object::SecurityGuard&>(o), reader); break;
    case ObjectType::Building: {
        // A building (object_building.h VisitFields): the fields with their parts, the static layer, then the rotation the
        // writer moved out of the sprite.
        auto& building = static_cast<object::Building&>(o);
        object::VisitFields(building, reader);
        reader.Base(static_cast<object::Prop&>(building));
        reader.Rotation(building);
        break;
    }
    case ObjectType::Guest: guest_load::ReadGuest(static_cast<object::Guest&>(o), reader, in, context, callbacks, where, error); break;
    default: error = where + ": no reader for object type " + std::to_string(static_cast<int>(o.m_type)); break;
    }
    return error.empty();
}

bool ReadObjectHeaderJson(const json& in, const ReadContext& context, uint8_t& type, std::string& name, std::string& error)
{
    error.clear();
    type = 0;
    name.clear();
    if (!in.is_object() || !in.contains("id") || !in["id"].is_number_unsigned()) {
        error = "objects: an entry without an id";
        return false;
    }
    const std::string where = "object " + std::to_string(in["id"].get<uint32_t>());
    const auto text = [&](const char* key, std::string fallback) {
        const auto it = in.find(key);
        return it != in.end() && it->is_string() ? it->get<std::string>() : fallback;
    };
    // The kind, and for staff and performers the `class` that says which of them.
    struct KindType {
        std::string_view name;
        ObjectType type;
    };
    static constexpr KindType kKinds[] = {{"scenery", ObjectType::Scenery}, {"building", ObjectType::Building},
            {"guest", ObjectType::Guest}, {"vehicle", ObjectType::Vehicle}, {"table", ObjectType::Table}, {"animal", ObjectType::Shark}};
    static constexpr KindType kClasses[] = {{"service", ObjectType::ServiceStaff}, {"waiter", ObjectType::SnootyStaff},
            {"lifeguard", ObjectType::Lifeguard}, {"mechanic", ObjectType::Mechanic}, {"rep", ObjectType::Rep},
            {"builder", ObjectType::Builder}, {"cleaner", ObjectType::Cleaner}, {"security", ObjectType::SecurityGuard},
            {"cabaret", ObjectType::Cabaret}, {"singer", ObjectType::Singer}, {"weightlifter", ObjectType::WeightLifter},
            {"catwalk", ObjectType::Catwalk}, {"wettshirt", ObjectType::WetTShirt}, {"aerobic", ObjectType::Aerobic}};
    const std::string kind = text("kind", "");
    const std::string key = text("type", "");
    const std::string objectClass = text("class", "");
    for (const auto& [kindName, value] : kKinds)
        if (kindName == kind) type = static_cast<uint8_t>(value);
    if (kind == "staff" || kind == "performer")
        for (const auto& [className, value] : kClasses)
            if (className == objectClass) type = static_cast<uint8_t>(value);
    if (!type) {
        error = where + ": no object type for kind '" + kind + "'";
        return false;
    }
    // The object manager's "Object Name" record: the creation key when it differs from `type`.
    if (!context.utf8ToNarrow(text("created_as", key), name)) {
        error = where + ".created_as: a character outside the code page";
        return false;
    }
    return true;
}

bool ReadObjectManagerJson(
        const json& section, const json& objects, const ReadContext& context, object_manager::SavedHeader& out, std::string& error)
{
    error.clear();
    out = {};
    if (!section.is_object() || !objects.is_array()) {
        error = "managers.objects: not an object";
        return false;
    }
    out.ids.assign(object::kSavedIdCount, 0xFFFFFFFFu); // an unused slot is stored as -1
    constexpr uint32_t kBases[] = {0, object_manager::kGuestBase, object_manager::kOtherBase};
    constexpr uint32_t kCapacities[] = {object::Manager::kMaxBuildings, object::Manager::kMaxGuests, object::Manager::kMaxOthers};
    uint32_t* counts[] = {&out.buildings, &out.guests, &out.others};
    uint32_t largest = 0;
    int lastGroup = 0;
    for (const json& entry : objects) {
        uint8_t type = 0;
        std::string name;
        if (!ReadObjectHeaderJson(entry, context, type, name, error)) return false;
        const uint32_t id = entry["id"].get<uint32_t>();
        const int group = type == 2 ? 0 : type == 3 ? 1 : 2;
        // The stream order the object manager load walks (the n-th object is objects[n]) and the capacity of each list.
        if (group < lastGroup || *counts[group] == kCapacities[group]) {
            error = "object " + std::to_string(id) +
                    (group < lastGroup ? ": not in the order buildings, guests, others" : ": its list is full");
            return false;
        }
        lastGroup = group;
        out.ids[kBases[group] + (*counts[group])++] = id;
        largest = std::max(largest, id + 1);
    }
    out.active = out.buildings + out.guests + out.others;
    const auto next = section.find("next_id");
    if (next == section.end())
        out.nextId = largest; // a document without it: one past the largest id, as the bridge rebuilt it
    else if (next->is_number_unsigned() && next->get<uint64_t>() <= 0xFFFFFFFFu)
        out.nextId = next->get<uint32_t>();
    else {
        error = "managers.objects.next_id: not a 32-bit unsigned number";
        return false;
    }
    const auto keyline = section.find("keyline");
    if (keyline != section.end() && !keyline->is_boolean()) {
        error = "managers.objects.keyline: not a bool";
        return false;
    }
    out.keyline = keyline != section.end() && keyline->get<bool>() ? 1 : 0;
    return true;
}

} // namespace obl::world_save
