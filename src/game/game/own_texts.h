#pragma once

#include "game/tables/text_table.h"

#include <string_view>
#include <vector>

// Texts of OpenBeach's own controls (ui/own_controls.h), merged into the language table like the own credits (text::Append adds a
// key only when the table has none, so a language file may translate them under the same keys).
namespace obl::own_texts {

// Our keys with their texts in renderer byte units for `language` (text_load::LanguageCode; unknown gives English).
std::vector<text::Entry> Texts(std::string_view language);

} // namespace obl::own_texts
