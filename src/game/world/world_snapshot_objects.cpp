#include "game/objects/building_queue_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_table.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_moving.h"
#include "game/map/path_data.h"
#include "core/char16.h"
#include "game/world/world_snapshot_model.h"

#include <iterator>

// Object snapshots start with the manager header, followed by buildings, guests and other objects. Derived records precede base
// records so the stream has a stable order. Each class emits only the fields selected by kObjectFields, using their save record types.
namespace obl::world_snapshot {
namespace {

using object::ObjectType;

// Emit the shared object identity and position through object::VisitHead, using the save record types.
struct HeadEmitter {
    WorldSnapshot& s;
    void Bool(const char* name, uint8_t value) { s.Bool(name, value); }
    void Name(const char* name, const char* value) { s.String(name, value); }
    void Id(const char* name, uint32_t value) { s.Int(name, static_cast<int32_t>(value)); }
    void Type(const char* name, ObjectType value) { s.Byte(name, static_cast<uint8_t>(value)); }
    void Byte(const char* name, uint8_t value) { s.Byte(name, value); }
    void Position(const object::Vec3& value)
    {
        for (int i = 0; i < 3; ++i) s.Float(object::kPositionNames[i], value[i]);
    }
    void Int(const char* name, int value) { s.Int(name, value); }
};

void EmitRoute(WorldSnapshot& s, const path::RouteCursor& cursor);
void EmitQueue(WorldSnapshot& s, const object::BuildingQueue& q);

// Field tables select the object state that contributes to the checksum and specify its record types.
struct ClassEmitter {
    WorldSnapshot& s;
    template<class T> void Int(const char* key, const T& value, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.Int(key, static_cast<int32_t>(value));
    }
    template<class T> void UInt(const char* key, const T& value, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.UInt(key, static_cast<uint32_t>(value));
    }
    void Float(const char* key, float value, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.Float(key, value);
    }
    void Bool(const char* key, uint8_t value, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.Bool(key, value);
    }
    void Byte(const char* key, uint8_t value, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.Byte(key, value);
    }
    void Ints(const char* key, const int32_t* values, int count, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.Ints(key, values, static_cast<size_t>(count));
    }
    void Bytes(const char* key, const uint8_t* values, int count, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.Bytes(key, values, static_cast<size_t>(count));
    }
    void Floats(const char* key, const float* values, int count, object::Hashed hashed)
    {
        if (hashed == object::Hashed::Yes) s.Floats(key, values, static_cast<size_t>(count));
    }
    void Rect(const char* key, const screen::Rect& r, object::Hashed hashed)
    {
        if (hashed == object::Hashed::No) return;
        for (int32_t v : {r.left, r.top, r.right, r.bottom}) s.UInt(key, static_cast<uint32_t>(v));
    }
    void Sprite(const char*, const void*) {} // no sprite record is hashed
    // The building's parts with their own records: only the queue is hashed.
    void Sprites(const char*, const object::Building&) {}
    void Construction(const object::Building&) {}
    void Events(const char*, const object::Building&) {}
    void Thoughts(const char*, const object::Building&) {}
    void Queue(const char* key, const object::Building& b, object::Hashed hashed)
    {
        if (hashed == object::Hashed::No) return;
        const auto* queue = static_cast<const object::BuildingQueue*>(b.m_queue);
        s.Bool(key, queue ? 1 : 0);
        if (queue) EmitQueue(s, *queue);
    }
    void Wide(const char*, const Char16*, int, object::Hashed) {} // no text record is hashed
    // The professions' class counters, mask bits, texts, beach area and attached sprites have no hashed records.
    void Counter(const char*, object::Hashed) {}
    void MaskBit(const char*, uint32_t, int, object::Hashed) {}
    void Text(const char*, const char*, int, object::Hashed) {}
    template<class Area> void BeachArea(const Area&, int32_t) {}
    void Attached(const char*, const void*, object::Hashed) {}
    void Name(const char*, const Char16*, object::Hashed) {} // the guest's heap name is not hashed
    void Route(const char* key, const path::RouteCursor* cursor, object::Hashed hashed)
    {
        if (hashed == object::Hashed::No) return;
        s.UInt(key, cursor ? 1 : 0);
        if (cursor) EmitRoute(s, *cursor);
    }
};

void EmitBase(WorldSnapshot& s, const object::Base& o)
{
    HeadEmitter emitter{s};
    object::VisitHead(o, emitter);
}

// Prop's records, then the base's.
void EmitStatic(WorldSnapshot& s, const object::Prop& o)
{
    ClassEmitter emitter{s};
    object::VisitFields(o, emitter);
    EmitBase(s, o);
}

// The route of a moving object: its shared steps, then the cursor's place in them. The integers are UInt records.
void EmitRoute(WorldSnapshot& s, const path::RouteCursor& cursor)
{
    const path::Route& d = *cursor.m_route;
    s.Float("moving.path.time_stamp", d.m_timeStamp);
    s.UInt("moving.path.steps", static_cast<uint32_t>(d.m_steps.size()));
    for (const auto& step : d.m_steps) {
        s.UInt("moving.path.steps[].x", step.x);
        s.UInt("moving.path.steps[].y", step.y);
    }
    s.UInt("moving.path.start.x", static_cast<uint32_t>(d.m_start[0]));
    s.UInt("moving.path.start.y", static_cast<uint32_t>(d.m_start[1]));
    s.UInt("moving.path.stop.x", static_cast<uint32_t>(d.m_stop[0]));
    s.UInt("moving.path.stop.y", static_cast<uint32_t>(d.m_stop[1]));
    s.UInt("moving.path.type", static_cast<uint32_t>(d.m_pathType));
    s.Bool("moving.path.valid", d.m_valid);
    s.Bool("moving.path.reverse", cursor.m_reverse);
    s.UInt("moving.path.current_step", static_cast<uint32_t>(cursor.m_currentStep));
}

void EmitMoving(WorldSnapshot& s, const object::Mover& o)
{
    ClassEmitter emitter{s};
    object::VisitFields(o, emitter);
    EmitBase(s, o);
}

void EmitStaff(WorldSnapshot& s, const object::Staff& o)
{
    ClassEmitter emitter{s};
    object::VisitFields(o, emitter);
    EmitMoving(s, o); // Person has no records of its own
}

// Profession records precede the shared staff state to keep object record order stable.
template<class Profession> void EmitProfession(WorldSnapshot& s, const Profession& o)
{
    ClassEmitter emitter{s};
    object::VisitFields(o, emitter);
    EmitStaff(s, o);
}

// Guest records precede movement state; the human base contributes no additional checksum fields.
void EmitGuest(WorldSnapshot& s, const object::Guest& o)
{
    ClassEmitter emitter{s};
    object::VisitFields(o, emitter);
    EmitMoving(s, o);
}

void EmitQueue(WorldSnapshot& s, const object::BuildingQueue& q)
{
    s.Int("building.queue.owner", static_cast<int32_t>(q.m_ownerId));
    s.Int("building.queue.max_length", static_cast<int32_t>(q.m_maxLength));
    const size_t guests = q.m_qlQueue.size();
    s.UInt("building.queue.guests", static_cast<uint32_t>(guests));
    for (size_t i = 0; i < guests; ++i) s.Int("building.queue.guests[]", q.m_qlQueue[i]);
    const size_t goods = q.m_qlCommmod.size();
    s.UInt("building.queue.commodities", static_cast<uint32_t>(goods));
    for (size_t i = 0; i < goods; ++i) s.Int("building.queue.commodities[]", q.m_qlCommmod[i]);
}

// Normalize cuts the staff arrays to the active prefix (DismissStaff compacts them).
void EmitBuilding(WorldSnapshot& s, const object::Building& b)
{
    ClassEmitter emitter{s};
    object::VisitFields(b, emitter);
    EmitStatic(s, b);
}

void EmitVehicle(WorldSnapshot& s, const object::Vehicle& v)
{
    ClassEmitter emitter{s};
    object::VisitFields(v, emitter);
    EmitMoving(s, v);
}

void EmitObject(WorldSnapshot& s, const object::Base& o)
{
    s.Byte("object.type", static_cast<uint8_t>(o.m_type));
    // The virtual name: scenery answers with its definition's name, every other class with m_Name.
    const bool isScenery = o.m_type == ObjectType::Scenery;
    s.String("object.name", isScenery ? static_cast<const object::Scenery&>(o).m_ObjectName : o.m_Name);
    switch (o.m_type) {
    case ObjectType::Scenery: {
        const auto& scenery = static_cast<const object::Scenery&>(o);
        ClassEmitter emitter{s};
        object::VisitFields(scenery, emitter);
        EmitStatic(s, scenery);
        break;
    }
    case ObjectType::Building: EmitBuilding(s, static_cast<const object::Building&>(o)); break;
    case ObjectType::Guest: EmitGuest(s, static_cast<const object::Guest&>(o)); break;
    case ObjectType::Vehicle: EmitVehicle(s, static_cast<const object::Vehicle&>(o)); break;
    case ObjectType::Table: {
        const auto& table = static_cast<const object::Furniture&>(o);
        ClassEmitter emitter{s};
        object::VisitFields(table, emitter);
        EmitStatic(s, table);
        break;
    }
    case ObjectType::Shark: {
        const auto& shark = static_cast<const object::Shark&>(o);
        ClassEmitter emitter{s};
        object::VisitFields(shark, emitter);
        EmitMoving(s, shark);
        break;
    }
    // Staff: the profession's hashed records, then Staff's. ServiceStaff and SnootyStaff have none of their own in the snapshot, nor
    // has the mechanic (its action state is left out: only the script writes it, so a fresh mechanic holds heap garbage).
    case ObjectType::Builder: EmitProfession(s, static_cast<const object::Builder&>(o)); break;
    case ObjectType::Cleaner: EmitProfession(s, static_cast<const object::Cleaner&>(o)); break;
    case ObjectType::Lifeguard: EmitProfession(s, static_cast<const object::Lifeguard&>(o)); break;
    case ObjectType::Mechanic: EmitProfession(s, static_cast<const object::Mechanic&>(o)); break;
    case ObjectType::Rep: EmitProfession(s, static_cast<const object::Rep&>(o)); break;
    case ObjectType::SecurityGuard: EmitProfession(s, static_cast<const object::SecurityGuard&>(o)); break;
    case ObjectType::ServiceStaff:
    case ObjectType::SnootyStaff: EmitStaff(s, static_cast<const object::Staff&>(o)); break;
    // The performers' own records are not part of the snapshot; their base is Person.
    case ObjectType::Cabaret:
    case ObjectType::Singer:
    case ObjectType::WeightLifter:
    case ObjectType::Catwalk:
    case ObjectType::WetTShirt:
    case ObjectType::Aerobic: EmitMoving(s, static_cast<const object::Mover&>(o)); break;
    default: EmitBase(s, o); break;
    }
}

} // namespace

void EmitObjects(WorldSnapshot& s, const object::Manager& m)
{
    s.Class("ObjectManager");
    s.Int("active_objects", static_cast<int32_t>(m.ActiveCount()));
    s.Int("active_buildings", static_cast<int32_t>(m.m_buildings.size()));
    s.Int("active_guests", static_cast<int32_t>(m.m_guests.size()));
    s.Int("active_others", static_cast<int32_t>(m.m_others.size()));
    const std::vector<uint32_t> ids = m.SavedIds();
    s.Ints("object_ids", ids.data(), ids.size());
    s.Int("next_id", static_cast<int32_t>(m.m_nextFreeObjectId));
    for (uint32_t i = 0; i < m.m_buildings.size(); ++i) EmitObject(s, *m.m_buildings[i]);
    for (uint32_t i = 0; i < m.m_guests.size(); ++i) EmitObject(s, *m.m_guests[i]);
    for (uint32_t i = 0; i < m.m_others.size(); ++i) EmitObject(s, *m.m_others[i]);
}

} // namespace obl::world_snapshot
