#include "game/game/diagnostics.h"
#include "gui_manager_frame.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/text_renderer.h"
#include "game/ui/font_construct.h"
#include "gui_widgets.h"
#include "gui_controls.h"
#include "gui_lists.h"
#include "gui_containers.h"
#include "gui_tables.h"
#include "game/ui/gui_script_leaves.h"
#include "game/ui/gui_script.h"
#include "game/ui/gui_script_state.h"
#include "gui_tooltip.h"
#include "gui_widgets_image_bar.h"
#include "gui_widgets_leaves.h"
#include "gui_variable_lifecycle.h"
#include "gui_manager_dialogs.h"

#include "core/fatal.h"
#include "game/game/game_accessors.h"
#include "game/ui/text_table.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/gui_tables_types.h"
#include "game/ui/gui_script_types.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <new>
#include <type_traits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace obl::gui {
namespace {

using gui_script::Bounds;
using ui::GuiScript;
using ui::GuiWidget;

// Call counters (`call-report ui.gui-script-leaves`).
test::CallCounter g_counter("ui.gui-script-leaves");

enum Leaf {
    Tga,
    Text,
    Variable,
    VariableImage,
    Image,
    Icon,
    Bar,
    Button,
    CheckBox,
    Slider,
    ComboBox,
    ImageList,
    NumberAdjust,
    RadioGroup,
    RadioButton,
    ListBox,
    Tab,
    TabWindow,
    Define,
    DialogItem,
    MainItem,
    MainItem640,
    Column,
    Table,
    Font,
    LeafCount
};

constexpr size_t kTokenBytes = 512;
constexpr uint32_t kTickerText = 16, kStartDebug = 24;

// --- game calls ------------------------------------------------------------------------------------------------------------

void ReadBounds(GuiScript& s, Bounds& out)
{
    GuiScriptReadBounds(s, out, s.m_parent);
}
void ReadToken(GuiScript& s, char* out)
{
    GuiScriptReadToken(s, out);
}
void ReadInt(GuiScript& s, int32_t& out, bool consumeDelimiter)
{
    GuiScriptReadInt(s, out, consumeDelimiter);
}
void ReadFloat(GuiScript& s, float& out)
{
    GuiScriptReadFloat(s, out);
}
void AddWidget(GuiScript& s, GuiWidget* widget)
{
    GuiScriptAddWidget(s, widget, false);
}

bool IsBlank(char c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}
void SkipBlanks(GuiScript& s)
{
    while (IsBlank(s.m_text[s.m_cursor])) ++s.m_cursor;
}

bool Open(GuiScript& s)
{
    if (s.m_text[s.m_cursor] != '(') return false;
    ++s.m_cursor;
    SkipBlanks(s);
    return true;
}

// Keys and file names become the widget's own copy (it strcpy's them), the source is only the stack buffer. `new T()`
// value-initializes the record (every field zero) before the widget's in-place constructor runs over it.
template<class T> GuiWidget* Construct(auto construct)
{
    return construct(static_cast<GuiWidget*>(new T()));
}

// `Image(rect, name, (tooltip))` and `VariableImage` after the widget is attached: the delimiter ReadToken consumed decides.
void TooltipAfterComma(GuiScript& s, GuiWidget* widget)
{
    char tooltip[kTokenBytes];
    SkipBlanks(s);
    ReadToken(s, tooltip);
    widget->SetTooltip(tooltip);

    ui::text_table::Lookup(tooltip);
}

// The tooltip of Button, CheckBox, RadioButton, Image and VariableImage: only when the last token was ended by a comma.
void TooltipIfComma(GuiScript& s, GuiWidget* widget)
{
    if (widget && s.m_text[s.m_cursor - 1] == ',') {
        SkipBlanks(s);
        TooltipAfterComma(s, widget);
    }
}

// After a number that left its delimiter in place (Variable, NumberAdjust): a comma starts the tooltip, a closing parenthesis is eaten.
void TailAfterNumber(GuiScript& s, GuiWidget* widget)
{
    if (!widget) return;
    if (s.m_text[s.m_cursor] == ',') {
        ++s.m_cursor;
        TooltipAfterComma(s, widget);
        return;
    }
    if (s.m_text[s.m_cursor] == ')') ++s.m_cursor;
    SkipBlanks(s);
}

// After a number whose reader already ate the delimiter and the blanks (Slider, ComboBox): `)` ends the command, anything else is a tooltip.
void TailAfterParen(GuiScript& s, GuiWidget* widget)
{
    if (s.m_text[s.m_cursor] == ')') {
        ++s.m_cursor;
        return;
    }
    if (!widget) return;
    SkipBlanks(s);
    TooltipAfterComma(s, widget);
}

// --- the sixteen parsers -----------------------------------------------------------------------------------------------------

bool ParseTga(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char name[kTokenBytes];
    ReadBounds(s, b);
    ReadToken(s, name);
    auto* widget = Construct<ui::GuiTga>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(GuiTgaConstruct(static_cast<ui::GuiTga*>(widget), b.x0, b.y0, b.x1, b.y1, name));
    });
    AddWidget(s, widget);
    return true;
}

bool ParseText(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char key[kTokenBytes];
    int32_t font = 0, color = 0, align = 0;
    ReadBounds(s, b);
    ReadToken(s, key);
    ReadInt(s, font, true);
    ReadInt(s, color, true);
    ReadInt(s, align, true);
    auto* widget = Construct<ui::GuiText>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(TextConstruct(static_cast<ui::GuiText*>(widget), b.x0, b.y0, b.x1, b.y1, key, font,
                static_cast<uint32_t>(color), static_cast<uint32_t>(align)));
    });
    AddWidget(s, widget);
    return true;
}

bool ParseVariable(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char key[kTokenBytes];
    int32_t font = 0, color = 0, align = 0;
    ReadBounds(s, b);
    ReadToken(s, key);
    ReadInt(s, font, true);
    ReadInt(s, color, true);
    ReadInt(s, align, false); // the last number leaves its delimiter for the tail below
    auto* widget = Construct<ui::GuiVariable>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(GuiVariableConstruct(static_cast<ui::GuiVariable*>(widget), b.x0, b.y0, b.x1, b.y1, key, font,
                static_cast<uint32_t>(color), static_cast<uint32_t>(align)));
    });
    AddWidget(s, widget);
    TailAfterNumber(s, widget);
    return true;
}

// Image and VariableImage: when the name was ended by a comma, a tooltip key follows.
template<class T, class Ctor> bool ParsePicture(GuiScript& s, Ctor construct)
{
    if (!Open(s)) return false;
    Bounds b{};
    char name[kTokenBytes];
    ReadBounds(s, b);
    ReadToken(s, name);
    auto* widget = Construct<T>([&](GuiWidget* widget) { return construct(widget, b.x0, b.y0, b.x1, b.y1, name); });
    AddWidget(s, widget);
    TooltipIfComma(s, widget);
    return true;
}
bool ParseImage(GuiScript& s)
{
    return ParsePicture<ui::GuiImage>(s, [](GuiWidget* widget, int x0, int y0, int x1, int y1, const char* name) {
        return static_cast<GuiWidget*>(ImageConstruct(static_cast<ui::GuiImage*>(widget), x0, y0, x1, y1, name));
    });
}
bool ParseVariableImage(GuiScript& s)
{
    return ParsePicture<ui::GuiVariableImage>(s, [](GuiWidget* widget, int x0, int y0, int x1, int y1, const char* name) {
        return static_cast<GuiWidget*>(GuiVariableImageConstruct(static_cast<ui::GuiVariableImage*>(widget), x0, y0, x1, y1, name));
    });
}

bool ParseIcon(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char name[kTokenBytes];
    int32_t frame = 0;
    ReadBounds(s, b);
    ReadToken(s, name);
    ReadInt(s, frame, true);
    auto* widget = Construct<ui::GuiImage>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(ImageConstruct(static_cast<ui::GuiImage*>(widget), b.x0, b.y0, b.x1, b.y1, name));
    });
    // The frame goes in before AddWidget, so a parent that reacts to AddChild already sees it.
    if (widget) GuiImageSetFrame(static_cast<ui::GuiImage*>(widget), frame);
    AddWidget(s, widget);
    return true;
}

bool ParseBar(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char key[kTokenBytes];
    float first = 0, second = 0;
    int32_t flags = 0;
    ReadBounds(s, b);
    ReadFloat(s, first);
    ReadFloat(s, second);
    ReadToken(s, key);
    ReadInt(s, flags, true);
    auto* widget = Construct<ui::GuiBar>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(
                GuiBarConstruct(static_cast<ui::GuiBar*>(widget), b.x0, b.y0, b.x1, b.y1, first, second, key, flags));
    });
    AddWidget(s, widget);
    return true;
}

// --- controls and lists ----------------------------------------------------------------------------------------------------

bool ParseButton(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char graphic[kTokenBytes], text[kTokenBytes], function[kTokenBytes], rightFunction[kTokenBytes];
    ReadBounds(s, b);
    ReadToken(s, graphic);
    ReadToken(s, text);
    ReadToken(s, function);
    ReadToken(s, rightFunction);
    auto* widget = Construct<ui::GuiButton>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(button_facade::Construct(
                static_cast<ui::GuiButton*>(widget), b.x0, b.y0, b.x1, b.y1, graphic, text, function, rightFunction));
    });
    AddWidget(s, widget);
    TooltipIfComma(s, widget);
    return true;
}

bool ParseCheckBox(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char graphic[kTokenBytes], variable[kTokenBytes];
    ReadBounds(s, b);
    ReadToken(s, graphic);
    ReadToken(s, variable);
    auto* widget = Construct<ui::GuiCheckBox>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(
                check_box_facade::Construct(static_cast<ui::GuiCheckBox*>(widget), b.x0, b.y0, b.x1, b.y1, graphic, variable));
    });
    AddWidget(s, widget);
    TooltipIfComma(s, widget);
    return true;
}

bool ParseSlider(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char key[kTokenBytes];
    float first = 0, second = 0;
    int32_t style = 0;
    ReadBounds(s, b);
    ReadToken(s, key);
    ReadFloat(s, first);
    ReadFloat(s, second);
    ReadInt(s, style, true);
    auto* widget = Construct<ui::GuiSlider>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(
                slider_facade::Construct(static_cast<ui::GuiSlider*>(widget), b.x0, b.y0, b.x1, b.y1, key, first, second, style));
    });
    AddWidget(s, widget);
    TailAfterParen(s, widget);
    return true;
}

bool ParseComboBox(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char key[kTokenBytes];
    int32_t lines = 0, color = 0, font = 0, style = 0, direction = 0; // script order: DisplayLines, TextColour, Font, Style, Direction
    ReadBounds(s, b);
    ReadToken(s, key);
    ReadInt(s, lines, true);
    ReadInt(s, color, true);
    ReadInt(s, font, true);
    ReadInt(s, style, true);
    ReadInt(s, direction, true);
    // The constructor takes them in another order than the script has them (colour and font first, the line count after).
    auto* widget = Construct<ui::GuiList>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(GuiListConstruct(static_cast<ui::GuiList*>(widget), b.x0, b.y0, b.x1, b.y1, key,
                static_cast<uint32_t>(color), font, lines, style, direction));
    });
    AddWidget(s, widget);
    TailAfterParen(s, widget);
    return true;
}

bool ParseImageList(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char list[kTokenBytes], highlight[kTokenBytes];
    int32_t spacingX = 0, spacingY = 0;
    ReadBounds(s, b);
    ReadInt(s, spacingX, true);
    ReadInt(s, spacingY, true);
    ReadToken(s, list);
    ReadToken(s, highlight);
    auto* widget = Construct<ui::GuiImageList>([&](GuiWidget* widget) {
        return GuiImageListConstruct(static_cast<ui::GuiImageList*>(widget), b.x0, b.y0, b.x1, b.y1, list, highlight, spacingX, spacingY);
    });
    AddWidget(s, widget);
    return true;
}

bool ParseNumberAdjust(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char key[kTokenBytes];
    int32_t mode = 0, font = 0, flag = 0;
    ReadBounds(s, b);
    ReadInt(s, mode, true);
    ReadInt(s, font, true);
    ReadToken(s, key);
    ReadInt(s, flag, false); // the last number leaves its delimiter for the tail
    // The constructor gets `flag != 0` as a bool (its slot has stack garbage above the low byte) and a trailing constant 1.
    auto* widget = Construct<ui::GuiNumberAdjust>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(number_adjust_facade::Construct(
                static_cast<ui::GuiNumberAdjust*>(widget), b.x0, b.y0, b.x1, b.y1, mode, font, key, flag != 0, 1));
    });
    AddWidget(s, widget);
    TailAfterNumber(s, widget);
    return true;
}

// The group becomes the parser's current parent, so the RadioButtons in its braces are its children.
bool ParseRadioGroup(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char variable[kTokenBytes], graphic[kTokenBytes];
    ReadBounds(s, b);
    ReadToken(s, variable);
    ReadToken(s, graphic);
    auto* widget = Construct<ui::GuiRadioGroup>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(
                radio_group_facade::Construct(static_cast<ui::GuiRadioGroup*>(widget), b.x0, b.y0, b.x1, b.y1, variable, graphic));
    });
    AddWidget(s, widget);
    GuiScriptPushParent(s, widget);
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] == '{') ++s.m_cursor; // a missing brace is not an error here
    return true;
}

// Unlike every other widget it skips AddWidget: the flat list gets its node here and the current parent (the group, never a column or
// tab) adopts it, with no last-widget or ID update.
bool ParseRadioButton(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    char graphic[kTokenBytes], text[kTokenBytes];
    int32_t font = 0, value = 0;
    ReadBounds(s, b);
    ReadToken(s, graphic);
    ReadInt(s, font, true);
    ReadInt(s, value, true);
    ReadToken(s, text);
    auto* widget = Construct<ui::GuiRadioButton>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(radio_button_facade::Construct(
                static_cast<ui::GuiRadioButton*>(widget), b.x0, b.y0, b.x1, b.y1, graphic, text, font, value));
    });
    auto* node = GuiListNodeConstruct(new ui::GuiScriptNode(), widget);
    if (s.m_widgetTail)
        s.m_widgetTail->m_next = node;
    else
        s.m_widgets = node;
    s.m_widgetTail = node;
    if (s.m_parent) s.m_parent->AddChild(widget);
    TooltipIfComma(s, widget);
    return true;
}

bool ParseListBox(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds outer{}, inner{};
    char key[kTokenBytes];
    int32_t first = 0, second = 0;
    ReadBounds(s, outer);
    ReadBounds(s, inner);
    ReadToken(s, key);
    ReadInt(s, first, true);
    ReadInt(s, second, true);
    auto* widget = Construct<ui::GuiListBox>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(listbox::Construct(static_cast<ui::GuiListBox*>(widget), outer.x0, outer.y0, outer.x1, outer.y1,
                inner.x0, inner.y0, inner.x1, inner.y1, key, first, second));
    });
    AddWidget(s, widget);
    return true;
}

// --- containers -------------------------------------------------------------------------------------------------------------

// Containers read their rectangle without a parent (absolute coordinates, no clipping check).
void ReadRootBounds(GuiScript& s, Bounds& out)
{
    GuiScriptReadBounds(s, out, nullptr);
}
void AddDialog(GuiScript& s, GuiWidget* widget)
{
    GuiScriptAddWidget(s, widget, true);
}
void PushParent(GuiScript& s, GuiWidget* widget)
{
    GuiScriptPushParent(s, widget);
}

constexpr const char* kTabNoBrace = "GUI_Parse::process_tab - Script Error \r\nNo open brace at file offset - %u";
constexpr const char* kTabNoClose = "GUI_Parse::process_tab - Script Error \r\nNo close symbol at file offset - %u";
constexpr const char* kMainNoBrace = "GUIManager::process_main - Script Error \r\nNo open brace found at file offset - %u";
constexpr const char* kDialogNoBrace = "GUIManager::process_dialog_box - Script Error \r\nNo open brace found at file offset - %u";
constexpr const char* kDefineNoBrace = "GUIManager::process_define - Script Error \r\nNo open bracket found at file offset - %u";

bool ScriptError(GuiScript& s, const char* format)
{
    game::FatalError(format, s.m_cursor);
    return true;
}

// `<TAB name(rect, graphic [, tooltip])> {`: the parser starts on the `(`. The tab becomes m_tab only once its brace is read, so the
// widgets inside attach to it; the rectangle is absolute.
bool ParseTab(GuiScript& s)
{
    if (s.m_text[s.m_cursor] != '(') return false;
    ++s.m_cursor;
    Bounds b{};
    char graphic[kTokenBytes];
    ReadRootBounds(s, b);
    ReadToken(s, graphic);
    auto* widget = Construct<ui::GuiTab>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(container::ConstructTab(static_cast<ui::GuiTab*>(widget), b.x0, b.y0, b.x1, b.y1, graphic));
    });
    AddWidget(s, widget);
    TooltipIfComma(s, widget);
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '>') return ScriptError(s, kTabNoClose);
    ++s.m_cursor;
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '{') return ScriptError(s, kTabNoBrace);
    ++s.m_cursor;
    s.m_tab = widget;
    return true;
}

bool ParseMainItemIf(GuiScript& s, bool build)
{
    if (!build) {
        SkipBlanks(s);
        if (s.m_text[s.m_cursor] != '{') return true;
        ++s.m_cursor;
        int depth = 1;
        do {
            const char c = s.m_text[s.m_cursor];
            if (c == '{') ++depth;
            if (c == '}') --depth;
            ++s.m_cursor;
        } while (depth != 0);
        return true;
    }
    auto* widget = Construct<ui::GuiMainItem>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(container::ConstructMainItem(static_cast<ui::GuiMainItem*>(widget), 0, 0, 0, 0, 0));
    });
    s.m_manager->m_mainItem = widget;
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '{') return ScriptError(s, kMainNoBrace);
    PushParent(s, widget);
    ++s.m_cursor;
    return true;
}
bool ParseMainItem(GuiScript& s)
{
    return ParseMainItemIf(s, static_cast<uint32_t>(game::GetRenderer()->m_width) >= 800);
}
bool ParseMainItem640(GuiScript& s)
{
    return ParseMainItemIf(s, game::GetRenderer()->m_width == 640);
}

// `DialogItem NAME(rect, background, type, draggable) {`: the ID counter moves before AddWidget, which also lists the dialog.
bool ParseDialogItem(GuiScript& s)
{
    const uint32_t start = s.m_cursor;
    SkipBlanks(s);
    char name[kTokenBytes], background[kTokenBytes];
    ReadToken(s, name);
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '(') {
        s.m_cursor = start;
        return false;
    }
    ++s.m_cursor;
    Bounds b{};
    int32_t type = 0, draggable = 0;
    ReadRootBounds(s, b);
    ReadToken(s, background);
    ReadInt(s, type, true);
    ReadInt(s, draggable, true);
    auto* widget = Construct<ui::GuiDialog>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(container::ConstructDialog(
                static_cast<ui::GuiDialog*>(widget), name, b.x0, b.y0, b.x1, b.y1, background, type, draggable != 0));
    });
    ++s.m_nextId;
    AddDialog(s, widget);
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '{') return ScriptError(s, kDialogNoBrace);
    PushParent(s, widget);
    ++s.m_cursor;
    return true;
}

// `Define NAME {`: a template container. Besides the flat list and the parent chain it goes to the definitions list (singly linked,
// the tail found by walking), which Parse clears without destroying the widgets at the end.
bool ParseDefine(GuiScript& s)
{
    SkipBlanks(s);
    char name[kTokenBytes];
    ReadToken(s, name);
    auto* widget = Construct<ui::GuiDefine>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(container::ConstructDefine(static_cast<ui::GuiDefine*>(widget), name));
    });
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '{') return ScriptError(s, kDefineNoBrace);
    ++s.m_cursor;
    AddWidget(s, widget);
    PushParent(s, widget);
    auto* node = GuiListNodeConstruct(new ui::GuiScriptNode(), widget);
    if (!s.m_defines) {
        s.m_defines = node;
        return true;
    }
    ui::GuiScriptNode* tail = s.m_defines;
    while (tail->m_next) tail = tail->m_next;
    tail->m_next = node;
    return true;
}

// `TabWindow(rect, flags) {`: the brace is optional (the window is the parent either way).
bool ParseTabWindow(GuiScript& s)
{
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '(') return false;
    ++s.m_cursor;
    Bounds b{};
    int32_t flags = 0;
    ReadRootBounds(s, b);
    ReadInt(s, flags, true);
    auto* widget = Construct<ui::GuiTabWindow>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(container::ConstructTabWindow(static_cast<ui::GuiTabWindow*>(widget), b.x0, b.y0, b.x1, b.y1));
    });
    AddWidget(s, widget);
    PushParent(s, widget);
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] == '{') ++s.m_cursor;
    return true;
}

// --- tables and fonts --------------------------------------------------------------------------------------------------------

constexpr const char* kColumnNoClose = "GUIManager::process_col - Script Error \r\nNo close symbol at file offset - %u";

// `Table(rect, scroll rect, name, rowSize, maxRows, allowSelection) [{]`: both rectangles are read against the current parent. The table
// becomes m_column, so the columns that follow attach to it (AddWidget prefers the column over the tab and the parent).
bool ParseTable(GuiScript& s)
{
    const uint32_t start = s.m_cursor;
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '(') {
        s.m_cursor = start;
        return false;
    }
    ++s.m_cursor;
    Bounds outer{}, scroll{};
    char name[kTokenBytes];
    int32_t rowSize = 0, maxRows = 0, selection = 0;
    ReadBounds(s, outer);
    ReadBounds(s, scroll);
    ReadToken(s, name);
    ReadInt(s, rowSize, true);
    ReadInt(s, maxRows, true);
    ReadInt(s, selection, true);
    auto* widget = Construct<ui::GuiTable>([&](GuiWidget* widget) {
        return static_cast<GuiWidget*>(table::Construct(static_cast<ui::GuiTable*>(widget), outer.x0, outer.y0, outer.x1, outer.y1,
                scroll.x0, scroll.y0, scroll.x1, scroll.y1, name, rowSize, maxRows, selection != 0));
    });
    AddWidget(s, widget);
    s.m_column = widget;
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] == '{') ++s.m_cursor;
    return true;
}

// `<COLn (font, color, name, x0, x1, kind, flag [, tooltip])>`: the column spans [table.x0 + x0, table.x0 + x1] over the table's height
// (the parent is the current table, not the parser's parent). The last number leaves its delimiter in place.
bool ParseColumn(GuiScript& s)
{
    const uint32_t start = s.m_cursor;
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] != '(' || !s.m_column) {
        s.m_cursor = start;
        return false;
    }
    ++s.m_cursor;
    char name[kTokenBytes];
    int32_t font = 0, color = 0, from = 0, to = 0, kind = 0, flag = 0;
    ReadInt(s, font, true);
    ReadInt(s, color, true);
    ReadToken(s, name);
    ReadInt(s, from, true);
    ReadInt(s, to, true);
    ReadInt(s, kind, true);
    ReadInt(s, flag, false);
    SkipBlanks(s);
    int32_t table[4];
    s.m_column->GetBounds(table);
    const Bounds b{static_cast<int32_t>(static_cast<uint32_t>(table[0]) + static_cast<uint32_t>(from)), table[1],
            static_cast<int32_t>(static_cast<uint32_t>(table[0]) + static_cast<uint32_t>(to)), table[3]};
    auto* widget = Construct<ui::GuiTableColumn>([&](GuiWidget* widget) {
        return new (static_cast<void*>(static_cast<ui::GuiTableColumn*>(widget)))
                ui::GuiTableColumn(b.x0, b.y0, b.x1, b.y1, name, font, color, flag, kind);
    });
    AddWidget(s, widget);
    if (s.m_text[s.m_cursor] == ',') {
        ++s.m_cursor;
        if (!widget) return true;
        TooltipAfterComma(s, widget);
    } else {
        if (s.m_text[s.m_cursor] == ')') ++s.m_cursor;
        SkipBlanks(s);
    }
    if (s.m_text[s.m_cursor] != '>') return ScriptError(s, kColumnNoClose);
    ++s.m_cursor;
    SkipBlanks(s);
    return true;
}

// `Font(name)`: loads one bitmap font and its text renderer into the manager's next slot. Anything but `(` is silently ignored.
bool ParseFont(GuiScript& s)
{
    if (s.m_text[s.m_cursor] != '(') return true;
    ++s.m_cursor;
    char name[kTokenBytes];
    ReadToken(s, name);
    void* font = ConstructBitmapFont(new ui::BitmapFont(), name);
    s.m_manager->SetFont(static_cast<int>(s.m_fontIndex), font);
    void* renderer = ui::font_text::Construct(new ui::TextRenderer(), font, const_cast<text::Table*>(ui::text_table::Current()));
    s.m_manager->SetTextRenderer(static_cast<int>(s.m_fontIndex), renderer);
    ++s.m_fontIndex;
    return true;
}

// --- ParseCommand ----------------------------------------------------------------------------------------------------------

using LeafParser = void (*)(GuiScript*);
template<int Index> void ParseLeaf(GuiScript* script);

// Handler per command id; null = handled inline (TickerText, StartDebug). The leaves
// are called directly, each counted like its entry point.
constexpr std::array<LeafParser, kStartDebug + 1> kHandlers = {&ParseLeaf<CheckBox>, &ParseLeaf<DialogItem>, &ParseLeaf<MainItem>,
        &ParseLeaf<MainItem640>, &ParseLeaf<TabWindow>, &ParseLeaf<Table>, &ParseLeaf<Define>, &ParseLeaf<Button>, &ParseLeaf<Slider>,
        &ParseLeaf<Text>, &ParseLeaf<Variable>, &ParseLeaf<ComboBox>, &ParseLeaf<VariableImage>, &ParseLeaf<Image>, &ParseLeaf<Font>,
        &ParseLeaf<Tga>, nullptr, &ParseLeaf<ImageList>, &ParseLeaf<Icon>, &ParseLeaf<Bar>, &ParseLeaf<NumberAdjust>,
        &ParseLeaf<RadioGroup>, &ParseLeaf<RadioButton>, &ParseLeaf<ListBox>, nullptr};

bool Ticker(GuiScript& s)
{
    if (!Open(s)) return false;
    Bounds b{};
    ReadBounds(s, b);
    SkipBlanks(s);
    ++s.m_cursor; // the closing parenthesis (whatever stands there)
    s.m_manager->SetTickerBounds(b.x0, b.y0, b.x1, b.y1);
    return true;
}

bool ParseCommand(GuiScript& s, uint32_t command, bool& finished)
{
    finished = false;
    if (command > kStartDebug) return true;
    if (command == kStartDebug) {
        finished = true;
        return true;
    }
    if (command == kTickerText) return Ticker(s);
    kHandlers[command](&s);
    return true;
}

struct Entry {
    const char* name; // for the script error report
    bool (*parse)(GuiScript&);
};
Entry g_entries[LeafCount] = {
        {"GUI script Tga", &ParseTga},
        {"GUI script Text", &ParseText},
        {"GUI script Variable", &ParseVariable},
        {"GUI script VariableImage", &ParseVariableImage},
        {"GUI script Image", &ParseImage},
        {"GUI script Icon", &ParseIcon},
        {"GUI script Bar", &ParseBar},
        {"GUI script Button", &ParseButton},
        {"GUI script CheckBox", &ParseCheckBox},
        {"GUI script Slider", &ParseSlider},
        {"GUI script ComboBox", &ParseComboBox},
        {"GUI script ImageList", &ParseImageList},
        {"GUI script NumberAdjust", &ParseNumberAdjust},
        {"GUI script RadioGroup", &ParseRadioGroup},
        {"GUI script RadioButton", &ParseRadioButton},
        {"GUI script ListBox", &ParseListBox},
        {"GUI script Tab", &ParseTab},
        {"GUI script TabWindow", &ParseTabWindow},
        {"GUI script Define", &ParseDefine},
        {"ui.script-parse-dialog-item", &ParseDialogItem},
        {"GUI script MainItem", &ParseMainItem},
        {"GUI script MainItem640", &ParseMainItem640},
        {"GUI script Column", &ParseColumn},
        {"GUI script Table", &ParseTable},
        {"GUI script Font", &ParseFont},
};

template<int Index> void ParseLeaf(GuiScript* script)
{
    const Entry& entry = g_entries[Index];
    g_counter.Count();

    if (!entry.parse(*script)) fatal::UnhandledInput(entry.name, "an argument list the parser does not handle");
}

} // namespace

namespace gui_script_leaves {
void ParseTab(GuiScript* s)
{
    ParseLeaf<Tab>(s);
}
void ParseColumn(GuiScript* s)
{
    ParseLeaf<Column>(s);
}
bool ParseCommand(GuiScript* s, int32_t command)
{
    g_counter.Count();
    bool finished = false;
    if (!gui::ParseCommand(*s, static_cast<uint32_t>(command), finished))
        fatal::UnhandledInput("ui.script-parse-command", "TickerText without its parenthesis (a script error)");
    return finished;
}
} // namespace gui_script_leaves

} // namespace obl::gui
