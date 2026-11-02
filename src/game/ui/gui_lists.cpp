#include "core/char16.h"
#include "game/game/host_text.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/sound_api.h"
#include "game/ui/renderer_api.h"
#include "game/ui/text_renderer.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_widgets_image_bar.h"
#include "game/game/game_api.h"
#include "game/ui/gui_containers.h"
#include "game/ui/gui_controls.h"
#include "game/ui/gui_data.h"
#include "game/map/path_queries.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"
#include "game/ui/text_table.h"
#include "game/world/world_params.h"
#include "game/objects/building_events_state.h"
#include "game/ui/gui_lists.h"
#include "game/ui/gui_tooltip.h"

#include "core/code_page.h"
#include "game/game/command_loop.h"
#include "game/ui/input.h"
#include "game/ui/gui_trivial_slots.h"
#include "game/game/game_accessors.h"
#include "game/game/startup_tables.h"
#include "game/ui/gui_combo.h"
#include "game/ui/gui_image_list.h"
#include "game/ui/gui_image_list_widget.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_lists_populate.h"
#include "game/ui/gui_content_runtime.h"
#include "game/ui/gui_definitions.h"
#include "game/ui/resolutions.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "game/ui/renderer_types.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <bit>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>

// The list box and the combo box list (GuiList): scrolling, row layout, forwarding to the cells and the scroll bar,
// mouse, row buttons, the combo box and its populate/selection logic. The logic is src/game/ui/gui_lists.h and gui_combo.h; this file adds
// the game side (virtual calls, gui_widgets::GetBounds/SetPosition, input).
namespace obl::gui {
namespace {

using Widget = ui::GuiWidget;
using ListBox = ui::GuiListBoxData;
using Cell = ui::GuiListRowCell;
using Scroll = ui::GuiScrollBar;
using List = ui::GuiListData;
using ListItem = ui::GuiListItem;

// Call counters (`call-report <name>`).
test::CallCounter g_counter("ui.gui-lists");
test::CallCounter g_comboCounter("ui.gui-combo");

// Thin wrappers over the game functions the lists call; GameEnv below goes through them.
uint8_t CallKeyHeld(void* input, int scancode)
{
    return InputKeyHeld(*static_cast<const ui::Input*>(input), scancode) ? 1 : 0;
}

void CallSetTopRow(Scroll* bar, uint32_t top)
{
    scrollbar_facade::SetTopRow(bar, top);
}

void __cdecl CallFree(void* block)
{
    ::operator delete(block);
}

void* __cdecl CallNew(size_t size)
{
    return ::operator new(size);
}

void __cdecl CallDeallocWords(void* block, uint32_t)
{
    ::operator delete(block);
}

void __cdecl CallDeallocSmall(void* block, uint32_t)
{
    ::operator delete(block);
}

void CallBaseSetVisible(Widget* w, uint32_t visible)
{
    w->SetVisibleBase(visible);
}

void CallSetItemCount(Scroll* bar, uint32_t count)
{
    scrollbar_facade::SetItemCount(bar, count);
}

Scroll* CallConstructScroll(Scroll* memory, int x0, int y0, int x1, int y1, uint32_t top, uint32_t count, uint32_t visible, uint32_t id)
{
    return scrollbar_facade::Construct(memory, x0, y0, x1, y1, top, count, visible, id);
}

uint32_t CallPackColor(void* renderer, uint32_t r, uint32_t g, uint32_t b)
{
    return obl::ui::renderer::PackColor(renderer, r, g, b);
}

void CallFillRect(void* renderer, const int32_t* rect, uint32_t color)
{
    obl::ui::renderer::FillRect(renderer, rect, color);
}

void CallSetFrame(Widget* w, int frame)
{
    GuiImageSetFrame(static_cast<ui::GuiImage*>(w), frame);
}

void* __cdecl CallAllocateWords(size_t bytes)
{
    return ::operator new(bytes);
}

Widget* ConstructImageWidget(void* memory, int x0, int y0, int x1, int y1, const char* graphic)
{
    return static_cast<Widget*>(ImageConstruct(static_cast<ui::GuiImage*>(memory), x0, y0, x1, y1, graphic));
}

Widget* CallConstructButton(
        void* memory, int x0, int y0, int x1, int y1, const char* graphic, const char* text, const char* left, const char* right)
{
    return button_facade::Construct(static_cast<ui::GuiButton*>(memory), x0, y0, x1, y1, graphic, text, left, right);
}

void CallTruncate(void* renderer, Char16* text, int width)
{
    ui::font_text::TruncateWithEllipsis(renderer, text, width);
}

Widget* CallConstructText(void* memory, int x0, int y0, int x1, int y1, const Char16* text, int font, uint32_t color, uint32_t flags)
{
    return static_cast<Widget*>(GuiTextConstructLiteral(static_cast<ui::GuiText*>(memory), x0, y0, x1, y1, text, font, color, flags));
}

ui::SpriteInstance* CallCreateSprite(void* bank, const char* name)
{
    return (static_cast<ui::SpriteBank*>(bank))->CreateInstance(name);
}

void CallReleaseSprite(void* bank, ui::SpriteInstance* sprite)
{
    (static_cast<ui::SpriteBank*>(bank))->ReleaseInstance(sprite);
}

Widget* CallConstructMainItem(void* memory, int x0, int y0, int x1, int y1, int variant)
{
    return static_cast<Widget*>(container::ConstructMainItem(static_cast<ui::GuiMainItem*>(memory), x0, y0, x1, y1, variant));
}

uint8_t CallTrackTooltip(Widget* w, int x, int y, int state)
{
    return w->TrackTooltip(x, y, state);
}

void CallSetContext(void* manager, int index, int value)
{
    static_cast<ui::GuiManager*>(manager)->SetContext(index, value);
}

// The drive's text table: "none" has no text (the item then keeps a null string), everything else answers a fixed wide string.
const Char16* CallLookup(void* table, const char* key)
{
    return ui::text_table::Lookup(static_cast<const text::Table*>(table), key);
}

void CallSelected(List* list, ListItem* item)
{
    GuiListOnItemSelected(list, item);
}

void CallTextAlign(void* renderer, int align)
{
    ui::font_text::SetAlign(renderer, align);
}

void CallTextOutlined(void* renderer, uint32_t outlined)
{
    ui::font_text::SetOutlined(renderer, outlined);
}

void CallTextColor(void* renderer, uint32_t color)
{
    ui::font_text::SetColor(renderer, color);
}

void CallTextRect(void* renderer, int x, int y, int w, int h)
{
    ui::font_text::SetRect(renderer, x, y, w, h);
}

void CallTextDraw(void* renderer, int x, int y, const Char16* text)
{
    ui::font_text::DrawWrapped(renderer, x, y, text);
}

void CallFillRectWH(void* renderer, const int32_t* rect, uint32_t color)
{
    obl::ui::renderer::FillRectBySize(renderer, rect, color);
}

// The game side of gui_lists.h. Virtual slots are the common widget interface.
struct GameEnv {
    const int32_t* Bounds(Widget& w, int32_t* out) { return w.GetBounds(out); }
    void SetPosition(Widget& w, int x, int y) { w.SetPosition(x, y); }
    void SetTopRow(ui::GuiScrollBarData& bar, uint32_t top) { CallSetTopRow(static_cast<Scroll*>(&bar), top); }
    void Update(Widget& w, uint32_t dt) { w.Update(std::bit_cast<float>(dt)); }
    void Draw(Widget& w) { w.Draw(); }
    void Invalidate(Widget& w) { w.Invalidate(); }
    void Translate(Widget& w, int dx, int dy) { w.Translate(dx, dy); }
    uint8_t HitTest(Widget& w, int x, int y) { return w.HitTest(x, y); }
    uint8_t OnMouseWheel(Widget& w, int x, int y, int delta) { return w.MouseWheel(x, y, delta); }
    uint8_t OnMouseLeft(Widget& w, int x, int y, int state) { return w.MouseLeft(x, y, state); }
    uint8_t IsClicked(Widget& w) { return w.WasClicked(); }

    void BaseInvalidate(Widget& w) { w.InvalidateBase(); }
    void BaseTranslate(Widget& w, int dx, int dy) { w.TranslateBase(dx, dy); }
    bool KeyHeld(int scancode) { return CallKeyHeld(game::GetInput(), scancode) != 0; }
    void SelectRow(ListBox& list, uint32_t row, int replace) { listbox::SelectRow(&list, row, replace); }

    void BaseSetVisible(Widget& w, uint32_t visible) { CallBaseSetVisible(&w, visible); }
    void* Alloc(size_t size) { return CallNew(size); }
    template<class T> T* NewRecord() { return NewGuiRecord<T>(); }
    void* AllocWords(size_t bytes, size_t objectBytes) { return objectBytes > 0x80 ? CallNew(bytes) : CallAllocateWords(bytes); }
    Widget* ConstructImage(void* m, int x, int y, const char* graphic) { return ConstructImageWidget(m, x, y, x, y, graphic); }
    Widget* ConstructButton(void* m, int x, int y, const char* graphic)
    {
        return CallConstructButton(m, x, y, x, y, graphic, "None", "NONE", "NONE");
    }
    Widget* ConstructText(void* m, int x0, int y0, int x1, int y1, const Char16* text, int font, uint32_t color, uint32_t flags)
    {
        return CallConstructText(m, x0, y0, x1, y1, text, font, color, flags);
    }
    void Widen(const char* text, Char16* out) { code_page::Widen(text, out, std::strlen(text) + 1); }
    void Truncate(int font, Char16* text, int width) { CallTruncate(game::GetGuiManager()->m_textRenderers[font], text, width); }
    void UpdateScrollBar(ListBox& list) { listbox::UpdateScrollBar(&list); }
    void Free(void* block) { CallFree(block); }
    void DeallocateWords(void* block, uint32_t bytes) { CallDeallocWords(block, bytes); }
    void DeallocateSmall(void* block, uint32_t bytes) { CallDeallocSmall(block, bytes); }
    void DeleteWidget(Widget& w) { gui::DestroyWidget(w); }
    void SetVisible(Widget& w, uint32_t visible) { w.SetVisible(visible); }
    void DrawOverlay(Widget& w) { w.DrawOverlay(); }
    void SetItemCount(ui::GuiScrollBarData& bar, uint32_t count) { CallSetItemCount(static_cast<Scroll*>(&bar), count); }
    Scroll* ConstructScroll(void* memory, int x0, int y0, int x1, int y1, uint32_t top, uint32_t count, uint32_t visible, uint32_t id)
    {
        return CallConstructScroll(static_cast<Scroll*>(memory), x0, y0, x1, y1, top, count, visible, id);
    }
    void SetFrame(Widget& w, int frame) { CallSetFrame(&w, frame); }
    void FillSelection(const int32_t* rect)
    {
        void* renderer = game::GetRenderer();
        const uint32_t color = CallPackColor(renderer, 0x1a, 0xdb, 0x7b);
        CallFillRect(renderer, rect, color);
    }

    void SetOrigin(Widget& w, int dx, int dy) { w.SetOrigin(dx, dy); }
    uint8_t IsHovered(Widget& w) { return w.IsHovered(); }
    uint8_t WasClicked(Widget& w) { return IsClicked(w); }
    void BaseSetOrigin(Widget& w, int dx, int dy) { w.SetOriginBase(dx, dy); }
    void TrackTooltip(Widget& w, int x, int y, int state) { CallTrackTooltip(&w, x, y, state); }
    void SetContext(int index, int value) { CallSetContext(game::GetGuiManager(), index, value); }
    const Char16* TextLookup(const char* key) { return CallLookup(const_cast<text::Table*>(ui::text_table::Current()), key); }
    void OnItemSelected(List& list, ListItem* item) { CallSelected(&list, item); }
    int32_t LineSize(int font)
    {
        return static_cast<int32_t>(static_cast<const ui::BitmapFont*>(game::GetGuiManager()->m_fonts[font])->m_size);
    }
    const char* SpriteName(int style) { return game::GuiListSpriteNames()[style]; }
    ui::SpriteInstance* CreateSprite(const char* name) { return CallCreateSprite(ui::GetSpriteBank(), name); }
    void ReleaseSprite(ui::SpriteInstance* sprite) { CallReleaseSprite(ui::GetSpriteBank(), sprite); }
    const char* NoText() { return gui_data::kButtonNoText; }
    const char* NoAction() { return gui_data::kButtonNoAction; }
    Widget* ConstructButton(
            void* m, int x0, int y0, int x1, int y1, const char* graphic, const char* text, const char* left, const char* right)
    {
        return CallConstructButton(m, x0, y0, x1, y1, graphic, text, left, right);
    }
    Widget* ConstructMainItem(void* m, int x0, int y0, int x1, int y1, int variant)
    {
        return CallConstructMainItem(m, x0, y0, x1, y1, variant);
    }
    void* FontRenderer(int font) { return game::GetGuiManager()->m_textRenderers[font]; }
    void TextAlign(int font, int align) { CallTextAlign(FontRenderer(font), align); }
    void TextOutlined(int font, uint32_t outlined) { CallTextOutlined(FontRenderer(font), outlined); }
    void TextColor(int font, uint32_t color) { CallTextColor(FontRenderer(font), color); }
    void TextRect(int font, int x, int y, int w, int h) { CallTextRect(FontRenderer(font), x, y, w, h); }
    void TextDraw(int font, int x, int y, const Char16* text) { CallTextDraw(FontRenderer(font), x, y, text); }
    void FillHover(const int32_t* rect)
    {
        void* renderer = game::GetRenderer();
        const uint32_t color = CallPackColor(renderer, 0x4a, 0x61, 0xc9);
        CallFillRect(renderer, rect, color);
    }
    void FillSeparator(const int32_t* rect)
    {
        void* renderer = game::GetRenderer();
        const uint32_t color = CallPackColor(renderer, 0x7a, 0x98, 0xfa);
        CallFillRectWH(renderer, rect, color);
    }
};

} // namespace

namespace gui_lists_slots {
// The step travels as bits so that children see exactly what the caller pushed.

} // namespace gui_lists_slots

namespace {} // namespace

ui::GuiList* GuiListConstruct(ui::GuiList* list, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* name, uint32_t color,
        int32_t font, int32_t lines, int32_t style, int32_t direction)
{
    return new (static_cast<void*>(list)) ui::GuiList(x0, y0, x1, y1, name, color, font, lines, style, direction);
}

namespace {

#include "game/ui/gui_lists_populate.inl"
#include "game/ui/gui_image_list.inl"

} // namespace

ui::GuiImageList* GuiImageListConstruct(ui::GuiImageList* list, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* name,
        const char* highlight, int32_t sx, int32_t sy)
{
    return new (static_cast<void*>(list)) ui::GuiImageList(x0, y0, x1, y1, name, highlight, sx, sy);
}

void GuiListAddItem(List* list, const char* key, int32_t value)
{
    g_comboCounter.Count();
    GameEnv env;
    gui_combo::AddItem(*list, key, value, env);
}
void GuiListClearItems(List* list)
{
    g_comboCounter.Count();
    GameEnv env;
    gui_combo::ClearItems(*list, env);
}

// An extended resolution (game/ui/resolutions.h) goes straight to the GUI manager's resolution request.
static bool SelectExtendedResolution(int height)
{
    int width = 0;
    if (!ui::FindExtendedResolution(height, &width)) return false;
    game::GetGuiManager()->RequestResolution(width, height);
    return true;
}

void GuiListOnItemSelected(List* list, ListItem* item)
{
    g_comboCounter.Count();

    if (list->m_kind == 1 && SelectExtendedResolution(item->m_value)) return;
    PopulateEnv env;
    gui_populate::OnItemSelected(*list, *item, env);
}

namespace listbox {

// Lifecycle and the row API. Each facade counts once, then runs the pure logic with the game side (GameEnv).
ui::GuiListBox* Construct(ui::GuiListBox* list, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t barX0, int32_t barY0, int32_t barX1,
        int32_t barY1, const char* name, int32_t rowHeight, int32_t visibleRows)
{
    g_counter.Count();
    return new (static_cast<void*>(list)) ui::GuiListBox(x0, y0, x1, y1, barX0, barY0, barX1, barY1, name, rowHeight, visibleRows);
}
void ClearRows(ui::GuiListBoxData* list)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::ClearRows(*list, env);
}
void ClearSelection(ui::GuiListBoxData* list)
{
    g_counter.Count();
    gui_lists::ClearSelection(*list);
}
int32_t NextSelectedRow(ui::GuiListBoxData* list)
{
    g_counter.Count();
    return gui_lists::NextSelectedRow(*list);
}
void AddImage(ui::GuiListBoxData* list, int32_t row, int32_t offset, const char* graphic)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::AddImage(*list, row, offset, graphic, env);
}
void SetRowButton(ui::GuiListBoxData* list, int32_t row, int32_t offset, const char* graphic, int32_t value)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::SetRowButton(*list, row, offset, graphic, value, env);
}
void AddText(ui::GuiListBoxData* list, int32_t row, int32_t offset, int32_t width, uint32_t unused, uint32_t flags, int32_t font,
        uint32_t color, const Char16* text, uint32_t newRow)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::AddText(*list, row, offset, width, unused, flags, font, color, text, newRow, env);
}
void AddTextA(ui::GuiListBoxData* list, int32_t row, int32_t offset, int32_t width, uint32_t unused, uint32_t flags, int32_t font,
        uint32_t color, const char* text, uint32_t newRow)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::AddText(*list, row, offset, width, unused, flags, font, color, text, newRow, env);
}
uint8_t PollRowButton(ui::GuiListBoxData* list, int32_t row, int32_t* value)
{
    g_counter.Count();
    GameEnv env;
    return gui_lists::PollRowButton(*list, row, value, env);
}

// Selection and scrolling. SelectRow is also what the logic calls on the game side (GameEnv::SelectRow).
void SelectRow(ui::GuiListBoxData* list, uint32_t row, int32_t replace)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::SelectRow(*list, row, replace, env);
}
void SetScrollTop(ui::GuiListBoxData* list, int32_t top)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::SetScrollTop(*list, top, env);
}
void EnsureRowVisible(ui::GuiListBoxData* list, uint32_t row)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::EnsureRowVisible(*list, row, env);
}
void UpdateScrollBar(ui::GuiListBoxData* list)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::UpdateScrollBar(*list, env);
}

// The list box's overrides of the common widget virtuals (its virtual table calls these, and so do the callers that use the
// facade).
void Update(ui::GuiListBoxData* list, uint32_t dtBits)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::Update(*list, dtBits, env);
}
void Draw(ui::GuiListBoxData* list)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::Draw(*list, env);
}
void Invalidate(ui::GuiListBoxData* list)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::Invalidate(*list, env);
}
void Translate(ui::GuiListBoxData* list, int32_t dx, int32_t dy)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::Translate(*list, dx, dy, env);
}
uint8_t OnMouseLeft(ui::GuiListBoxData* list, int32_t x, int32_t y, int32_t state)
{
    g_counter.Count();
    GameEnv env;
    return gui_lists::OnMouseLeft(*list, x, y, state, env);
}
uint8_t OnMouseWheel(ui::GuiListBoxData* list, int32_t x, int32_t y, int32_t delta)
{
    g_counter.Count();
    GameEnv env;
    return gui_lists::OnMouseWheel(*list, x, y, delta, env);
}
void SetVisible(ui::GuiListBoxData* list, uint32_t visible)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::SetVisible(*list, visible, env);
}
void DrawOverlay(ui::GuiListBoxData* list)
{
    g_counter.Count();
    GameEnv env;
    gui_lists::DrawOverlay(*list, env);
}

} // namespace listbox

bool RegisterGuiLists()
{
    return RegisterPopulateDrivers();
}
} // namespace obl::gui

namespace obl::ui {

GuiListBox::GuiListBox(
        int x0, int y0, int x1, int y1, int barX0, int barY0, int barX1, int barY1, const char* name, int rowHeight, int visibleRows)
    : GuiLeaf(x0, y0, x1, y1)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    const int32_t bar[4] = {barX0, barY0, barX1, barY1};
    gui_lists::Construct(*this, bar, name, rowHeight, visibleRows, env);
}

GuiListBox::~GuiListBox()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_lists::Destruct(*this, env);
}

// The frame time keeps its four bytes on the way to the cells.
void GuiListBox::Update(float dt)
{
    gui::listbox::Update(this, std::bit_cast<uint32_t>(dt));
}

void GuiListBox::Draw()
{
    gui::listbox::Draw(this);
}

void GuiListBox::Invalidate()
{
    gui::listbox::Invalidate(this);
}

void GuiListBox::Translate(int dx, int dy)
{
    gui::listbox::Translate(this, dx, dy);
}

bool GuiListBox::MouseLeft(int x, int y, int state)
{
    return gui::listbox::OnMouseLeft(this, x, y, state) != 0;
}

bool GuiListBox::MouseWheel(int x, int y, int delta)
{
    return gui::listbox::OnMouseWheel(this, x, y, delta) != 0;
}

void GuiListBox::SetVisible(uint32_t visible)
{
    gui::listbox::SetVisible(this, visible);
}

void GuiListBox::DrawOverlay()
{
    gui::listbox::DrawOverlay(this);
}

GuiList::GuiList(int x0, int y0, int x1, int y1, const char* name, uint32_t color, int font, int lines, int style, int direction)
    : GuiLeaf(x0, y0, x1, y1)
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    const int32_t rect[4] = {x0, y0, x1, y1};
    gui_combo::Construct(*this, rect, name, color, font, lines, style, direction, env);
}

GuiList::~GuiList()
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    gui_combo::Destruct(*this, env);
}

void GuiList::Translate(int dx, int dy)
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    gui_combo::Translate(*this, dx, dy, env);
}

void GuiList::SetOrigin(int dx, int dy)
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    gui_combo::SetOrigin(*this, dx, dy, env);
}

void GuiList::SetVisible(uint32_t visible)
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    gui_combo::SetVisible(*this, visible, env);
}

bool GuiList::MouseWheel(int x, int y, int delta)
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    return gui_combo::OnMouseWheel(*this, x, y, delta, env) != 0;
}

bool GuiList::MouseLeft(int x, int y, int state)
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    return gui_combo::OnMouse(*this, x, y, state, env) != 0;
}

void GuiList::Draw()
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    gui_combo::Draw(*this, env);
}

void GuiList::Invalidate()
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    gui_combo::Invalidate(*this, env);
}

void GuiList::DrawOverlay()
{
    gui::g_comboCounter.Count();
    gui::GameEnv env;
    gui_combo::DrawOverlay(*this, env);
}

// The frame time keeps its four bytes on the way to the refill.
void GuiList::Update(float dt)
{
    gui::g_comboCounter.Count();
    gui::PopulateEnv env;
    gui_populate::Populate(*this, std::bit_cast<uint32_t>(dt), env);
}

void GuiImageListData::PopulateBuildings(int32_t category, const char* sprite, const char* locked)
{
    gui::g_imageCalls.Count();
    gui::ImageEnv env;
    gui_image_list::PopulateBuildings(*this, category, sprite, locked, env);
}

void GuiImageListData::PopulateSurfaces()
{
    gui::g_imageCalls.Count();
    gui::ImageEnv env;
    gui_image_list::PopulateSurfaces(*this, env);
}

void GuiImageListData::PopulateScenery(int32_t category, const char* sprite)
{
    gui::g_imageCalls.Count();
    gui::ImageEnv env;
    gui_image_list::PopulateScenery(*this, category, sprite, env);
}

void GuiImageListData::CreateImageWidgets()
{
    gui::g_imageCalls.Count();
    gui::ImageEnv env;
    gui_image_list::CreateImageWidgets(*this, env);
}

void GuiImageListData::RefreshImagesAndCounts()
{
    gui::g_imageCalls.Count();
    gui::ImageEnv env;
    gui_image_list::RefreshImagesAndCounts(*this, env);
}

void GuiImageList::RefreshSelection()
{
    gui::g_imageCalls.Count();
    gui::ImageEnv env;
    gui_image_list::RefreshSelection(*this, env);
}

GuiImageList::GuiImageList(int x0, int y0, int x1, int y1, const char* name, const char* highlight, int sx, int sy)
    : GuiLeaf(x0, y0, x1, y1)
{
    gui::ImageWidgetEnv env;
    gui_image_list::Construct(*this, x0, y0, x1, y1, name, highlight, sx, sy, env);
}

GuiImageList::~GuiImageList()
{
    gui::g_imageCalls.Count();
    gui::ImageWidgetEnv env;
    gui_image_list::Destruct(*this, env);
}

void GuiImageList::Draw()
{
    gui::g_imageCalls.Count();
    gui::ImageWidgetEnv env;
    gui_image_list::Draw(*this, env);
}

void GuiImageList::Invalidate()
{
    gui::g_imageCalls.Count();
    gui::ImageWidgetEnv env;
    gui_image_list::Invalidate(*this, env);
}

void GuiImageList::Update(float dt)
{
    gui::g_imageCalls.Count();
    gui::ImageWidgetEnv env;
    gui_image_list::Update(*this, dt, env);
}

bool GuiImageList::MouseLeft(int x, int y, int state)
{
    gui::g_imageCalls.Count();
    gui::ImageWidgetEnv env;
    return gui_image_list::OnMouseLeft(*this, x, y, state, env) != 0;
}

void GuiImageList::Translate(int dx, int dy)
{
    gui::g_imageCalls.Count();
    gui::ImageWidgetEnv env;
    gui_image_list::Translate(*this, dx, dy, env);
}

void GuiImageList::SetVisible(uint32_t visible)
{
    gui::g_imageCalls.Count();
    gui::ImageWidgetEnv env;
    gui_image_list::SetVisible(*this, visible, env);
}

} // namespace obl::ui
