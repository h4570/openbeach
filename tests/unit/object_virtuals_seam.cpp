// The unit tests build object fixtures (so the compiler emits their vtables) but never dispatch: the game's bodies
// (src/game/objects/object_virtuals.cpp) need the slot arrays, so the tests link these traps instead.

#include "core/char16.h"
#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_performers.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_vehicle.h"

#include <cstdio>
#include <cstdlib>

namespace obl::object {

namespace {
[[noreturn]] void Trap(const char* name)
{
    std::fprintf(stderr, "unit test dispatched object virtual %s\n", name);
    std::abort();
}
} // namespace

Base::~Base() = default;
bool Base::HasState(uint32_t)
{
    Trap("HasState");
}
bool Base::OnEnterState(uint32_t)
{
    Trap("OnEnterState");
}
bool Base::Update()
{
    Trap("Update");
}
bool Base::OnExitState()
{
    Trap("OnExitState");
}
bool Base::ReceiveMessage(messages::Message*)
{
    Trap("ReceiveMessage");
}
void Base::GetVariableText(const char*, obl::Char16*)
{
    Trap("GetVariableText");
}
void Base::SetVariable(const char*, const void*)
{
    Trap("SetVariable");
}
void Base::Send(messages::Message)
{
    Trap("Send");
}
void Base::SendDelayed(messages::Message, float)
{
    Trap("SendDelayed");
}
float Base::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("PlayAnimation");
}
script::ScriptValue* Base::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("GetValue");
}
void Base::SetValue(int, intptr_t, int, int)
{
    Trap("SetValue");
}
void Base::CallFunction(int)
{
    Trap("CallFunction");
}
void Base::SetPosition(const float*)
{
    Trap("SetPosition");
}
void Base::Draw()
{
    Trap("Draw");
}
void Base::DrawOverlay()
{
    Trap("DrawOverlay");
}
int Base::GetOverlayOffset()
{
    Trap("GetOverlayOffset");
}
void Base::OnSelect(uint32_t, uint32_t)
{
    Trap("OnSelect");
}
void Base::ReduceSelectionToHover()
{
    Trap("ReduceSelectionToHover");
}
void Base::ClearSelection()
{
    Trap("ClearSelection");
}
bool Base::IsSelectable()
{
    Trap("IsSelectable");
}
const char* Base::GetName()
{
    Trap("GetName");
}
void Mover::Teleport(float, float, float)
{
    Trap("Teleport");
}

Aerobic::~Aerobic() = default;
bool Aerobic::HasState(uint32_t)
{
    Trap("AerobicObject::HasState");
}
bool Aerobic::OnEnterState(uint32_t)
{
    Trap("AerobicObject::OnEnterState");
}
bool Aerobic::OnExitState()
{
    Trap("AerobicObject::OnExitState");
}
bool Aerobic::ReceiveMessage(messages::Message*)
{
    Trap("AerobicObject::ReceiveMessage");
}
bool Aerobic::Update()
{
    Trap("AerobicObject::Update");
}
float Aerobic::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("AerobicObject::PlayAnimation");
}
script::ScriptValue* Aerobic::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("AerobicObject::GetValue");
}
void Aerobic::SetValue(int, intptr_t, int, int)
{
    Trap("AerobicObject::SetValue");
}
Cabaret::~Cabaret() = default;
bool Cabaret::OnEnterState(uint32_t)
{
    Trap("CabaretObject::OnEnterState");
}
bool Cabaret::OnExitState()
{
    Trap("CabaretObject::OnExitState");
}
bool Cabaret::ReceiveMessage(messages::Message*)
{
    Trap("CabaretObject::ReceiveMessage");
}
bool Cabaret::Update()
{
    Trap("CabaretObject::Update");
}
float Cabaret::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("CabaretObject::PlayAnimation");
}
script::ScriptValue* Cabaret::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("CabaretObject::GetValue");
}
void Cabaret::SetValue(int, intptr_t, int, int)
{
    Trap("CabaretObject::SetValue");
}
Catwalk::~Catwalk() = default;
bool Catwalk::HasState(uint32_t)
{
    Trap("CatwalkObject::HasState");
}
bool Catwalk::OnEnterState(uint32_t)
{
    Trap("CatwalkObject::OnEnterState");
}
bool Catwalk::OnExitState()
{
    Trap("CatwalkObject::OnExitState");
}
bool Catwalk::ReceiveMessage(messages::Message*)
{
    Trap("CatwalkObject::ReceiveMessage");
}
bool Catwalk::Update()
{
    Trap("CatwalkObject::Update");
}
float Catwalk::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("CatwalkObject::PlayAnimation");
}
script::ScriptValue* Catwalk::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("CatwalkObject::GetValue");
}
void Catwalk::SetValue(int, intptr_t, int, int)
{
    Trap("CatwalkObject::SetValue");
}
bool Shark::HasState(uint32_t)
{
    Trap("Shark::HasState");
}
bool Shark::OnEnterState(uint32_t)
{
    Trap("Shark::OnEnterState");
}
bool Shark::OnExitState()
{
    Trap("Shark::OnExitState");
}
bool Shark::ReceiveMessage(messages::Message*)
{
    Trap("Shark::ReceiveMessage");
}
bool Shark::Update()
{
    Trap("Shark::Update");
}
float Shark::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("Shark::PlayAnimation");
}
script::ScriptValue* Shark::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("Shark::GetValue");
}
void Shark::CallFunction(int)
{
    Trap("Shark::CallFunction");
}
void Shark::Draw()
{
    Trap("Shark::Draw");
}
void Shark::GetVariableText(const char*, obl::Char16*)
{
    Trap("Shark::GetVariableText");
}
void Shark::SendDelayed(messages::Message, float)
{
    Trap("Shark::SendDelayed");
}
void Shark::Send(messages::Message)
{
    Trap("Shark::Send");
}
void Shark::SetValue(int, intptr_t, int, int)
{
    Trap("Shark::SetValue");
}
Singer::~Singer() = default;
bool Singer::HasState(uint32_t)
{
    Trap("SingerObject::HasState");
}
bool Singer::OnEnterState(uint32_t)
{
    Trap("SingerObject::OnEnterState");
}
bool Singer::OnExitState()
{
    Trap("SingerObject::OnExitState");
}
bool Singer::ReceiveMessage(messages::Message*)
{
    Trap("SingerObject::ReceiveMessage");
}
bool Singer::Update()
{
    Trap("SingerObject::Update");
}
float Singer::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("SingerObject::PlayAnimation");
}
script::ScriptValue* Singer::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("SingerObject::GetValue");
}
void Singer::Draw()
{
    Trap("SingerObject::Draw");
}
void Singer::SetValue(int, intptr_t, int, int)
{
    Trap("SingerObject::SetValue");
}
Vehicle::~Vehicle() = default;
bool Vehicle::OnEnterState(uint32_t)
{
    Trap("Vehicle::OnEnterState");
}
bool Vehicle::OnExitState()
{
    Trap("Vehicle::OnExitState");
}
bool Vehicle::ReceiveMessage(messages::Message*)
{
    Trap("Vehicle::ReceiveMessage");
}
bool Vehicle::Update()
{
    Trap("Vehicle::Update");
}
void Vehicle::Draw()
{
    Trap("Vehicle::Draw");
}
void Vehicle::GetVariableText(const char*, obl::Char16*)
{
    Trap("Vehicle::GetVariableText");
}
WeightLifter::~WeightLifter() = default;
bool WeightLifter::HasState(uint32_t)
{
    Trap("WeightLifterObject::HasState");
}
bool WeightLifter::OnEnterState(uint32_t)
{
    Trap("WeightLifterObject::OnEnterState");
}
bool WeightLifter::OnExitState()
{
    Trap("WeightLifterObject::OnExitState");
}
bool WeightLifter::ReceiveMessage(messages::Message*)
{
    Trap("WeightLifterObject::ReceiveMessage");
}
bool WeightLifter::Update()
{
    Trap("WeightLifterObject::Update");
}
float WeightLifter::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("WeightLifterObject::PlayAnimation");
}
script::ScriptValue* WeightLifter::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("WeightLifterObject::GetValue");
}
void WeightLifter::Draw()
{
    Trap("WeightLifterObject::Draw");
}
void WeightLifter::SetValue(int, intptr_t, int, int)
{
    Trap("WeightLifterObject::SetValue");
}
WetTShirt::~WetTShirt() = default;
bool WetTShirt::HasState(uint32_t)
{
    Trap("WetTShirtObject::HasState");
}
bool WetTShirt::OnEnterState(uint32_t)
{
    Trap("WetTShirtObject::OnEnterState");
}
bool WetTShirt::OnExitState()
{
    Trap("WetTShirtObject::OnExitState");
}
bool WetTShirt::ReceiveMessage(messages::Message*)
{
    Trap("WetTShirtObject::ReceiveMessage");
}
bool WetTShirt::Update()
{
    Trap("WetTShirtObject::Update");
}
float WetTShirt::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("WetTShirtObject::PlayAnimation");
}
script::ScriptValue* WetTShirt::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("WetTShirtObject::GetValue");
}
void WetTShirt::SetValue(int, intptr_t, int, int)
{
    Trap("WetTShirtObject::SetValue");
}
void Shark::OnSelect(uint32_t, uint32_t)
{
    Trap("Shark::OnSelect");
}
bool Shark::IsSelectable()
{
    Trap("Shark::IsSelectable");
}
bool Cabaret::HasState(uint32_t)
{
    Trap("CabaretObject::HasState");
}
void Cabaret::CallFunction(int)
{
    Trap("CabaretObject::CallFunction");
}
Mover::~Mover() = default;
bool Mover::Update()
{
    Trap("Mover::Update");
}
bool Mover::ReceiveMessage(messages::Message*)
{
    Trap("Mover::ReceiveMessage");
}
void Mover::Send(messages::Message)
{
    Trap("Mover::Send");
}
void Mover::SendDelayed(messages::Message, float)
{
    Trap("Mover::SendDelayed");
}
script::ScriptValue* Mover::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("Mover::GetValue");
}
void Mover::SetValue(int, intptr_t, int, int)
{
    Trap("Mover::SetValue");
}
bool Person::Update()
{
    Trap("Person::Update");
}
bool Person::ReceiveMessage(messages::Message*)
{
    Trap("Person::ReceiveMessage");
}
script::ScriptValue* Person::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("Person::GetValue");
}
void Person::SetValue(int, intptr_t, int, int)
{
    Trap("Person::SetValue");
}
Guest::~Guest() = default;
bool Guest::HasState(uint32_t)
{
    Trap("Guest::HasState");
}
bool Guest::OnEnterState(uint32_t)
{
    Trap("Guest::OnEnterState");
}
bool Guest::Update()
{
    Trap("Guest::Update");
}
bool Guest::OnExitState()
{
    Trap("Guest::OnExitState");
}
bool Guest::ReceiveMessage(messages::Message*)
{
    Trap("Guest::ReceiveMessage");
}
void Guest::GetVariableText(const char*, obl::Char16*)
{
    Trap("Guest::GetVariableText");
}
void Guest::Send(messages::Message)
{
    Trap("Guest::Send");
}
void Guest::SendDelayed(messages::Message, float)
{
    Trap("Guest::SendDelayed");
}
float Guest::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("Guest::PlayAnimation");
}
script::ScriptValue* Guest::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("Guest::GetValue");
}
void Guest::SetValue(int, intptr_t, int, int)
{
    Trap("Guest::SetValue");
}
void Guest::CallFunction(int)
{
    Trap("Guest::CallFunction");
}
void Guest::Draw()
{
    Trap("Guest::Draw");
}
void Guest::DrawOverlay()
{
    Trap("Guest::DrawOverlay");
}
void Guest::OnSelect(uint32_t, uint32_t)
{
    Trap("Guest::OnSelect");
}
void Guest::ReduceSelectionToHover()
{
    Trap("Guest::ReduceSelectionToHover");
}
void Guest::ClearSelection()
{
    Trap("Guest::ClearSelection");
}

} // namespace obl::object
