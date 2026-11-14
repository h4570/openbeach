#include "game/ui/own_controls.h"

#include <string_view>

namespace obl::own_controls {

namespace {

bool g_padPointer;

// Below the Video tab's last checkbox ("nudge scrolling", GAME_NUDGE), in the two rows that follow; each slider and its -/+ images
// repeat the Audio tab's volume sliders. The same rows stand in every language's script.
constexpr std::string_view kTab = "TABVideoOptions";
constexpr std::string_view kAnchor = "GAME_NUDGE";
constexpr std::string_view kRows[] = {
        "",
        "            // cursor speed (OpenBeach: the stick of a handheld)",
        "            Text(32, 163, 176, 178, OBL_CURSOR_SPEED, 0, 0, 1)",
        "            Slider(206, 163, 381, 178, CURSOR_SPEED, 0, 1, 0, OBL_TIPTEXT_CURSOR_SPEED)",
        "            Image(194, 163, 207, 178, UI3_slider_minus_l)",
        "            Image(380, 163, 393, 178, UI3_slider_plus_r)",
        "",
        "            // map scroll speed (OpenBeach: the screen edge scrolls fast with a stick-moved cursor)",
        "            Text(32, 178, 176, 193, OBL_SCROLL_SPEED, 0, 0, 1)",
        "            Slider(206, 178, 381, 193, SCROLL_SPEED, 0, 1, 0, OBL_TIPTEXT_SCROLL_SPEED)",
        "            Image(194, 178, 207, 193, UI3_slider_minus_l)",
        "            Image(380, 178, 393, 193, UI3_slider_plus_r)",
};

} // namespace

void SetPadPointer(bool available)
{
    g_padPointer = available;
}

bool PadPointer()
{
    return g_padPointer;
}

std::string Apply(std::string script)
{
    if (!g_padPointer) return script;
    const size_t tab = script.find(kTab);
    if (tab == std::string::npos) return script;
    const size_t anchor = script.find(kAnchor, tab);
    if (anchor == std::string::npos) return script;
    // The tab ends at its own closing brace: the anchor must come before the next tab opens.
    const size_t nextTab = script.find("<TAB", tab + kTab.size());
    if (nextTab != std::string::npos && nextTab < anchor) return script;
    const size_t lineEnd = script.find('\n', anchor);
    if (lineEnd == std::string::npos) return script;
    const bool crlf = lineEnd > 0 && script[lineEnd - 1] == '\r';
    std::string insert;
    for (std::string_view row : kRows) {
        insert += crlf ? "\r\n" : "\n";
        insert += row;
    }
    // The line break that ends the anchor line stays where it was: the rows go before it.
    script.insert(crlf ? lineEnd - 1 : lineEnd, insert);
    return script;
}

} // namespace obl::own_controls
