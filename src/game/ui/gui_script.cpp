#include "gui_widgets.h"
#include "game/ui/gui_script.h"
#include "game/ui/gui_script_leaves.h"
#include "game/ui/gui_script_parse.h"
#include "game/ui/gui_script_state.h"

#include "core/fatal.h"
#include "game/game/game_accessors.h"
#include "game/ui/gui_script_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"

#include <cstring>
#include <string>

namespace obl::gui {
namespace {

using gui_script::Bounds;
using gui_script::BoundsResult;
using gui_script::Cursor;
using ui::GuiScript;

// Call counters (`call-report ui.gui-script`).
test::CallCounter g_counter("ui.gui-script");

Cursor Input(const GuiScript& script)
{
    return {script.m_text, script.m_cursor};
}
void Commit(GuiScript& script, const Cursor& cursor)
{
    script.m_cursor = static_cast<uint32_t>(cursor.at);
}

} // namespace

void GuiScriptReadToken(GuiScript& script, char* out)
{
    g_counter.Count();
    Cursor cursor = Input(script);
    std::string token;
    if (!gui_script::ReadToken(cursor, token))
        fatal::UnhandledInput("ui.script-read-token", "a token the reader rejects (an unterminated field)");
    std::memcpy(out, token.c_str(), token.size() + 1);
    Commit(script, cursor);
}

void GuiScriptReadInt(GuiScript& script, int32_t& out, bool consumeDelimiter)
{
    g_counter.Count();
    Cursor cursor = Input(script);
    int32_t value = 0;
    if (!gui_script::ReadInt(cursor, value, consumeDelimiter))
        fatal::UnhandledInput("ui.script-read-int", "a number the reader rejects (an unterminated field)");
    out = value;
    Commit(script, cursor);
}

void GuiScriptReadFloat(GuiScript& script, float& out)
{
    g_counter.Count();
    Cursor cursor = Input(script);
    float value = 0;
    if (!gui_script::ReadFloat(cursor, value))
        fatal::UnhandledInput("ui.script-read-float", "a number the reader rejects (an unterminated field)");
    out = value;
    Commit(script, cursor);
}

namespace {

BoundsResult CalculateBounds(Cursor& cursor, Bounds& out, ui::GuiWidget* parent)
{
    Bounds parentBounds;
    if (parent) {
        int32_t rect[4];
        parent->GetBounds(rect);
        parentBounds = {rect[0], rect[1], rect[2], rect[3]};
    }
    const auto* renderer = game::GetRenderer();
    return gui_script::ReadBounds(cursor, out, renderer->m_width, renderer->m_height, parent ? &parentBounds : nullptr);
}

} // namespace

void GuiScriptReadBounds(GuiScript& script, Bounds& out, ui::GuiWidget* parent)
{
    g_counter.Count();
    Cursor cursor = Input(script);
    Bounds value;

    if (CalculateBounds(cursor, value, parent) != BoundsResult::Ok)
        fatal::UnhandledInput("ui.script-read-bounds", "a rectangle outside its parent (a script error)");
    out = value;
    Commit(script, cursor);
}

} // namespace obl::gui
