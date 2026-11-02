#pragma once

#include "formats/gui_content.h"
#include "game/tables/text_table.h"

#include <string_view>
#include <vector>

// OpenBeach's own credits, merged into the converted credits roll. The roll draws a row as the text
// "CREDIT_ENTRY_<line>"; our rows use line numbers from kFirstLine on, and their texts enter the language table
// (text::Append), where a language file may translate them under the same keys.
namespace obl::own_credits {

inline constexpr int kFirstLine = 9000;

// Append our rows and mark our final row as the end of the roll. Empty input
// produces a complete own-credit roll, without an imported pack.
std::vector<gui_content::Credit> Merge(const std::vector<gui_content::Credit>& credits);

// "CREDIT_ENTRY_<n>" texts in renderer byte units for `language` (text_load::LanguageCode; unknown gives English).
std::vector<text::Entry> Texts(std::string_view language);

} // namespace obl::own_credits
