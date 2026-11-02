#include "gui_tree.h"

#include "test_util.h"

#include "platform/runtime.h"

namespace obl::test::gui {

using namespace layout;
using namespace ui;

bool Readable(const void* pointer, size_t size)
{
    return platform::MemoryReadable(pointer, size);
}

// Strings of the game are NUL-terminated heap copies: accept one only when it is readable up to its terminator.
bool SafeString(const char* text, size_t limit, std::string& out)
{
    out.clear();
    for (size_t i = 0; i < limit; ++i) {
        if (!Readable(text + i, 1)) return false;
        if (!text[i]) return true;
        out.push_back(text[i]);
    }
    return false;
}

namespace {

// `list` is the widget's sentinel field; the table's column list has the same {next, prev, value} node shape.
bool ListChildren(GuiContainerChildNode* const* list, Children& out)
{
    if (!Readable(list, sizeof(*list)) || !*list) return false;
    const GuiContainerChildNode* sentinel = *list;
    if (!Readable(sentinel, sizeof(*sentinel))) return false;
    size_t count = 0;
    for (const auto* node = sentinel->m_next; node != sentinel; node = node->m_next) {
        if (!Readable(node, sizeof(*node)) || ++count > kMaxWidgets) return false;
        const auto* child = View<GuiWidget>(node->m_widget);
        if (!child) return false;
        out.widgets.push_back(child);
    }
    return true;
}

// TabWindow keeps its tabs in a plain singly linked chain {next, widget}.
bool ChainChildren(const GuiTabNode* const* head, Children& out)
{
    if (!Readable(head, sizeof(*head))) return false;
    size_t count = 0;
    for (const GuiTabNode* node = *head; node; node = node->m_next) {
        if (!Readable(node, sizeof(*node)) || ++count > kMaxWidgets) return false;
        const auto* child = View<GuiWidget>(node->m_widget);
        if (!child) return false;
        out.widgets.push_back(child);
    }
    return true;
}

template<class T> const T& As(const GuiWidget& w)
{
    return static_cast<const T&>(w);
}

} // namespace

bool CollectChildren(const GuiWidget& w, Children& out)
{
    switch (static_cast<GuiType>(w.m_type)) {
    case GuiType::Dialog: return ListChildren(&As<GuiDialogData>(w).m_children, out);
    case GuiType::Define: return ListChildren(&As<GuiDefine>(w).m_children, out);
    case GuiType::Tab: return ListChildren(&As<GuiTab>(w).m_children, out);
    case GuiType::RadioGroup: return ListChildren(&As<GuiRadioGroup>(w).m_children, out);
    case GuiType::MainItem: return ListChildren(&As<GuiMainItem>(w).m_children, out);
    case GuiType::TabWindow: return ChainChildren(&As<GuiTabWindow>(w).m_tabs, out);
    case GuiType::Table: {
        const auto& table = As<GuiTable>(w);
        out.header = View<GuiWidget>(table.m_scrollBar);
        return ListChildren(reinterpret_cast<GuiContainerChildNode* const*>(&table.m_columns), out);
    }
    default: return true; // leaf (columns keep their cells in vectors that change with the data: not part of the tree)
    }
}

bool ChainWidgets(const GuiManagerWidgetNode* head, std::vector<const GuiWidget*>& out)
{
    size_t count = 0;
    for (const GuiManagerWidgetNode* node = head; node; node = node->m_next) {
        if (!Readable(node, sizeof(*node)) || ++count > kMaxWidgets) return false;
        const auto* widget = View<GuiWidget>(node->m_widget);
        if (!widget) return false;
        out.push_back(widget);
    }
    return true;
}

const GuiManager* Manager()
{
    return game::GetRenderer() ? View<GuiManager>(game::GetGuiManager()) : nullptr;
}

} // namespace obl::test::gui
