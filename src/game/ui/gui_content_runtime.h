#pragma once
// Object browser lists, table headings, row buttons and credits from packs/base/gui/*.json (obl-convert gui-tables,
// docs/architecture/data-formats/converter/index.md). Loaded once before the GUI starts; the views stay valid afterwards.

#include "core/char16.h"
#include "formats/gui_content.h"
#include "core/pointer_width.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_tables_types.h"
#include "game/ui/gui_controls_types.h"

#include <span>
#include <string>

namespace obl::gui::gui_content {

struct ListRow {
    const char* key; // null = separator
    int value;
};

// Lists in format order: guest, building, cell, staff, hire, event.
enum class List { Guest, Building, Cell, Staff, Hire, Event };

bool Load(const Text16& gameDirectory);
bool Loaded();
const std::string& LoadError();

std::span<const ListRow> Rows(List list);
std::span<const ui::GuiTableHeadingKey> Headings();
const char* ButtonResource(int index);
std::span<const ui::GuiCreditEntry> Credits();
std::span<const obl::gui_content::Credit> CreditRows();

} // namespace obl::gui::gui_content
