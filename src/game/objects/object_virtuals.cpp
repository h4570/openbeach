// The object slots without a class of their own: the pure Base slots trap, the rest are the shared defaults.
#include "game/objects/object_base.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_table.h"
#include "core/call_counter.h"
#include "core/fatal.h"

namespace obl::object {

namespace {
// Every concrete class implements HasState, OnEnterState, Update, OnExitState and ReceiveMessage.
[[noreturn]] void PureCall(const char* name)
{
    fatal::UnhandledInput(name, "pure virtual slot called");
}

// Every call of a shared default (some run per object and frame).
test::CallCounter g_defaults("objects.default-methods");
} // namespace

bool Base::HasState(uint32_t)
{
    PureCall("HasState");
}
bool Base::OnEnterState(uint32_t)
{
    PureCall("OnEnterState");
}
bool Base::Update()
{
    PureCall("Update");
}
bool Base::OnExitState()
{
    PureCall("OnExitState");
}
bool Base::ReceiveMessage(messages::Message*)
{
    PureCall("ReceiveMessage");
}

void Base::SetVariable(const char*, const void*)
{
    g_defaults.Count();
}
void Mover::Send(messages::Message)
{
    g_defaults.Count();
}
void Mover::SendDelayed(messages::Message, float)
{
    g_defaults.Count();
}
bool Base::IsSelectable()
{
    g_defaults.Count();
    return true;
}
const char* Base::GetName()
{
    g_defaults.Count();
    return m_Name;
}
bool ServiceStaff::OnExitState()
{
    g_defaults.Count();
    return true;
}
bool SnootyStaff::OnExitState()
{
    g_defaults.Count();
    return true;
}
bool Scenery::OnExitState()
{
    g_defaults.Count();
    return true;
}
bool Furniture::OnExitState()
{
    g_defaults.Count();
    return true;
}
void Base::CallFunction(int)
{
    g_defaults.Count();
}
void Staff::DirectToObject(int)
{
    g_defaults.Count();
}
void Base::DrawOverlay() {}
void Base::ReduceSelectionToHover() {}
void Base::ClearSelection() {}
void Base::OnSelect(uint32_t, uint32_t)
{
    g_defaults.Count();
}
void ServiceStaff::OnSelect(uint32_t, uint32_t)
{
    g_defaults.Count();
}
void Shark::OnSelect(uint32_t, uint32_t)
{
    g_defaults.Count();
}
} // namespace obl::object
