#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_fields.h"

#include <cstdint>

// Show performers (docs/architecture/objects.md): Aerobic (type 20), Cabaret (14),
// Catwalk (17), Singer (15), WeightLifter (16) and WetTShirt (18). All six run the human base constructor but never use its three
// guest/staff words, so they derive from Mover. The logic is performer_*.h.
namespace obl::object {

// The microphone of a singer exists from the constructor and is only filled by Load; a weightlifter's weights are created by
// name on Load.
enum class AttachedMode { Fill, Create };

struct Aerobic : object::Mover {
    Aerobic() = default;
    // Builds the performer of the given kind (performer_virtuals.cpp).
    explicit Aerobic(const char* name);
    // The slots whose function differs from Mover's (game/performer_*.cpp).
    ~Aerobic() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;

    int32_t m_actionState = 0;
    uint32_t m_variant = 0; // variant: 0 = "Gail", otherwise "Shaz"
    int32_t m_OwnerID = 0; // building the performer belongs to (AttachToBuilding)
    float m_ActionPoint[3] = {};
    uint8_t m_finish = 0;
};

// No action point; the constructor sets Mover::m_maxForce 20 and m_maxVel 60.
struct Cabaret : object::Mover {
    Cabaret() = default;
    // Builds the performer of the given kind (performer_virtuals.cpp).
    explicit Cabaret(const char* name);
    // The slots whose function differs from Mover's (game/performer_*.cpp).
    ~Cabaret() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;

    int32_t m_actionState = 0;
    uint32_t m_variant = 0; // 0 = GAIL, 1 = SHAZ, 2 = FAT_BLOKE, 3 = FIT_BLOKE
    int32_t m_OwnerID = 0;
    uint8_t m_finish = 0;
};

// The constructor makes the "Gail" sprite; AttachToBuilding hides the model (Entity::m_visible) for building rotations 1 and 2.
struct Catwalk : object::Mover {
    Catwalk() = default;
    // Builds the performer of the given kind (performer_virtuals.cpp).
    explicit Catwalk(const char* name);
    // The slots whose function differs from Mover's (game/performer_*.cpp).
    ~Catwalk() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;

    int32_t m_actionState = 0;
    uint32_t m_variant = 0;
    int32_t m_OwnerID = 0;
    uint8_t m_finished = 0;
    float m_ActionPoint[3] = {};
};

// Construct creates the stage microphone sprite with SINGMIC_STILL; AttachToBuilding turns it towards the audience.
struct Singer : object::Mover {
    Singer() = default;
    // Builds the performer of the given kind (performer_virtuals.cpp).
    explicit Singer(const char* name);
    // The slots whose function differs from Mover's (game/performer_*.cpp).
    ~Singer() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void Draw() override;

    int32_t m_actionState = 0;
    uint32_t m_variant = 0;
    int32_t m_OwnerID = 0;
    void* m_attachedSprite = nullptr; // microphone SpriteInstance
    float m_ActionPoint[3] = {};
    uint8_t m_finish = 0;
};

// The weights sprite is null until a Load (which creates it by name) or the script attaches one; OnExitState(4) releases it.
struct WeightLifter : object::Mover {
    WeightLifter() = default;
    // Builds the performer of the given kind (performer_virtuals.cpp).
    explicit WeightLifter(const char* name);
    // The slots whose function differs from Mover's (game/performer_*.cpp).
    ~WeightLifter() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void Draw() override;

    int32_t m_actionState = 0;
    uint32_t m_variant = 0;
    int32_t m_OwnerID = 0;
    void* m_attachedSprite = nullptr;
    uint8_t m_finished = 0;
    float m_ActionPoint[3] = {};
};

struct WetTShirt : object::Mover {
    WetTShirt() = default;
    // Builds the performer of the given kind (performer_virtuals.cpp).
    explicit WetTShirt(const char* name);
    // The slots whose function differs from Mover's (game/performer_*.cpp).
    ~WetTShirt() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;

    int32_t m_actionState = 0;
    int32_t m_Contestent = 0; // the contestant guest (object id)
    uint32_t m_variant = 0; // 3 = "wtsc", otherwise "wts"
    int32_t m_OwnerID = 0;
    uint8_t m_finished = 0;
    float m_ActionPoint[3] = {};
};

// The six classes' own records in the save order (docs/architecture/objects.md, "Save/Load"), then Person's (the caller). None is
// hashed. Visitor: Int/UInt/Float/Bool(key, field, hashed), Attached(key, void*& sprite, AttachedMode) (Save: flag, "Sprite
// name" and the sprite's records of a non-null pointer). A slip kept on purpose (saves depend on it): `m_ActionPoint.z` is written from, and read
// into, the y word, so a Load leaves z untouched.
namespace detail {
template<class Self, class Visitor> void PerformerHeader(Self& o, Visitor& v)
{
    v.Int("performer.action_state", o.m_actionState, Hashed::No);
    v.UInt("performer.performer_type", o.m_variant, Hashed::No);
    v.Int("performer.owner", o.m_OwnerID, Hashed::No);
}
template<class Point, class Visitor> void ActionPoint(Point& point, Visitor& v)
{
    v.Float("performer.action_point.x", point[0], Hashed::No);
    v.Float("performer.action_point.y", point[1], Hashed::No);
    v.Float("performer.action_point.z", point[1], Hashed::No);
}
} // namespace detail

template<SelfOf<Aerobic> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    detail::PerformerHeader(o, v);
    v.Bool("performer.finished", o.m_finish, Hashed::No);
    detail::ActionPoint(o.m_ActionPoint, v);
}

template<SelfOf<Cabaret> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    detail::PerformerHeader(o, v);
    v.Bool("performer.finished", o.m_finish, Hashed::No);
}

template<SelfOf<Catwalk> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    detail::PerformerHeader(o, v);
    v.Bool("performer.finished", o.m_finished, Hashed::No);
    detail::ActionPoint(o.m_ActionPoint, v);
}

template<SelfOf<Singer> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    detail::PerformerHeader(o, v);
    v.Bool("performer.finished", o.m_finish, Hashed::No);
    detail::ActionPoint(o.m_ActionPoint, v);
    v.Attached("performer.attached", o.m_attachedSprite, AttachedMode::Fill);
}

template<SelfOf<WeightLifter> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    detail::PerformerHeader(o, v);
    v.Bool("performer.finished", o.m_finished, Hashed::No);
    detail::ActionPoint(o.m_ActionPoint, v);
    v.Attached("performer.attached", o.m_attachedSprite, AttachedMode::Create);
}

// The contestant comes after the flag here.
template<SelfOf<WetTShirt> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    detail::PerformerHeader(o, v);
    v.Bool("performer.finished", o.m_finished, Hashed::No);
    v.Int("performer.contestant", o.m_Contestent, Hashed::No);
    detail::ActionPoint(o.m_ActionPoint, v);
}

} // namespace obl::object
