#pragma once

#include <cstddef>

namespace obl::game {

// Our victory-page layout: three columns, twelve 30-pixel lines each,
// within the 800x600 front-end canvas. Content and fonts come from credit rows.
inline constexpr int kCreditColumns = 3;
inline constexpr int kCreditRowsPerColumn = 12;
inline constexpr int kCreditsPerPage = kCreditColumns * kCreditRowsPerColumn;
inline constexpr int kCreditLineHeight = 30;
inline constexpr int kCreditTop = 160;
inline constexpr int kCreditLeft = 40;
inline constexpr int kCreditColumnPitch = 248;
inline constexpr int kCreditColumnWidth = 224;

constexpr int CreditPageCount(size_t rows)
{
    return rows == 0 ? 1 : static_cast<int>((rows - 1) / kCreditsPerPage + 1);
}

} // namespace obl::game
