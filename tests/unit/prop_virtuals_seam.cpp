// The unit tests' traps for the static branch's overrides, as object_virtuals_seam.cpp.

#include "core/char16.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_table.h"

#include <cstdio>
#include <cstdlib>

namespace obl::object {

namespace {
[[noreturn]] void StaticTrap(const char* name)
{
    std::fprintf(stderr, "unit test dispatched static object virtual %s\n", name);
    std::abort();
}
} // namespace

bool Prop::Update()
{
    StaticTrap("Prop::Update");
}
bool Prop::ReceiveMessage(messages::Message*)
{
    StaticTrap("Prop::ReceiveMessage");
}
Scenery::~Scenery() = default;
bool Scenery::HasState(uint32_t)
{
    StaticTrap("Scenery::HasState");
}
bool Scenery::OnEnterState(uint32_t)
{
    StaticTrap("Scenery::OnEnterState");
}
bool Scenery::Update()
{
    StaticTrap("Scenery::Update");
}
bool Scenery::OnExitState()
{
    StaticTrap("Scenery::OnExitState");
}
bool Scenery::ReceiveMessage(messages::Message*)
{
    StaticTrap("Scenery::ReceiveMessage");
}
void Scenery::GetVariableText(const char*, obl::Char16*)
{
    StaticTrap("Scenery::GetVariableText");
}
void Scenery::Draw()
{
    StaticTrap("Scenery::Draw");
}
void Scenery::OnSelect(uint32_t, uint32_t)
{
    StaticTrap("Scenery::OnSelect");
}
bool Scenery::IsSelectable()
{
    StaticTrap("Scenery::IsSelectable");
}
const char* Scenery::GetName()
{
    StaticTrap("Scenery::GetName");
}

Building::~Building() = default;
bool Building::HasState(uint32_t)
{
    StaticTrap("Building::HasState");
}
bool Building::OnEnterState(uint32_t)
{
    StaticTrap("Building::OnEnterState");
}
bool Building::Update()
{
    StaticTrap("Building::Update");
}
bool Building::OnExitState()
{
    StaticTrap("Building::OnExitState");
}
bool Building::ReceiveMessage(messages::Message*)
{
    StaticTrap("Building::ReceiveMessage");
}
void Building::GetVariableText(const char*, obl::Char16*)
{
    StaticTrap("Building::GetVariableText");
}
void Building::SetVariable(const char*, const void*)
{
    StaticTrap("Building::SetVariable");
}
void Building::Draw()
{
    StaticTrap("Building::Draw");
}
void Building::DrawOverlay()
{
    StaticTrap("Building::DrawOverlay");
}
int Building::GetOverlayOffset()
{
    StaticTrap("Building::GetOverlayOffset");
}
void Building::OnSelect(uint32_t, uint32_t)
{
    StaticTrap("Building::OnSelect");
}
void Building::ReduceSelectionToHover()
{
    StaticTrap("Building::ReduceSelectionToHover");
}
bool Building::IsSelectable()
{
    StaticTrap("Building::IsSelectable");
}

Furniture::~Furniture() = default;
bool Furniture::HasState(uint32_t)
{
    StaticTrap("Furniture::HasState");
}
bool Furniture::OnEnterState(uint32_t)
{
    StaticTrap("Furniture::OnEnterState");
}
bool Furniture::Update()
{
    StaticTrap("Furniture::Update");
}
bool Furniture::OnExitState()
{
    StaticTrap("Furniture::OnExitState");
}
bool Furniture::ReceiveMessage(messages::Message*)
{
    StaticTrap("Furniture::ReceiveMessage");
}
void Furniture::GetVariableText(const char*, obl::Char16*)
{
    StaticTrap("Furniture::GetVariableText");
}
void Furniture::SetVariable(const char*, const void*)
{
    StaticTrap("Furniture::SetVariable");
}
script::ScriptValue* Furniture::GetValue(script::ScriptValue*, int, intptr_t)
{
    StaticTrap("Furniture::GetValue");
}
void Furniture::SetValue(int, intptr_t, int, int)
{
    StaticTrap("Furniture::SetValue");
}
void Furniture::Draw()
{
    StaticTrap("Furniture::Draw");
}
} // namespace obl::object
