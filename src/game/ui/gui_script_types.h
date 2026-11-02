#pragma once

#include "game/ui/gui_types.h"

#include <cstdint>

namespace obl::ui {

struct GuiManager; // gui_manager_types.h

// Parser lists own the nodes; their widget payloads are borrowed when used as a parent stack.
struct GuiScriptNode {
    GuiScriptNode* m_next;
    GuiWidget* m_widget;
};

struct GuiScript {
    GuiManager* m_manager;
    uint32_t m_cursor;
    const char* m_text;
    GuiWidget* m_parent;
    GuiWidget* m_tab;
    GuiWidget* m_column;
    GuiScriptNode* m_parentStack;
    GuiWidget* m_previousParent;
    GuiWidget* m_lastWidget;
    GuiScriptNode* m_defines;
    GuiScriptNode* m_dialogs;
    GuiScriptNode* m_widgets;
    GuiScriptNode* m_widgetTail;
    GuiScriptNode* m_dialogTail;
    uint32_t m_nextId;
    uint32_t m_fontIndex;
};

} // namespace obl::ui
