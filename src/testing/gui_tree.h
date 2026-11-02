#pragma once

#include "game/ui/gui_containers_types.h"
#include "game/ui/gui_controls_types.h"
#include "game/ui/gui_tables_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"

#include <cstddef>
#include <string>
#include <vector>

// Safe walking of the GUI object graph for gui-dump and the gui-open/-click tools (docs/architecture/data-formats/converter/gui-runtime.md): every pointer is
// checked with platform::MemoryReadable before it is followed and every list is bounded, so a wrong layout fails the command instead of
// crashing the game.
namespace obl::test::gui {

constexpr size_t kMaxWidgets = 1u << 16;
constexpr int kMaxDepth = 64;

bool Readable(const void* pointer, size_t size);
template<class T> const T* View(const void* pointer)
{
    return Readable(pointer, sizeof(T)) ? static_cast<const T*>(pointer) : nullptr;
}

// NUL-terminated game string, accepted only when readable up to its terminator within `limit` bytes.
bool SafeString(const char* text, size_t limit, std::string& out);

// Child widgets of a container by its type (layout notes: docs/architecture/data-formats/converter/gui-runtime.md).
struct Children {
    std::vector<const ui::GuiWidget*> widgets;
    const ui::GuiWidget* header = nullptr; // Table: the header widget
};
bool CollectChildren(const ui::GuiWidget& widget, Children& out);

// Widgets of a script chain (widgets and dialogs): every node readable, bounded; false on a broken chain.
bool ChainWidgets(const ui::GuiManagerWidgetNode* head, std::vector<const ui::GuiWidget*>& out);

// The GuiManager tree view, null before the manager exists.
const ui::GuiManager* Manager();

} // namespace obl::test::gui
