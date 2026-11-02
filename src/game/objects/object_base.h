#pragma once

#include "core/char16.h"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace obl::layout {
struct SerialStream;
} // namespace obl::layout
namespace obl::script {
struct ScriptValue;
}
namespace obl::messages {
struct Message;
}

// Shared world object state and behavior (docs/architecture/objects.md). Derived classes extend object::Base.
// Serialization in entity_lifecycle.h and world snapshots in world_snapshot_objects.cpp visit the same field table.
namespace obl::object {

// Object type numbers of the object manager's CreateObject(name, type).
enum class ObjectType : uint8_t {
    Scenery = 1,
    Building = 2,
    Guest = 3,
    Vehicle = 4,
    Table = 5,
    ServiceStaff = 6,
    Lifeguard = 7,
    Mechanic = 8,
    Rep = 9,
    Builder = 10,
    Cleaner = 11,
    SecurityGuard = 12,
    Shark = 13,
    Cabaret = 14,
    Singer = 15,
    WeightLifter = 16,
    Catwalk = 17,
    WetTShirt = 18,
    SnootyStaff = 19,
    Aerobic = 20,
};

constexpr int kNameSize = 32; // name buffer, terminator included

// A world position (x east, y south, z height). Indexed and passed as an array by the readers that predate it.
struct Vec3 {
    float m_xyz[3];

    float& x() { return m_xyz[0]; }
    float& y() { return m_xyz[1]; }
    float& z() { return m_xyz[2]; }
    float x() const { return m_xyz[0]; }
    float y() const { return m_xyz[1]; }
    float z() const { return m_xyz[2]; }
    // A template, so any integer index is an exact match and wins over the built-in subscript through the pointer conversion.
    template<std::integral I> float& operator[](I i) { return m_xyz[i]; }
    template<std::integral I> const float& operator[](I i) const { return m_xyz[i]; }
    float* begin() { return m_xyz; }
    float* end() { return m_xyz + 3; }
    const float* begin() const { return m_xyz; }
    const float* end() const { return m_xyz + 3; }
    // As the `float[3]` it replaced: readers take `float (&)[3]` or, through the array's decay, `float*`.
    using Array = float[3];
    operator Array&() { return m_xyz; }
    operator const Array&() const { return m_xyz; }
};

// Every game object derives from Base; the object manager makes them with `new T(name)` and deletes them through the virtual
// destructor, so each class's constructor and destructor handle its own part (base first, teardown derived first).
struct Base {
    // A zeroed object (unit-test fixtures); the game names every object (entity_lifecycle.cpp).
    Base() = default;
    explicit Base(const char* name);
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;
    // The game defines the virtual methods in src/game/objects/object_virtuals.cpp; unit tests provide a trap seam (the destructors
    // there do nothing, so a fixture can go out of scope).
    virtual ~Base();
    virtual bool HasState(uint32_t state);
    virtual bool OnEnterState(uint32_t state);
    virtual bool Update();
    virtual bool OnExitState();
    virtual bool ReceiveMessage(messages::Message* message);
    virtual void GetVariableText(const char* key, Char16* out);
    virtual void SetVariable(const char* key, const void* value);
    virtual void Send(messages::Message message);
    virtual void SendDelayed(messages::Message message, float delay);
    virtual float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish);
    virtual script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data);
    virtual void SetValue(int key, intptr_t data, int type, int bits);
    virtual void CallFunction(int id);
    virtual void SetPosition(const float* xyz);
    virtual void Draw();
    virtual void DrawOverlay();
    virtual int GetOverlayOffset();
    virtual void OnSelect(uint32_t clicked, uint32_t centerCamera);
    virtual void ReduceSelectionToHover();
    virtual void ClearSelection();
    virtual bool IsSelectable();
    virtual const char* GetName();

    // The plain setters and getters every object shares (entity_accessors.cpp). The getters fill `out` and return it.
    void SetType(uint8_t type);
    void SetId(uint32_t id);
    void SetState(uint32_t state);
    void SetWorldPosition(float x, float y, float z);
    float* GetPosition(float* out) const;
    int32_t* GetTilePosition(int32_t* out) const;
    // The messages every object answers the same way (state changes, owner); the classes fall back to it (mover_receive.cpp).
    bool ReceiveCommonMessage(messages::Message* message);

    uint8_t m_waitingForDeath = 0;
    uint8_t m_requiresAttention = 0;
    // The name in place, with its terminator. An array, not a wrapper: it decays to `char*`
    // everywhere, through C varargs (FatalError's "%s") too, where a class would be pushed by value.
    char m_Name[kNameSize] = {};
    uint32_t m_id = 0;
    ObjectType m_type = {};
    uint8_t m_owner = 0;
    Vec3 m_WorldXYZ = {};
    int m_state = 0;
    uint8_t m_visible = 0;
    void* m_sprite = nullptr; // Sprite*, its keyline byte is read directly
    int m_XDrawOffset = 0;
    int m_YDrawOffset = 0;
    uint8_t m_debugBreak = 0;
    int m_SelectLevel = 0;
};

// The names of the position's three records.
constexpr const char* kPositionNames[3] = {"base.position.x", "base.position.y", "base.position.z"};

// The leading records of the base object's Save, which the world snapshot also hashes (the rest of the save is visibility, draw offsets, sprite and
// selection). `Self` is `Base` or `const Base`. The visitor's record kinds: Bool (uint8_t), Name (the char[32] name, a String record), Id (uint32_t,
// an Int record), Type (ObjectType, a Byte record), Byte (uint8_t), Position (Vec3, three Float records under kPositionNames), Int (int).
template<class Self, class Visitor> void VisitHead(Self& o, Visitor& v)
{
    v.Bool("base.waiting_for_death", o.m_waitingForDeath);
    v.Bool("base.requires_attention", o.m_requiresAttention);
    v.Name("base.name", o.m_Name);
    v.Id("base.id", o.m_id);
    v.Type("base.type", o.m_type);
    v.Byte("base.owner", o.m_owner);
    v.Position(o.m_WorldXYZ);
    v.Int("base.state", o.m_state);
}

} // namespace obl::object
