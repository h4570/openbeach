#include "core/char16.h"
#include "unit_test.h"
#include "game/ui/gui_records.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
using namespace obl::gui_records;
namespace ui = obl::ui;
namespace {
const obl::Char16 kText[] = u"tooltip";
struct Env {
    std::vector<std::string> events;
    uint32_t measured = 100, wrapped = 140;
    int lines = 3;
    uint32_t width = 800, height = 600;
    void* freed[4]{};
    int freeCount = 0;
    obl::Char16 copy[16]{};
    const obl::Char16* Lookup(const char* key)
    {
        events.push_back(std::string("lookup ") + key);
        return kText;
    }
    uint32_t MeasureWidth(int font, const obl::Char16*)
    {
        events.push_back("measure " + std::to_string(font));
        return measured;
    }
    uint32_t MeasureWrappedWidth(int, const obl::Char16*, uint32_t maxWidth, uint32_t startX)
    {
        events.push_back("wrap " + std::to_string(maxWidth) + " " + std::to_string(startX));
        return wrapped;
    }
    int CountLines(int, const obl::Char16*, uint32_t w, uint32_t startX)
    {
        events.push_back("lines " + std::to_string(w) + " " + std::to_string(startX));
        return lines;
    }
    uint32_t ScreenWidth() { return width; }
    uint32_t ScreenHeight() { return height; }
    void ConstructWidget(ui::GuiTextData*, int x0, int y0, int x1, int y1)
    {
        events.push_back("widget " + std::to_string(x0) + " " + std::to_string(y0) + " " + std::to_string(x1) + " " + std::to_string(y1));
    }
    obl::Char16* CopyText(const obl::Char16*)
    {
        events.push_back("copy");
        return copy;
    }
    void Free(void* block) { freed[freeCount++] = block; }
    void DrawLoadingBar(float progress) { events.push_back("bar " + std::to_string(progress)); }
    void* backing = nullptr;
    void* NewBacking()
    {
        events.push_back("new");
        return backing;
    }
    void DeleteBacking(void* b) { events.push_back(b == backing ? "delete" : "delete?"); }
    void InvalidateBacking(ui::GuiWidget*) { events.push_back("invalidate"); }
    void DrawBacking(ui::GuiWidget*) { events.push_back("backing"); }
    void SetAlign(int f, int v) { events.push_back("align " + std::to_string(f) + " " + std::to_string(v)); }
    void SetOutlined(int f, int v) { events.push_back("outline " + std::to_string(f) + " " + std::to_string(v)); }
    void SetColor(int f, uint32_t v) { events.push_back("color " + std::to_string(f) + " " + std::to_string(v)); }
    void SetRect(int f, int x, int y, int w, int h)
    {
        events.push_back("rect " + std::to_string(f) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " +
                std::to_string(h));
    }
    void DrawWrapped(int f, int x, int y, const obl::Char16*)
    {
        events.push_back("text " + std::to_string(f) + " " + std::to_string(x) + " " + std::to_string(y));
    }
};
struct Fixture {
    ui::GuiTooltip tooltip{};
    ui::GuiWidget backing{};
    Fixture() { tooltip.m_backing = &backing; }
};
} // namespace

OBL_TEST_CASE("tooltip text that fits keeps its width and gets a one-line box")
{
    Env env;
    Fixture f;
    f.tooltip.m_font = 2;
    TooltipSetText(env, f.tooltip, "KEY");
    OBL_CHECK(f.tooltip.m_text == kText);
    OBL_CHECK_EQ(f.tooltip.m_textWidth, 100);
    OBL_CHECK_EQ(f.tooltip.m_textHeight, 15);
    OBL_CHECK_EQ(f.tooltip.m_boxWidth, 110);
    OBL_CHECK_EQ(f.tooltip.m_boxHeight, 21);
    OBL_CHECK_EQ(env.events.size(), 2u);
}

OBL_TEST_CASE("tooltip text wider than 160 wraps at 160 starting at 5 and sizes the box from the line count")
{
    Env env;
    Fixture f;
    env.measured = 161;
    TooltipSetText(env, f.tooltip, "KEY");
    const std::vector<std::string> expected = {"lookup KEY", "measure 0", "wrap 160 5", "lines 150 5"};
    OBL_CHECK(env.events == expected);
    OBL_CHECK_EQ(f.tooltip.m_textWidth, 150);
    OBL_CHECK_EQ(f.tooltip.m_textHeight, 36);
    OBL_CHECK_EQ(f.tooltip.m_boxWidth, 150);
    OBL_CHECK_EQ(f.tooltip.m_boxHeight, 42);
    env.events.clear();
    env.measured = 160; // exactly 160 still fits
    TooltipSetText(env, f.tooltip, "KEY");
    OBL_CHECK_EQ(f.tooltip.m_boxWidth, 170);
}

OBL_TEST_CASE("tooltip show places the box under the pointer and flips or shifts it at the screen edges")
{
    Env env;
    Fixture f;
    f.tooltip.m_boxWidth = 110;
    f.tooltip.m_boxHeight = 21;
    TooltipShow(env, f.tooltip, 100, 200);
    OBL_CHECK_EQ(f.tooltip.m_visible, 1);
    OBL_CHECK_EQ(f.tooltip.m_x, 100);
    OBL_CHECK_EQ(f.tooltip.m_y, 225);
    OBL_CHECK_EQ(f.backing.m_x0, 100);
    OBL_CHECK_EQ(f.backing.m_y0, 225);
    OBL_CHECK_EQ(f.backing.m_x1, 210);
    OBL_CHECK_EQ(f.backing.m_y1, 246);
    // Hidden again: near the bottom it flips above the pointer, near the right edge it is pushed back inside.
    f.tooltip.m_visible = 0;
    TooltipShow(env, f.tooltip, 790, 590);
    OBL_CHECK_EQ(f.tooltip.m_y, 590 - 21 - 2);
    OBL_CHECK_EQ(f.tooltip.m_x, 800 - 110);
    OBL_CHECK_EQ(f.backing.m_x1, 800);
}

OBL_TEST_CASE("tooltip show does nothing while the tooltip is visible and treats negative positions as off-screen")
{
    Env env;
    Fixture f;
    f.tooltip.m_boxWidth = 110;
    f.tooltip.m_boxHeight = 21;
    f.tooltip.m_visible = 1;
    f.tooltip.m_x = 7;
    TooltipShow(env, f.tooltip, 100, 200);
    OBL_CHECK_EQ(f.tooltip.m_x, 7);
    OBL_CHECK_EQ(f.backing.m_x0, 0);
    f.tooltip.m_visible = 0;
    TooltipShow(env, f.tooltip, -5, -100); // x + w = 105 stays inside
    OBL_CHECK_EQ(f.tooltip.m_x, -5);
    OBL_CHECK_EQ(f.tooltip.m_y, -100 - 21 - 2);
    f.tooltip.m_visible = 0;
    TooltipShow(env, f.tooltip, -200, 10); // x + w = -90 is huge unsigned: pushed to the right edge
    OBL_CHECK_EQ(f.tooltip.m_x, 800 - 110);
}

OBL_TEST_CASE("literal text widget owns a copy and fills the leaf fields after the base constructor")
{
    Env env;
    ui::GuiTextData w{};
    w.m_text = reinterpret_cast<const obl::Char16*>(0x55);
    w.m_type = 3;
    auto* result = ConstructLiteralText(env, &w, 1, 2, 3, 4, u"abc", 5, 0xff8000u, 0x41u);
    OBL_CHECK(result == &w);
    const std::vector<std::string> expected = {"widget 1 2 3 4", "copy"};
    OBL_CHECK(env.events == expected);
    OBL_CHECK(w.m_text == nullptr);
    OBL_CHECK(w.m_ownedText == env.copy);
    OBL_CHECK_EQ(w.m_drawOnSurface, 1);
    OBL_CHECK_EQ(w.m_font, 5);
    OBL_CHECK_EQ(w.m_color, 0xff8000u);
    OBL_CHECK_EQ(w.m_alignFlags, 0x41u);
    OBL_CHECK_EQ(w.m_type, 12);
}

OBL_TEST_CASE("image-list entry, label and list item deletes free in the fixed order")
{
    Env env;
    ui::GuiImageListEntry entry{};
    char tooltipText[4]{};
    entry.m_tooltip = tooltipText;
    OBL_CHECK(DeleteImageListEntry(env, &entry, 1) == &entry);
    OBL_CHECK_EQ(env.freeCount, 2);
    OBL_CHECK(env.freed[0] == tooltipText);
    OBL_CHECK(env.freed[1] == &entry);
    Env keep;
    entry.m_tooltip = nullptr;
    DeleteImageListEntry(keep, &entry, 0);
    OBL_CHECK_EQ(keep.freeCount, 0);
    DeleteImageListEntry(keep, &entry, 2); // only bit 0 frees the object
    OBL_CHECK_EQ(keep.freeCount, 0);
    ui::GuiImageListLabel label{};
    Env labels;
    DeleteImageListLabel(labels, &label, 0);
    OBL_CHECK_EQ(labels.freeCount, 0);
    OBL_CHECK(DeleteImageListLabel(labels, &label, 1) == &label);
    OBL_CHECK(labels.freed[0] == &label);
}

OBL_TEST_CASE("list item free is null tolerant and releases the text before the item")
{
    Env env;
    FreeListItem(env, nullptr);
    OBL_CHECK_EQ(env.freeCount, 0);
    ui::GuiListItem separator{};
    separator.m_value = -1;
    FreeListItem(env, &separator);
    OBL_CHECK_EQ(env.freeCount, 1);
    OBL_CHECK(env.freed[0] == &separator);
    Env both;
    obl::Char16 text[2]{};
    ui::GuiListItem item{};
    item.m_text = text;
    FreeListItem(both, &item);
    OBL_CHECK_EQ(both.freeCount, 2);
    OBL_CHECK(both.freed[0] == text);
    OBL_CHECK(both.freed[1] == &item);
}

OBL_TEST_CASE("loading bar progress is forwarded to the renderer")
{
    Env env;
    SetLoadingProgress(env, 0.5f);
    OBL_CHECK_EQ(env.events.size(), 1u);
    OBL_CHECK(env.events[0].rfind("bar 0.5", 0) == 0);
}

OBL_TEST_CASE("tooltip construction hides it, clears the text and stores the unchecked font and the backing")
{
    Env env;
    ui::GuiWidget rect{};
    env.backing = &rect;
    ui::GuiTooltip tooltip;
    std::memset(&tooltip, 0xAA, sizeof tooltip);
    OBL_CHECK(ConstructTooltip(env, &tooltip, 7) == &tooltip);
    OBL_CHECK_EQ(tooltip.m_font, 7);
    OBL_CHECK_EQ(tooltip.m_visible, 0);
    OBL_CHECK(tooltip.m_text == nullptr);
    OBL_CHECK(tooltip.m_backing == &rect);
    env.backing = nullptr;
    ConstructTooltip(env, &tooltip, 1);
    OBL_CHECK(tooltip.m_backing == nullptr);
}

OBL_TEST_CASE("tooltip destruction deletes the backing only when there is one")
{
    Env env;
    Fixture f;
    env.backing = &f.backing;
    DestructTooltip(env, f.tooltip);
    OBL_CHECK(env.events == std::vector<std::string>{"delete"});
    Env none;
    f.tooltip.m_backing = nullptr;
    DestructTooltip(none, f.tooltip);
    OBL_CHECK(none.events.empty());
}

OBL_TEST_CASE("tooltip draw is a no-op while hidden and otherwise draws backing then text in order")
{
    Env env;
    Fixture f;
    f.tooltip.m_font = 3;
    f.tooltip.m_x = 10;
    f.tooltip.m_y = 20;
    f.tooltip.m_textWidth = 30;
    f.tooltip.m_textHeight = 15;
    DrawTooltip(env, f.tooltip);
    OBL_CHECK(env.events.empty());
    f.tooltip.m_visible = 1;
    DrawTooltip(env, f.tooltip);
    const std::vector<std::string> expected = {
            "invalidate", "backing", "align 3 0", "outline 3 0", "color 3 0", "rect 3 10 20 30 15", "text 3 5 3"};
    OBL_CHECK(env.events == expected);
    HideTooltip(f.tooltip);
    OBL_CHECK_EQ(f.tooltip.m_visible, 0);
}

OBL_TEST_CASE("base widget accessors read their flags and the mouse wheel is never consumed")
{
    ui::GuiWidget w{};
    OBL_CHECK(!WidgetIsVisible(w) && !WidgetIsHovered(w) && !WidgetIsHeld(w) && !WidgetWasClicked(w));
    w.m_visible = 1;
    w.m_hovered = 1;
    w.m_held = 1;
    w.m_clicked = 1;
    OBL_CHECK(WidgetIsVisible(w) && WidgetIsHovered(w) && WidgetIsHeld(w) && WidgetWasClicked(w));
    OBL_CHECK(!WidgetOnMouseWheel());
}

OBL_TEST_CASE("dialog entry release detaches before releasing its surface and memory")
{
    struct ReleaseEnv {
        std::vector<int> events;
        ui::GuiDialogData* dialog{};
        void* freed{};
        void DetachDialog(ui::GuiDialogData* value)
        {
            dialog = value;
            events.push_back(1);
        }
        void ReleaseSurface(int value) { events.push_back(value); }
        void Free(void* value)
        {
            freed = value;
            events.push_back(3);
        }
    } env;
    ReleaseDialogEntry(env, nullptr);
    OBL_CHECK(env.events.empty());
    ui::GuiManagerDialogEntry entry{};
    entry.m_dialog = reinterpret_cast<ui::GuiDialogData*>(0x1234);
    entry.m_surface = 42;
    ReleaseDialogEntry(env, &entry);
    OBL_CHECK(env.events == std::vector<int>({1, 42, 3}));
    OBL_CHECK(env.dialog == entry.m_dialog);
    OBL_CHECK(env.freed == &entry);
}

OBL_TEST_CASE("credits draw walks the sentinel list in order without drawing the sentinel")
{
    struct DrawEnv {
        std::vector<ui::GuiWidget*> drawn;
        void DrawCredit(ui::GuiWidget* widget) { drawn.push_back(widget); }
    } env;
    ui::GuiCreditsNode head{};
    head.m_next = &head;
    ui::GuiCredits credits{};
    credits.m_lines = &head;
    DrawCredits(env, credits);
    OBL_CHECK(env.drawn.empty());
    ui::GuiWidget widgets[2]{};
    ui::GuiCreditsLine lines[] = {{&widgets[0], 1}, {&widgets[1], 2}};
    ui::GuiCreditsNode nodes[] = {{nullptr, &head, &lines[0]}, {&head, nullptr, &lines[1]}};
    head.m_next = &nodes[0];
    nodes[0].m_next = &nodes[1];
    DrawCredits(env, credits);
    OBL_CHECK(env.drawn == std::vector<ui::GuiWidget*>({&widgets[0], &widgets[1]}));
}
