#pragma once

#include "game/game/command.h"

namespace obl::command {

// NumberAdjust can send zero; wages and repair intervals retain their setter's minimum clamp.
// Ordered comparisons reject non-finite floats as well as values beyond the GUI controls.
inline bool GuiNumber(float value)
{
    return value >= 0.0f && value <= 999.99f;
}
inline bool GuiSlider(float value)
{
    return value >= 0.0f && value <= 1.0f;
}
template<class T> bool InGuiRange(const T&)
{
    return true;
}
inline bool InGuiRange(const SetStaffWage& c)
{
    return c.wage >= 0 && c.wage <= 999;
}
// A copied wage is one the source can hold. Only the six staff types with a wage panel (object types 7..12) take wage
// commands; they start at 9..12 and a wage edit clamps to at least 5. Service staff (bar, kitchen, DJ) start at 0 but
// have no wage panel, so 0 is never copied.
inline bool InGuiRange(const CopyStaffWage& c)
{
    return c.wage >= 5 && c.wage <= 999;
}
inline bool InGuiRange(const SetPrice& c)
{
    return GuiNumber(c.price);
}
inline bool InGuiRange(const SetEventPrice& c)
{
    return GuiNumber(c.price);
}
inline bool InGuiRange(const SetBeerPrice& c)
{
    return GuiNumber(c.value);
}
inline bool InGuiRange(const SetBeerStrength& c)
{
    return GuiSlider(c.value);
}
inline bool InGuiRange(const SetSecurityHarshness& c)
{
    return GuiSlider(c.value);
}
inline bool InGuiRange(const SetRepGuestTime& c)
{
    return GuiSlider(c.value);
}
inline bool InGuiRange(const SetRepairInterval& c)
{
    return c.value >= 0 && c.value <= 999;
}

} // namespace obl::command
