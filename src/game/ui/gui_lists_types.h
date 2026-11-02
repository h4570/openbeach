#pragma once

#include "core/char16.h"
#include "game/ui/gui_controls_types.h"

#include <cstdint>

namespace obl::ui {

// One cell of a `GuiListBox` (one heap object per cell): a widget placed in a row. `m_widget` type 0 is a button
// (`listbox::SetRowButton`: `m_value` is what `PollRowButton` returns), type 10 an image, anything else text.
struct GuiListRowCell {
    GuiWidget* m_widget; // owned, deleted through its virtual destructor
    int32_t m_row; // row the widget belongs to (it is shown while the row is within the scrolled window)
    uint8_t m_active; // buttons answer the mouse only while set (`ClearRows` clears it on cells it keeps)
    int32_t m_value; // payload of a row button
};

// One entry of a `GuiList`: 8 bytes on the game heap, the text is a second block (null for a separator, value -1).
struct GuiListItem {
    Char16* m_text; // owned, game heap
    int32_t m_value; // what OnItemSelected acts on; -1 marks a separator line
};

struct GuiListData : GuiWidget {
    using GuiWidget::GuiWidget;
    int32_t m_state = 0; // 0 closed, 1 open (any other value makes OnMouse ignore everything)
    int32_t m_kind = 0; // 0..12 by the script name (GuestFilter, ResolutionList, ... CocktailList); unknown names leave it unwritten
    int32_t m_style = 0; // index into g_GuiListSpriteNames (button sprite) and the box variant
    int32_t m_font = 0; // text renderer / font index
    uint32_t m_color = 0; // RGB of the item text
    uint32_t m_selected = 0; // index of the chosen item
    uint32_t m_hover = 0; // index under the cursor while open
    GuiScrollBarData* m_scroll = nullptr; // bar, only while the list is open and has more items than lines
    GuiWidget* m_panel = nullptr; // drop-down background (GuiMainItem variant 2 down / 3 up), null while closed
    GuiWidget* m_box = nullptr; // closed box (GuiMainItem variant 1)
    uint32_t m_scrollTop = 0; // first item of the open window
    uint32_t m_rowHeight = 0; // twice the font's line size
    uint32_t m_lines = 0; // item rows shown at once
    GuiListItem** m_itemsBegin = nullptr;
    GuiListItem** m_itemsEnd = nullptr;
    GuiListItem** m_itemsCap = nullptr;
    GuiWidget* m_button = nullptr; // the arrow button at the right end of the box
    uint8_t m_refresh = 0; // set by the constructor and SetVisible(true): Populate refills the list
    int32_t m_direction = 0; // 0 opens downwards, anything else upwards
};

// gui_lists.cpp
struct GuiList final : GuiLeaf<GuiListData> {
    using GuiLeaf::GuiLeaf;
    GuiList() = default;
    GuiList(int x0, int y0, int x1, int y1, const char* name, uint32_t color, int font, int lines, int style, int direction);
    ~GuiList() override;
    void Update(float dt) override;
    void Draw() override;
    void DrawOverlay() override;
    void Invalidate() override;
    void SetVisible(uint32_t visible) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    bool MouseLeft(int x, int y, int state) override;
    bool MouseWheel(int x, int y, int delta) override;
    const char* ClassName() const override { return "GuiList"; }
};

// One palette entry (the constructor writes only the first, fourth and flag fields, the rest is whatever the heap block held).
struct GuiImageListEntry {
    const char* m_sprite;
    int32_t m_frame; // animation frame / thumbnail index
    char* m_tooltip; // owned, upper-case text key ("BUILD_TIPTEXT_<key>" / "SP_DESC_<key>")
    int32_t m_id; // definition id (building or scenery), -1 until set
    Char16 m_text[0x40]; // counter text (swprintf of the building count), only written for counted entries
    uint32_t m_color;
    uint8_t m_hasCount; // the entry gets a counter label
};

// A counter label drawn over an icon (not a widget).
struct GuiImageListLabel {
    Char16 m_text[0x40]; // copy of the entry's counter text
    int32_t m_font; // renderer index, initialized to zero
    uint32_t m_color;
    int32_t m_x; // icon's left edge + 3
    int32_t m_y; // icon's top edge
    int32_t m_param; // always 50 (the two wide characters '2', 0 written over the last word)
};

// The palette widget's record. Three vectors of pointers: the entries, the icon widgets (GuiImage) made from them, the counter labels.
struct GuiImageListData : GuiWidget {
    using GuiWidget::GuiWidget;
    void PopulateBuildings(int32_t category, const char* sprite, const char* locked);
    void PopulateSurfaces();
    void PopulateScenery(int32_t category, const char* sprite);
    void CreateImageWidgets();
    void RefreshImagesAndCounts();

    int32_t m_kind =
            0; // 0..2 building categories, 3 furniture, 4 surfaces (paths and grass), 5..9 flowers, ornaments, trees, plants, billboards
    int32_t m_spacingX = 0; // gap between two icons of a row
    int32_t m_spacingY = 0; // gap between rows
    void* m_highlight = nullptr; // owned GuiImage selection frame; positioned during Draw
    int32_t m_selected = 0; // chosen entry; -1 after the mode changed, RefreshSelection turns it into 0
    float m_refreshTimer = 0.0f; // seconds since the entries were rebuilt
    uint8_t m_refresh = 0; // periodic rebuild on (the surface and scenery populators switch it off)
    GuiImageListEntry** m_entriesBegin = nullptr;
    GuiImageListEntry** m_entriesEnd = nullptr;
    GuiImageListEntry** m_entriesCap = nullptr;
    GuiWidget** m_imagesBegin = nullptr;
    GuiWidget** m_imagesEnd = nullptr;
    GuiWidget** m_imagesCap = nullptr;
    GuiImageListLabel** m_labelsBegin = nullptr;
    GuiImageListLabel** m_labelsEnd = nullptr;
    GuiImageListLabel** m_labelsCap = nullptr;
};

// gui_lists.cpp
struct GuiImageList final : GuiLeaf<GuiImageListData> {
    using GuiLeaf::GuiLeaf;
    GuiImageList() = default;
    GuiImageList(int x0, int y0, int x1, int y1, const char* name, const char* highlight, int sx, int sy);
    ~GuiImageList() override;
    void Update(float dt) override;
    void Draw() override;
    void Invalidate() override;
    void SetVisible(uint32_t visible) override;
    void RefreshSelection() override;
    void Translate(int dx, int dy) override;
    bool MouseLeft(int x, int y, int state) override;
    const char* ClassName() const override { return "GuiImageList"; }
};

struct GuiSortComparator {
    int32_t key;
    uint8_t reverse;
};

struct GuiListBoxData : GuiWidget {
    using GuiWidget::GuiWidget;
    int32_t m_barX0 = 0, m_barY0 = 0, m_barX1 = 0, m_barY1 = 0; // rectangle of the scroll bar (the constructor's arguments 6..9)
    char* m_name = nullptr; // copy of the script name on the game heap
    uint8_t m_multiSelect = 0; // SelectRow honours its "add to selection" argument only when set
    uint8_t m_enabled = 0; // constructor writes 1; OnMouseLeft ignores the click while 0
    int32_t m_rowHeight = 0; // pixels per row
    int32_t m_visibleRows = 0; // rows that fit (the scroll bar appears above this)
    int32_t m_scrollTop = 0; // first row shown
    uint32_t m_slotCount = 0; // row slots laid out below the top edge (cells of row top+i sit at slot i)
    GuiListRowCell** m_cellsBegin = nullptr; // cell vector
    GuiListRowCell** m_cellsEnd = nullptr;
    GuiListRowCell** m_cellsCap = nullptr;
    int32_t* m_selBegin = nullptr; // selected rows, ascending after SelectRow
    int32_t* m_selEnd = nullptr;
    int32_t* m_selCap = nullptr;
    int32_t m_selCursor = 0; // NextSelectedRow walks the selection with it
    uint32_t m_rowCount = 0; // rows added so far
    GuiScrollBarData* m_scroll = nullptr; // bar or null
    float m_holdTime = 0.0f; // seconds since the button went down on a row
    uint8_t m_holding = 0; // left button held on a row (selection repeat armed)
    uint8_t m_repeat = 0; // set for one frame when the press was released quickly
};

// gui_lists.cpp
struct GuiListBox final : GuiLeaf<GuiListBoxData> {
    using GuiLeaf::GuiLeaf;
    GuiListBox() = default;
    GuiListBox(
            int x0, int y0, int x1, int y1, int barX0, int barY0, int barX1, int barY1, const char* name, int rowHeight, int visibleRows);
    ~GuiListBox() override;
    void Update(float dt) override;
    void Draw() override;
    void DrawOverlay() override;
    void Invalidate() override;
    void SetVisible(uint32_t visible) override;
    void Translate(int dx, int dy) override;
    bool MouseLeft(int x, int y, int state) override;
    bool MouseWheel(int x, int y, int delta) override;
    const char* ClassName() const override { return "GuiListBox"; }
};

// The templates allocate a `Gui*Data` record for the class that is constructed over it.
static_assert(sizeof(GuiListBox) == sizeof(GuiListBoxData) && sizeof(GuiImageList) == sizeof(GuiImageListData) &&
        sizeof(GuiList) == sizeof(GuiListData));

} // namespace obl::ui
