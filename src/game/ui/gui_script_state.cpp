#include "gui_widgets.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_script_state.h"
#include "gui_tooltip.h"
#include "game/ui/gui_script.h"

#include "game/game/game_accessors.h"
#include "core/fatal.h"
#include "game/ui/gui_script_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"

#include <array>
#include <bit>
#include <cstring>
#include <vector>

namespace obl::gui {
namespace {

using ui::GuiScript;
using ui::GuiScriptNode;
using ui::GuiWidget;

// Call counters (`call-report ui.gui-script-state`).
test::CallCounter g_counter("ui.gui-script-state");

GuiScript* Construct(GuiScript* script, ui::GuiManager* manager)
{
    // Also shared by Playlist: the receiver there is only one pointer, not a 0x40-byte parser.
    script->m_manager = manager;
    return script;
}

GuiScriptNode* NewNode(GuiWidget* widget)
{
    return GuiListNodeConstruct(new GuiScriptNode(), widget);
}

void DeleteBorrowedNode(GuiScriptNode* node)
{
    // The widget is borrowed: an empty list node keeps the destruction counters without deleting it.
    node->m_widget = nullptr;
    ui::GuiWidgetListNode borrowed{};
    borrowed.DestroyWidget();
    delete node;
}

void Append(GuiScriptNode*& head, GuiScriptNode*& tail, GuiWidget* widget)
{
    auto* node = NewNode(widget);
    if (tail)
        tail->m_next = node;
    else
        head = node;
    tail = node;
}

void PushParent(GuiScript& script, GuiWidget* widget)
{
    if (!script.m_parent) {
        script.m_parent = script.m_previousParent = widget;
        return;
    }
    script.m_previousParent = script.m_parent;
    auto** link = &script.m_parentStack;
    while (*link) link = &(*link)->m_next;
    *link = NewNode(script.m_parent);
    script.m_parent = widget;
}

void PopParent(GuiScript& script)
{
    if (!script.m_parentStack) {
        script.m_parent = script.m_previousParent = nullptr;
        return;
    }
    GuiScriptNode* previous = nullptr;
    auto* node = script.m_parentStack;
    while (node->m_next) {
        previous = node;
        node = node->m_next;
    }
    script.m_parent = node->m_widget;
    if (previous) {
        previous->m_next = nullptr;
        script.m_previousParent = previous->m_widget;
    } else {
        script.m_parentStack = nullptr;
        script.m_previousParent = nullptr;
    }
    DeleteBorrowedNode(node);
}

void Attach(GuiWidget* parent, GuiWidget* widget)
{
    parent->AddChild(widget);
}

template<class Attacher> void AddWidget(GuiScript& script, GuiWidget* widget, bool dialog, Attacher attach)
{
    if (dialog) Append(script.m_dialogs, script.m_dialogTail, widget);
    Append(script.m_widgets, script.m_widgetTail, widget);
    auto* parent = script.m_column ? script.m_column : script.m_tab ? script.m_tab : script.m_parent;
    if (parent) attach(parent, widget);
    script.m_lastWidget = widget;
    widget->m_id = std::bit_cast<int32_t>(script.m_nextId);
}

void Clear(GuiScriptNode*& head)
{
    while (head) {
        auto* node = head;
        head = node->m_next;
        DeleteBorrowedNode(node);
    }
}

} // namespace

void GuiScriptPushParent(GuiScript& script, GuiWidget* widget)
{
    g_counter.Count();
    PushParent(script, widget);
}

void GuiScriptPopParent(GuiScript& script)
{
    if (!script.m_parent) fatal::UnhandledInput("ui.script-pop-parent", "an unmatched closing brace");
    g_counter.Count();
    PopParent(script);
}

void GuiScriptAddWidget(GuiScript& script, GuiWidget* widget, bool dialog)
{
    g_counter.Count();
    AddWidget(script, widget, dialog, Attach);
}

GuiScript* GuiScriptConstruct(GuiScript* script, ui::GuiManager* manager)
{
    g_counter.Count();
    return Construct(script, manager);
}

GuiScriptNode* GuiScriptDialogs(GuiScript& script)
{
    g_counter.Count();
    return script.m_dialogs;
}

GuiScriptNode* GuiScriptWidgets(GuiScript& script)
{
    g_counter.Count();
    return script.m_widgets;
}

void ReleaseBorrowedList(GuiScriptNode*& head)
{
    Clear(head);
}

} // namespace obl::gui
