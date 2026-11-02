// BitmapFont metrics contain 256 entries. Some strings contain real Unicode characters while
// most localized text stores pre-encoded code-page bytes in UTF-16 code units. Convert only code
// units above 255 before the renderer indexes its metrics and kerning tables.

#include "core/char16.h"
#include "game/ui/text_encoding.h"

#include "core/code_pages.h"
#include "game/ui/font.h"

#include <cstring>
#include <string>
#include <unordered_set>

namespace obl::ui {
namespace {
std::unordered_set<const void*> g_polishFonts;
} // namespace

const Char16* EncodedText(const void* renderer, const Char16* text, Text16& encoded)
{
    if (!text) return text;
    const Char16* first = text;
    while (*first && *first <= 0xff) ++first;
    if (!*first) return text;

    const auto* font = static_cast<const ui::TextRenderer*>(renderer)->m_font;
    const bool polish = g_polishFonts.contains(font);
    encoded.assign(text);
    for (Char16& ch : encoded) {
        if (ch <= 0xff) continue;
        // WideCharToMultiByte(1250/1252, WC_NO_BEST_FIT_CHARS) on the code page tables: a unit the font's code page
        // lacks becomes '?'.
        uint8_t byte = 0;
        if (!code_pages::ByteOf(polish, static_cast<char16_t>(ch), byte)) byte = '?';
        ch = byte;
    }
    return encoded.c_str();
}

void TrackBitmapFont(const void* font, const char* name)
{
    if (name && std::strstr(name, "Pol"))
        g_polishFonts.insert(font);
    else
        g_polishFonts.erase(font);
}
} // namespace obl::ui
