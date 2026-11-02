#pragma once

#include "core/char16.h"
#include "game/ui/gui_tables_types.h"

#include <bit>
#include <cstdint>

// Engine-owned GUI definitions: how the object browser draws each filter column and the fixed names the list code
// passes to the engine. These are behavior of our GUI code, not converted content (the lists, headings, row buttons
// and credits are, see gui_content.h).
namespace obl::gui_data {

inline constexpr uint32_t kBarMin = std::bit_cast<uint32_t>(0.0f);
inline constexpr uint32_t kBarMax = std::bit_cast<uint32_t>(1.0f);

// One value cell per filter id a heading names (ui::GuiTableFilterDefinition). Mode 0 draws a 0..1 bar of the
// variable with the given bar kind, mode 1 the variable as text, mode 2 nothing. Several ids share a variable.
inline constexpr ui::GuiTableFilterDefinition kFilterDefinitions[] = {
        {1, 0, "GUEST_HAPPY_VALUE", kBarMin, kBarMax, 1},
        {3, 0, "GUEST_HAPPY_VALUE", kBarMin, kBarMax, 1},
        {4, 0, "GUEST_HAPPY_VALUE", kBarMin, kBarMax, 1},
        {2, 1, "GUEST_CASH", kBarMin, kBarMax, 0},
        {7, 0, "GUEST_DRUNK", kBarMin, kBarMax, 2},
        {8, 0, "GUEST_DESIRE_RELEIF", kBarMin, kBarMax, 2},
        {9, 0, "GUEST_ILL", kBarMin, kBarMax, 2},
        {10, 0, "GUEST_OPINION_PRICE", kBarMin, kBarMax, 1},
        {11, 0, "GUEST_OPINION_SAFETY", kBarMin, kBarMax, 1},
        {12, 0, "GUEST_OPINION_CROWDING", kBarMin, kBarMax, 1},
        {13, 0, "GUEST_OPINION_BEAUTY", kBarMin, kBarMax, 1},
        {14, 0, "GUEST_OPINION_CLEANLINESS", kBarMin, kBarMax, 1},
        {15, 0, "GUEST_OPINION_TOTTY", kBarMin, kBarMax, 1},
        {16, 0, "GUEST_OPINION_VARIETY", kBarMin, kBarMax, 1},
        {6, 0, "GUEST_DESIRE_HEADACHE", kBarMin, kBarMax, 2},
        {5, 0, "GUEST_SUNBURNT_LEVEL", kBarMin, kBarMax, 2},
        {17, 2, "BUILDING_REPAIRSTATE", kBarMin, kBarMax, 0},
        {23, 1, "BUILDING_POWER", kBarMin, kBarMax, 0},
        {18, 1, "BUILDING_REPAIRSTATE", kBarMin, kBarMax, 0},
        {28, 1, "BUILDING_NUMFAULTS", kBarMin, kBarMax, 0},
        {19, 1, "BUILDING_INCOMETODAY$", kBarMin, kBarMax, 0},
        {20, 1, "BUILDING_COSTTODAY$", kBarMin, kBarMax, 0},
        {21, 1, "BUILDING_PROFITTODAY$", kBarMin, kBarMax, 0},
        {22, 1, "BUILDING_AVERAGEPROFIT$", kBarMin, kBarMax, 0},
        {26, 1, "BUILDING_AGE", kBarMin, kBarMax, 0},
        {27, 1, "BUILDING_NUMVISITS", kBarMin, kBarMax, 0},
        {24, 1, "BUILDING_OPENHOURS", kBarMin, kBarMax, 0},
        {25, 1, "BUILDING_OPENHOURS", kBarMin, kBarMax, 0},
};

inline constexpr char kListEventInProgress[] = "BI_EVENT_INPROGRESS";
inline constexpr char kButtonNoText[] = "None";
inline constexpr char kButtonNoAction[] = "NONE";
inline constexpr char kToggleProgressSizeAction[] = "ToggleProgressSize";
inline constexpr char kToggleRadarSizeAction[] = "ToggleRadarSize";
inline constexpr char kButtonClickSound[] = "ButtonClick";
inline constexpr Char16 kListNumberFormat[] = u"%d";

} // namespace obl::gui_data
