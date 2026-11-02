#include "game/game/diagnostics.h"
#include "game/ui/gui_script_parse.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_script.h"
#include "game/ui/gui_script_leaves.h"
#include "game/ui/gui_script_state.h"

#include "game/game/game_accessors.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_script_types.h"
#include "core/call_counter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace obl::gui {
namespace {

using ui::GuiScript;
using ui::GuiScriptNode;
using ui::GuiWidget;

// Call counters (`call-report ui.gui-script-parse`).
test::CallCounter g_counter("ui.gui-script-parse");

constexpr std::array<const char*, 25> kCommandNames = {"CheckBox", "DialogItem", "MainItem", "MainItem640", "TabWindow", "Table", "Define",
        "Button", "Slider", "Text", "Variable", "ComboBox", "VariableImage", "Image", "Font", "TGA", "TickerText", "ImageList", "Icon",
        "Bar", "NumberAdjust", "RadioGroup", "RadioButton", "ListBox", "StartDebug"};

constexpr const char* kOnlyOneTab = "GUI_Parse - Only one tab can be defined at a time, file offset - %u";
constexpr const char* kInvalidCommand = "GUI_Parse - Invalid command at file offset - %u";
constexpr const char* kNoParent = "GUI_Parse - No parent for defined item at file offset - %u";
constexpr const char* kInvalidDefined = "GUI_Parse - Invalid Command/Defined item at file offset - %u string %s";

constexpr size_t kNameChars = 32;

bool IsBlank(char c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

template<class... Args> void Fatal(const char* format, Args... args)
{
    game::FatalError(format, args...);
}

void ParseAngle(GuiScript& s)
{
    const char* text = s.m_text;
    char c;
    do {
        do {
            ++s.m_cursor;
            c = text[s.m_cursor];
        } while (c == ' ');
    } while (c == '\r' || c == '\n' || c == '\t');
    char name[kNameChars + 1];
    size_t length = 0;
    while (length < kNameChars) {
        c = text[s.m_cursor];
        if (c == ' ' || c == '(' || c == '\r' || c == '\n') break;
        name[length++] = c;
        ++s.m_cursor;
    }
    name[length] = 0;
    if (std::strncmp("TAB", name, 3) == 0) {
        if (s.m_tab)
            Fatal(kOnlyOneTab, s.m_cursor);
        else
            gui_script_leaves::ParseTab(&s);
    } else if (std::strncmp("COL", name, 3) == 0) {
        gui_script_leaves::ParseColumn(&s);
    } else {
        Fatal(kInvalidCommand, s.m_cursor);
    }
}

// `}` closes the innermost open thing: a column, else a tab, else the parent container.
void ParseClose(GuiScript& s)
{
    if (s.m_column)
        s.m_column = nullptr;
    else if (s.m_tab)
        s.m_tab = nullptr;
    else
        GuiScriptPopParent(s);
    ++s.m_cursor;
}

// Any other word: a command of the table, else the use of a Define template (which goes under the current parent as it stands).
// Returns what ParseCommand returned (true: StartDebug).
bool ParseWord(GuiScript& s)
{
    const char* text = s.m_text;
    char name[kNameChars + 1];
    size_t length = 0;
    do {
        const char c = text[s.m_cursor];
        if (c == '(' || c == '\r' || length >= kNameChars) break;
        name[length++] = c;
        ++s.m_cursor;
    } while (text[s.m_cursor] != ' ');
    name[length] = 0;
    for (uint32_t id = 0; id < kCommandNames.size(); ++id) {
        if (std::strcmp(name, kCommandNames[id]) == 0) return gui_script_leaves::ParseCommand(&s, static_cast<int32_t>(id));
    }
    if (!s.m_parent) {
        Fatal(kNoParent, s.m_cursor);
        return false;
    }
    for (const GuiScriptNode* node = s.m_defines; node; node = node->m_next) {
        const auto* define = static_cast<const ui::GuiDefine*>(node->m_widget);
        if (std::strcmp(define->m_name, name) == 0) {
            s.m_parent->AddChild(node->m_widget);
            return false;
        }
    }
    Fatal(kInvalidDefined, s.m_cursor, name);
    return false;
}

void Parse(GuiScript& s, const char* text, uint32_t size)
{
    s.m_parent = s.m_tab = s.m_column = nullptr;
    s.m_parentStack = nullptr;
    s.m_previousParent = s.m_lastWidget = nullptr;
    s.m_defines = s.m_dialogs = s.m_widgets = s.m_widgetTail = s.m_dialogTail = nullptr;
    s.m_nextId = 0;
    s.m_fontIndex = 0;
    s.m_cursor = 0;
    s.m_text = text;
    bool finished = false;
    if (size != 0) {
        do {
            if (finished) break;
            while (IsBlank(text[s.m_cursor])) ++s.m_cursor; // not bounded by the size: the loader appends a dot after the data
            if (s.m_cursor < size) {
                const char c = text[s.m_cursor];
                if (c == '<') {
                    ParseAngle(s);
                } else if (c == '}') {
                    ParseClose(s);
                } else if (c == '/') {
                    ++s.m_cursor;
                    while (text[s.m_cursor] != '\n') ++s.m_cursor;
                } else if (c == '(') {
                    ++s.m_cursor;
                } else {
                    finished = ParseWord(s);
                }
            }
        } while (s.m_cursor < size);
    }
    // The templates were only borrowed by the containers that used them; the nodes go, the widgets stay.
    ReleaseBorrowedList(s.m_defines);
}

} // namespace

void GuiScriptParse(GuiScript& script, const char* text, uint32_t size)
{
    g_counter.Count();
    Parse(script, text, size);
}

} // namespace obl::gui
