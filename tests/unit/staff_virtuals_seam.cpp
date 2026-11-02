// The unit tests' traps for the staff overrides: the fixtures emit the vtables but never
// dispatch.

#include "core/char16.h"
#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"

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

bool Staff::Update()
{
    Trap("Staff::Update");
}
bool Staff::ReceiveMessage(messages::Message*)
{
    Trap("Staff::ReceiveMessage");
}
void Staff::GetVariableText(const char*, obl::Char16*)
{
    Trap("Staff::GetVariableText");
}
void Staff::SetVariable(const char*, const void*)
{
    Trap("Staff::SetVariable");
}
script::ScriptValue* Staff::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("Staff::GetValue");
}
void Staff::SetValue(int, intptr_t, int, int)
{
    Trap("Staff::SetValue");
}
void Staff::CallFunction(int)
{
    Trap("Staff::CallFunction");
}
void Staff::DrawOverlay()
{
    Trap("Staff::DrawOverlay");
}
void Staff::OnSelect(uint32_t, uint32_t)
{
    Trap("Staff::OnSelect");
}
void Staff::ReduceSelectionToHover()
{
    Trap("Staff::ReduceSelectionToHover");
}
void Staff::ClearSelection()
{
    Trap("Staff::ClearSelection");
}
void Staff::SetHome(int32_t)
{
    Trap("Staff::SetHome");
}
const char* Staff::GetFeedbackText(uint32_t)
{
    Trap("Staff::GetFeedbackText");
}
void Staff::Sack()
{
    Trap("Staff::Sack");
}
void Staff::DirectToObject(int32_t)
{
    Trap("Staff::DirectToObject");
}

ServiceStaff::~ServiceStaff() = default;
bool ServiceStaff::HasState(uint32_t)
{
    Trap("ServiceStaff::HasState");
}
bool ServiceStaff::OnEnterState(uint32_t)
{
    Trap("ServiceStaff::OnEnterState");
}
bool ServiceStaff::Update()
{
    Trap("ServiceStaff::Update");
}
bool ServiceStaff::OnExitState()
{
    Trap("ServiceStaff::OnExitState");
}
bool ServiceStaff::ReceiveMessage(messages::Message*)
{
    Trap("ServiceStaff::ReceiveMessage");
}
void ServiceStaff::Send(messages::Message)
{
    Trap("ServiceStaff::Send");
}
void ServiceStaff::SendDelayed(messages::Message, float)
{
    Trap("ServiceStaff::SendDelayed");
}
void ServiceStaff::Draw()
{
    Trap("ServiceStaff::Draw");
}
void ServiceStaff::OnSelect(uint32_t, uint32_t)
{
    Trap("ServiceStaff::OnSelect");
}
void ServiceStaff::SetHome(int32_t)
{
    Trap("ServiceStaff::SetHome");
}

SnootyStaff::~SnootyStaff() = default;
bool SnootyStaff::HasState(uint32_t)
{
    Trap("SnootyStaff::HasState");
}
bool SnootyStaff::OnEnterState(uint32_t)
{
    Trap("SnootyStaff::OnEnterState");
}
bool SnootyStaff::Update()
{
    Trap("SnootyStaff::Update");
}
bool SnootyStaff::OnExitState()
{
    Trap("SnootyStaff::OnExitState");
}
bool SnootyStaff::ReceiveMessage(messages::Message*)
{
    Trap("SnootyStaff::ReceiveMessage");
}
void SnootyStaff::SetHome(int32_t)
{
    Trap("SnootyStaff::SetHome");
}

Builder::~Builder() = default;
bool Builder::HasState(uint32_t)
{
    Trap("BuilderObject::HasState");
}
bool Builder::OnEnterState(uint32_t)
{
    Trap("BuilderObject::OnEnterState");
}
bool Builder::Update()
{
    Trap("BuilderObject::Update");
}
bool Builder::OnExitState()
{
    Trap("BuilderObject::OnExitState");
}
bool Builder::ReceiveMessage(messages::Message*)
{
    Trap("BuilderObject::ReceiveMessage");
}
void Builder::GetVariableText(const char*, obl::Char16*)
{
    Trap("BuilderObject::GetVariableText");
}
void Builder::SetVariable(const char*, const void*)
{
    Trap("BuilderObject::SetVariable");
}
void Builder::Send(messages::Message)
{
    Trap("BuilderObject::Send");
}
void Builder::SendDelayed(messages::Message, float)
{
    Trap("BuilderObject::SendDelayed");
}
float Builder::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("BuilderObject::PlayAnimation");
}
script::ScriptValue* Builder::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("BuilderObject::GetValue");
}
void Builder::SetValue(int, intptr_t, int, int)
{
    Trap("BuilderObject::SetValue");
}
void Builder::CallFunction(int)
{
    Trap("BuilderObject::CallFunction");
}
void Builder::OnSelect(uint32_t, uint32_t)
{
    Trap("BuilderObject::OnSelect");
}
const char* Builder::GetFeedbackText(uint32_t)
{
    Trap("BuilderObject::GetFeedbackText");
}
void Builder::Sack()
{
    Trap("BuilderObject::Sack");
}
void Builder::DirectToObject(int32_t)
{
    Trap("BuilderObject::DirectToObject");
}

Cleaner::~Cleaner() = default;
bool Cleaner::HasState(uint32_t)
{
    Trap("CleanerObject::HasState");
}
bool Cleaner::OnEnterState(uint32_t)
{
    Trap("CleanerObject::OnEnterState");
}
bool Cleaner::Update()
{
    Trap("CleanerObject::Update");
}
bool Cleaner::OnExitState()
{
    Trap("CleanerObject::OnExitState");
}
bool Cleaner::ReceiveMessage(messages::Message*)
{
    Trap("CleanerObject::ReceiveMessage");
}
void Cleaner::GetVariableText(const char*, obl::Char16*)
{
    Trap("CleanerObject::GetVariableText");
}
void Cleaner::SetVariable(const char*, const void*)
{
    Trap("CleanerObject::SetVariable");
}
void Cleaner::Send(messages::Message)
{
    Trap("CleanerObject::Send");
}
void Cleaner::SendDelayed(messages::Message, float)
{
    Trap("CleanerObject::SendDelayed");
}
float Cleaner::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("CleanerObject::PlayAnimation");
}
script::ScriptValue* Cleaner::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("CleanerObject::GetValue");
}
void Cleaner::SetValue(int, intptr_t, int, int)
{
    Trap("CleanerObject::SetValue");
}
void Cleaner::CallFunction(int)
{
    Trap("CleanerObject::CallFunction");
}
void Cleaner::OnSelect(uint32_t, uint32_t)
{
    Trap("CleanerObject::OnSelect");
}
const char* Cleaner::GetFeedbackText(uint32_t)
{
    Trap("CleanerObject::GetFeedbackText");
}
void Cleaner::Sack()
{
    Trap("CleanerObject::Sack");
}
void Cleaner::DirectToObject(int32_t)
{
    Trap("CleanerObject::DirectToObject");
}

Lifeguard::~Lifeguard() = default;
bool Lifeguard::HasState(uint32_t)
{
    Trap("LifeguardObject::HasState");
}
bool Lifeguard::OnEnterState(uint32_t)
{
    Trap("LifeguardObject::OnEnterState");
}
bool Lifeguard::Update()
{
    Trap("LifeguardObject::Update");
}
bool Lifeguard::OnExitState()
{
    Trap("LifeguardObject::OnExitState");
}
bool Lifeguard::ReceiveMessage(messages::Message*)
{
    Trap("LifeguardObject::ReceiveMessage");
}
void Lifeguard::GetVariableText(const char*, obl::Char16*)
{
    Trap("LifeguardObject::GetVariableText");
}
void Lifeguard::SetVariable(const char*, const void*)
{
    Trap("LifeguardObject::SetVariable");
}
void Lifeguard::Send(messages::Message)
{
    Trap("LifeguardObject::Send");
}
void Lifeguard::SendDelayed(messages::Message, float)
{
    Trap("LifeguardObject::SendDelayed");
}
float Lifeguard::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("LifeguardObject::PlayAnimation");
}
script::ScriptValue* Lifeguard::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("LifeguardObject::GetValue");
}
void Lifeguard::SetValue(int, intptr_t, int, int)
{
    Trap("LifeguardObject::SetValue");
}
void Lifeguard::CallFunction(int)
{
    Trap("LifeguardObject::CallFunction");
}
void Lifeguard::Draw()
{
    Trap("LifeguardObject::Draw");
}
void Lifeguard::OnSelect(uint32_t, uint32_t)
{
    Trap("LifeguardObject::OnSelect");
}
const char* Lifeguard::GetFeedbackText(uint32_t)
{
    Trap("LifeguardObject::GetFeedbackText");
}
void Lifeguard::Sack()
{
    Trap("LifeguardObject::Sack");
}
void Lifeguard::DirectToObject(int32_t)
{
    Trap("LifeguardObject::DirectToObject");
}

Mechanic::~Mechanic() = default;
bool Mechanic::HasState(uint32_t)
{
    Trap("MechanicObject::HasState");
}
bool Mechanic::OnEnterState(uint32_t)
{
    Trap("MechanicObject::OnEnterState");
}
bool Mechanic::Update()
{
    Trap("MechanicObject::Update");
}
bool Mechanic::OnExitState()
{
    Trap("MechanicObject::OnExitState");
}
bool Mechanic::ReceiveMessage(messages::Message*)
{
    Trap("MechanicObject::ReceiveMessage");
}
void Mechanic::GetVariableText(const char*, obl::Char16*)
{
    Trap("MechanicObject::GetVariableText");
}
void Mechanic::SetVariable(const char*, const void*)
{
    Trap("MechanicObject::SetVariable");
}
void Mechanic::Send(messages::Message)
{
    Trap("MechanicObject::Send");
}
void Mechanic::SendDelayed(messages::Message, float)
{
    Trap("MechanicObject::SendDelayed");
}
float Mechanic::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("MechanicObject::PlayAnimation");
}
script::ScriptValue* Mechanic::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("MechanicObject::GetValue");
}
void Mechanic::SetValue(int, intptr_t, int, int)
{
    Trap("MechanicObject::SetValue");
}
void Mechanic::CallFunction(int)
{
    Trap("MechanicObject::CallFunction");
}
void Mechanic::OnSelect(uint32_t, uint32_t)
{
    Trap("MechanicObject::OnSelect");
}
const char* Mechanic::GetFeedbackText(uint32_t)
{
    Trap("MechanicObject::GetFeedbackText");
}
void Mechanic::Sack()
{
    Trap("MechanicObject::Sack");
}
void Mechanic::DirectToObject(int32_t)
{
    Trap("MechanicObject::DirectToObject");
}

Rep::~Rep() = default;
bool Rep::HasState(uint32_t)
{
    Trap("RepObject::HasState");
}
bool Rep::OnEnterState(uint32_t)
{
    Trap("RepObject::OnEnterState");
}
bool Rep::Update()
{
    Trap("RepObject::Update");
}
bool Rep::OnExitState()
{
    Trap("RepObject::OnExitState");
}
bool Rep::ReceiveMessage(messages::Message*)
{
    Trap("RepObject::ReceiveMessage");
}
void Rep::GetVariableText(const char*, obl::Char16*)
{
    Trap("RepObject::GetVariableText");
}
void Rep::SetVariable(const char*, const void*)
{
    Trap("RepObject::SetVariable");
}
void Rep::Send(messages::Message)
{
    Trap("RepObject::Send");
}
void Rep::SendDelayed(messages::Message, float)
{
    Trap("RepObject::SendDelayed");
}
float Rep::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("RepObject::PlayAnimation");
}
script::ScriptValue* Rep::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("RepObject::GetValue");
}
void Rep::SetValue(int, intptr_t, int, int)
{
    Trap("RepObject::SetValue");
}
void Rep::CallFunction(int)
{
    Trap("RepObject::CallFunction");
}
void Rep::Draw()
{
    Trap("RepObject::Draw");
}
void Rep::OnSelect(uint32_t, uint32_t)
{
    Trap("RepObject::OnSelect");
}
const char* Rep::GetFeedbackText(uint32_t)
{
    Trap("RepObject::GetFeedbackText");
}
void Rep::Sack()
{
    Trap("RepObject::Sack");
}
void Rep::DirectToObject(int32_t)
{
    Trap("RepObject::DirectToObject");
}

SecurityGuard::~SecurityGuard() = default;
bool SecurityGuard::HasState(uint32_t)
{
    Trap("SecurityGuardObject::HasState");
}
bool SecurityGuard::OnEnterState(uint32_t)
{
    Trap("SecurityGuardObject::OnEnterState");
}
bool SecurityGuard::Update()
{
    Trap("SecurityGuardObject::Update");
}
bool SecurityGuard::OnExitState()
{
    Trap("SecurityGuardObject::OnExitState");
}
bool SecurityGuard::ReceiveMessage(messages::Message*)
{
    Trap("SecurityGuardObject::ReceiveMessage");
}
void SecurityGuard::GetVariableText(const char*, obl::Char16*)
{
    Trap("SecurityGuardObject::GetVariableText");
}
void SecurityGuard::SetVariable(const char*, const void*)
{
    Trap("SecurityGuardObject::SetVariable");
}
void SecurityGuard::Send(messages::Message)
{
    Trap("SecurityGuardObject::Send");
}
void SecurityGuard::SendDelayed(messages::Message, float)
{
    Trap("SecurityGuardObject::SendDelayed");
}
float SecurityGuard::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    Trap("SecurityGuardObject::PlayAnimation");
}
script::ScriptValue* SecurityGuard::GetValue(script::ScriptValue*, int, intptr_t)
{
    Trap("SecurityGuardObject::GetValue");
}
void SecurityGuard::SetValue(int, intptr_t, int, int)
{
    Trap("SecurityGuardObject::SetValue");
}
void SecurityGuard::CallFunction(int)
{
    Trap("SecurityGuardObject::CallFunction");
}
void SecurityGuard::OnSelect(uint32_t, uint32_t)
{
    Trap("SecurityGuardObject::OnSelect");
}
const char* SecurityGuard::GetFeedbackText(uint32_t)
{
    Trap("SecurityGuardObject::GetFeedbackText");
}
void SecurityGuard::Sack()
{
    Trap("SecurityGuardObject::Sack");
}
void SecurityGuard::DirectToObject(int32_t)
{
    Trap("SecurityGuardObject::DirectToObject");
}

} // namespace obl::object
