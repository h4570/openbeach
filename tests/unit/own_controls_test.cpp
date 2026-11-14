// OpenBeach's own controls in the original dialogs' scripts (game/ui/own_controls.h).

#include "unit_test.h"

#include "game/ui/own_controls.h"

#include <string>

using namespace obl;

namespace {

const std::string kVideoTab =
        "        <TABVideoOptions(439, 100, 483, 131, UI3_options_t_video, IGOPT_TIPTEXT_VIDEOTAB)>\n"
        "        {\n"
        "            CheckBox(194, 148, 210, 163, UI3_fieldcheckbox, GAME_NUDGE, IGOPT_TIPTEXT_NUDGE)\n"
        "\n"
        "            Button(194, 242, 359, 266, UI3_bi_longbutton, IGOPT_VIDEO_RESET, ResetVideoOptions, None, IGOPT_TIPTEXT_RESET_VIDEO)\n"
        "        }\n";

} // namespace

OBL_TEST_CASE("own_controls.no_stick_leaves_the_script_alone")
{
    own_controls::SetPadPointer(false);
    OBL_CHECK(own_controls::Apply(kVideoTab) == kVideoTab);
}

OBL_TEST_CASE("own_controls.slider_follows_the_nudge_scroll_row")
{
    own_controls::SetPadPointer(true);
    const std::string script = own_controls::Apply(kVideoTab);
    const size_t nudge = script.find("GAME_NUDGE");
    const size_t slider = script.find("Slider(206, 163, 381, 178, CURSOR_SPEED");
    const size_t reset = script.find("ResetVideoOptions");
    OBL_CHECK(nudge != std::string::npos && slider != std::string::npos && reset != std::string::npos);
    OBL_CHECK(nudge < slider && slider < reset); // inside the Video tab, before its reset button
    OBL_CHECK(script.find("Text(32, 163, 176, 178, OBL_CURSOR_SPEED") != std::string::npos);
    const size_t scroll = script.find("Slider(206, 178, 381, 193, SCROLL_SPEED");
    OBL_CHECK(scroll != std::string::npos && slider < scroll && scroll < reset); // the scroll speed row follows the cursor speed row
    own_controls::SetPadPointer(false);
}

OBL_TEST_CASE("own_controls.crlf_scripts_keep_their_line_breaks")
{
    own_controls::SetPadPointer(true);
    std::string crlf;
    for (char c : kVideoTab) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const std::string script = own_controls::Apply(crlf);
    for (size_t i = 0; i < script.size(); ++i)
        if (script[i] == '\n') OBL_CHECK(i > 0 && script[i - 1] == '\r');
    OBL_CHECK(script.find("CURSOR_SPEED") != std::string::npos);
    own_controls::SetPadPointer(false);
}

OBL_TEST_CASE("own_controls.other_layouts_are_untouched")
{
    own_controls::SetPadPointer(true);
    const std::string other = "DialogItem BuildPanel(0, 0, 10, 10, X, 1, 1)\n{\n  CheckBox(1, 1, 2, 2, G, GAME_NUDGE, T)\n}\n";
    OBL_CHECK(own_controls::Apply(other) == other); // no Video tab: nothing to attach to
    own_controls::SetPadPointer(false);
}
