#pragma once

#include "core/char16.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_fields.h"

#include <cstdint>

namespace obl::script {
struct ScriptProgram;
}

// Shark (type 13; docs/architecture/objects.md): the shark of the Sharks disaster, a Mover driven by the object script
// "SharkLogic" (C++ class, docs/architecture/objects.md). The logic is shark.h; the fields the constructor leaves unset
// (they keep the zero of their member initializers) are noted.
namespace obl::object {

struct Shark : object::Mover {
    Shark() = default;
    // Builds the shark of the given kind (shark.cpp).
    explicit Shark(const char* name);
    // The slots whose function differs from Mover's (shark.cpp).
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    float PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void CallFunction(int id) override;
    void Draw() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    bool IsSelectable() override;
    // The area it swims in and the owner word, set by the world's creator.
    void SetBounds(int32_t left, int32_t top, int32_t right, int32_t bottom);
    void SetInitParam(int32_t value);

    messages::Message* m_curMessage = nullptr; // the message being received, valid inside ReceiveMessage only (MessData)
    int32_t m_alertRange = 5; // tiles around the shark whose swimmers are warned
    int32_t m_sniffRange = 10; // tiles around the shark in which it looks for a victim
    int32_t m_SwimCircleRect[4] = {}; // left, top, right, bottom of the area it swims in (SetBounds); left unset
    uint32_t m_actionState = 0; // 0..3, the quarter of the rectangle the next swim target lies in; left unset
    int32_t m_myVictim = -1; // object id of the swimmer being attacked, -1 = none
    script::ScriptProgram* m_script = nullptr; // "SharkLogic"
    int32_t m_guestsEaten = 0;
    int32_t m_ownerId = 0; // SetInitParam; left unset
};

// The object manager's constructor of the type: builds a shark in `memory` (value-initialized).

// Shark save records, then Mover's (the caller). `Self` is `Shark` or `const Shark`.
template<SelfOf<Shark> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Int("shark.alert_range", o.m_alertRange, Hashed::No);
    v.Int("shark.sniff_range", o.m_sniffRange, Hashed::No);
    for (auto& side : o.m_SwimCircleRect) v.UInt("shark.swim_rect[]", side, Hashed::No);
    v.Int("shark.action_state", o.m_actionState, Hashed::No);
    v.Int("shark.victim", o.m_myVictim, Hashed::No);
    v.Int("shark.guests_eaten", o.m_guestsEaten, Hashed::No);
    v.Int("shark.owner", o.m_ownerId, Hashed::Yes);
}

} // namespace obl::object
