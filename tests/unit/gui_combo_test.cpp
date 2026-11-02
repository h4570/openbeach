#include <cstdint>
#include <memory>
// GuiList, the ComboBox (src/game/ui/gui_combo.h): construction, the drop-down opened and closed by OnMouse, the wheel over the closed box, the item
// list and the drawing calls.

#include "core/char16.h"
#include "unit_test.h"

#include "game/ui/gui_combo.h"

#include <cstring>
#include <new>
#include <map>
#include <string>
#include <vector>

using namespace obl::gui_combo;
using obl::ui::GuiMainItemData;
using obl::ui::GuiScrollBarData;
using obl::ui::GuiWidget;

namespace {

constexpr int kSelf = 100, kPanel = 101, kBox = 102, kButton = 103, kBar = 104;

// Widgets are told apart by `m_id`; the environment answers by masks and logs every call in order.
struct FakeEnv {
    std::vector<std::string> log;
    uint32_t hovered = 0, hit = 0, clicked = 0, left = 0, wheel = 0; // bit per widget index (id - 100)
    bool failAlloc = false;
    size_t failSize = 0; // only a request of exactly this size fails
    int boxX0 = 20, boxY0 = 30, boxX1 = 120, boxY1 = 45;
    int frameWidth = 15, frameHeight = 18;
    unsigned char heap[2048]{};
    size_t used = 0;
    GuiScrollBarData madeBar{};
    GuiWidget made{};
    GuiWidget madeButton{}, madeBox{}, madePanel{};
    GuiMainItemData madeItem{};
    obl::ui::SpriteInstance sprite{};
    obl::ui::Sprite spriteData{};
    obl::ui::SpriteAnim* anims[2]{};
    obl::ui::SpriteAnim anim[2]{};
    std::map<const void*, std::string> label;

    FakeEnv()
    {
        anim[1].m_width = static_cast<uint16_t>(frameWidth);
        anim[1].m_height = static_cast<uint16_t>(frameHeight);
        anims[0] = &anim[0];
        anims[1] = &anim[1];
        spriteData.m_anims = anims;
        sprite.m_sprite = &spriteData;
        sprite.m_animIndex = 1;
    }
    static std::string Id(const GuiWidget& w) { return std::to_string(w.m_id); }
    static uint32_t Bit(const GuiWidget& w) { return 1u << (w.m_id - 100); }
    std::string Name(const void* p)
    {
        auto it = label.find(p);
        return it == label.end() ? "?" : it->second;
    }
    void Log(const std::string& s) { log.push_back(s); }

    const int32_t* Bounds(GuiWidget& w, int32_t* out)
    {
        Log("bounds:" + Id(w));
        out[0] = boxX0;
        out[1] = boxY0;
        out[2] = boxX1;
        out[3] = boxY1;
        return out;
    }
    void Invalidate(GuiWidget& w) { Log("invalidate:" + Id(w)); }
    void Draw(GuiWidget& w) { Log("draw:" + Id(w)); }
    void DrawOverlay(GuiWidget& w) { Log("overlay:" + Id(w)); }
    void SetVisible(GuiWidget& w, uint32_t v) { Log("visible:" + Id(w) + "," + std::to_string(v)); }
    void Translate(GuiWidget& w, int dx, int dy) { Log("translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy)); }
    void SetOrigin(GuiWidget& w, int dx, int dy) { Log("origin:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy)); }
    uint8_t IsHovered(GuiWidget& w)
    {
        Log("hovered:" + Id(w));
        return (hovered & Bit(w)) != 0;
    }
    uint8_t HitTest(GuiWidget& w, int x, int y)
    {
        Log("hit:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y));
        return (hit & Bit(w)) ? 3 : 0;
    }
    uint8_t WasClicked(GuiWidget& w)
    {
        Log("clicked:" + Id(w));
        return (clicked & Bit(w)) != 0;
    }
    uint8_t OnMouseWheel(GuiWidget& w, int, int, int delta)
    {
        Log("wheel:" + Id(w) + ":" + std::to_string(delta));
        return (wheel & Bit(w)) ? 0x81 : 0;
    }
    uint8_t OnMouseLeft(GuiWidget& w, int, int, int state)
    {
        Log("left:" + Id(w) + ":" + std::to_string(state));
        return (left & Bit(w)) ? 5 : 0;
    }
    void DeleteWidget(GuiWidget& w) { Log("delete:" + Id(w)); }
    void BaseSetVisible(GuiWidget& w, uint32_t v) { Log("base-visible:" + Id(w) + "," + std::to_string(v)); }
    void BaseInvalidate(GuiWidget& w) { Log("base-invalidate:" + Id(w)); }
    void BaseTranslate(GuiWidget& w, int dx, int dy)
    {
        Log("base-translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void BaseSetOrigin(GuiWidget& w, int dx, int dy) { Log("base-origin:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy)); }
    void TrackTooltip(GuiWidget& w, int x, int y, int state)
    {
        Log("tooltip:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(state));
    }
    void SetContext(int index, int value) { Log("context:" + std::to_string(index) + "," + std::to_string(value)); }
    void OnItemSelected(obl::ui::GuiListData&, obl::ui::GuiListItem* item) { Log("selected:" + std::to_string(item->m_value)); }

    // Heap: the log names what was requested; blocks come from a bump area so that they differ.
    // The GUI records (`new T()` in the game) come from Alloc with the record's size.
    template<class T> T* NewRecord()
    {
        void* memory = Alloc(sizeof(T));
        return memory ? std::construct_at(static_cast<T*>(memory)) : nullptr;
    }
    void* Alloc(size_t n)
    {
        Log("new:" + std::to_string(n));
        if (failAlloc || n == failSize) return nullptr;
        void* block = heap + used;
        used += (n + 7) & ~size_t(7);
        return block;
    }
    void* AllocWords(size_t n, size_t)
    {
        Log("words:" + std::to_string(n));
        void* block = heap + used;
        used += (n + 7) & ~size_t(7);
        return block;
    }
    void Free(void* p) { Log("free:" + Name(p)); }
    void DeallocateSmall(void* p, uint32_t bytes) { Log("small:" + Name(p) + "," + std::to_string(bytes)); }
    const obl::Char16* TextLookup(const char* key)
    {
        Log(std::string("lookup:") + key);
        return std::strcmp(key, "none") == 0 ? nullptr : u"Text";
    }

    // Construction.
    int32_t LineSize(int font)
    {
        Log("line:" + std::to_string(font));
        return 7;
    }
    const char* SpriteName(int style)
    {
        Log("name:" + std::to_string(style));
        return "sprite";
    }
    obl::ui::SpriteInstance* CreateSprite(const char* name)
    {
        Log(std::string("create:") + name);
        return &sprite;
    }
    void ReleaseSprite(obl::ui::SpriteInstance*) { Log("release"); }
    const char* NoText() { return "NoText"; }
    const char* NoAction() { return "NoAction"; }
    GuiWidget* ConstructButton(void*, int x0, int y0, int x1, int y1, const char* graphic, const char* text, const char* l, const char* r)
    {
        Log("button:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1) + ":" +
                graphic + "," + text + "," + l + "," + r);
        madeButton.m_id = kButton;
        return &madeButton;
    }
    GuiWidget* ConstructMainItem(void*, int x0, int y0, int x1, int y1, int variant)
    {
        Log("main:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1) + "," +
                std::to_string(variant));
        GuiWidget* w = variant == 1 ? static_cast<GuiWidget*>(&madeItem) : &madePanel;
        w->m_id = variant == 1 ? kBox : kPanel;
        return w;
    }
    GuiScrollBarData* ConstructScroll(void*, int x0, int y0, int x1, int y1, uint32_t top, uint32_t count, uint32_t visible, uint32_t id)
    {
        Log("scroll:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1) + "," +
                std::to_string(top) + "," + std::to_string(count) + "," + std::to_string(visible) + "," + std::to_string(id));
        madeBar.GuiWidget::m_id = kBar;
        madeBar.m_top = top + 1; // the bar clamps its first row
        return &madeBar;
    }

    // Drawing.
    void TextAlign(int font, int align) { Log("talign:" + std::to_string(font) + "," + std::to_string(align)); }
    void TextOutlined(int font, uint32_t v) { Log("toutline:" + std::to_string(font) + "," + std::to_string(v)); }
    void TextColor(int font, uint32_t c) { Log("tcolor:" + std::to_string(font) + "," + std::to_string(c)); }
    void TextRect(int font, int x, int y, int w, int h)
    {
        Log("trect:" + std::to_string(font) + "," + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                std::to_string(h));
    }
    void TextDraw(int font, int x, int y, const obl::Char16* text)
    {
        Log("tdraw:" + std::to_string(font) + "," + std::to_string(x) + "," + std::to_string(y) + ":" +
                (text ? std::to_string(text[0]) : "null"));
    }
    void FillHover(const int32_t* r)
    {
        Log("hover-fill:" + std::to_string(r[0]) + "," + std::to_string(r[1]) + "," + std::to_string(r[2]) + "," + std::to_string(r[3]));
    }
    void FillSeparator(const int32_t* r)
    {
        Log("line-fill:" + std::to_string(r[0]) + "," + std::to_string(r[1]) + "," + std::to_string(r[2]) + "," + std::to_string(r[3]));
    }
};

std::string Join(const std::vector<std::string>& v)
{
    std::string out;
    for (const auto& s : v) out += s + " ";
    return out;
}

constexpr int kMaxItems = 6;
struct Fixture {
    FakeEnv env;
    obl::ui::GuiListData list{};
    GuiScrollBarData bar{};
    GuiWidget panel{}, box{}, button{};
    obl::ui::GuiListItem items[kMaxItems]{};
    obl::Char16 text[kMaxItems][4]{};
    obl::ui::GuiListItem* vec[kMaxItems + 4]{};

    // `count` items with values 10, 11, ... ; the ones in `separators` (bit mask) are separators.
    explicit Fixture(int count, unsigned separators = 0)
    {
        list.m_id = kSelf;
        panel.m_id = kPanel;
        box.m_id = kBox;
        button.m_id = kButton;
        bar.GuiWidget::m_id = kBar;
        list.m_box = &box;
        list.m_button = &button;
        list.m_rowHeight = 14;
        list.m_lines = 3;
        list.m_font = 2;
        list.m_color = 0x00112233;
        list.m_x0 = 20;
        list.m_y0 = 30;
        list.m_x1 = 120;
        list.m_y1 = 45;
        list.m_itemsBegin = vec;
        list.m_itemsEnd = vec + count;
        list.m_itemsCap = vec + kMaxItems + 4;
        for (int i = 0; i < count; ++i) {
            text[i][0] = static_cast<obl::Char16>(u'A' + i);
            const bool separator = (separators >> i) & 1;
            items[i].m_text = separator ? nullptr : text[i];
            items[i].m_value = separator ? kSeparator : 10 + i;
            vec[i] = &items[i];
        }
    }
    void Open()
    {
        list.m_state = 1;
        list.m_panel = &panel;
    }
};

} // namespace

OBL_TEST_CASE("gui_combo: Construct derives the layout from the font and the sprite, maps the name to a kind and builds button and box")
{
    Fixture f(0);
    obl::ui::GuiListData list{};
    list.m_id = 7;
    list.m_hover = 77;
    list.m_kind = 99;
    list.m_state = 0;
    const int32_t rect[4] = {20, 30, 120, 45};
    List* result = Construct(list, rect, "resolutionlist", 0x00abcdef, 2, 3, 1, -1, f.env);
    OBL_CHECK(result == &list);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"line:2", "name:1", "create:sprite", "release", "new:" + std::to_string(sizeof(obl::ui::GuiButtonData)),
                    "button:105,30,120,48:sprite,NoText,NoAction,NoAction", "new:" + std::to_string(sizeof(obl::ui::GuiMainItemData)),
                    "main:20,30,105,45,1"}));
    OBL_CHECK_EQ(list.m_kind, 1);
    OBL_CHECK_EQ(list.m_style, 1);
    OBL_CHECK_EQ(list.m_font, 2);
    OBL_CHECK_EQ(list.m_color, 0x00abcdefu);
    OBL_CHECK_EQ(list.m_lines, 3u);
    OBL_CHECK_EQ(list.m_rowHeight, 14u);
    OBL_CHECK_EQ(list.m_direction, -1);
    OBL_CHECK_EQ(list.m_refresh, 1);
    OBL_CHECK_EQ(list.m_type, kListType);
    OBL_CHECK_EQ(list.m_selected, 0u);
    OBL_CHECK(list.m_itemsBegin == nullptr && list.m_itemsEnd == nullptr && list.m_itemsCap == nullptr);
    OBL_CHECK(list.m_scroll == nullptr && list.m_panel == nullptr);
    OBL_CHECK(list.m_button == &f.env.madeButton && list.m_box == &f.env.madeItem);
    OBL_CHECK_EQ(f.env.madeItem.m_index, 1);
    OBL_CHECK_EQ(list.m_hover, 77u);
    OBL_CHECK_EQ(list.m_id, 7);
}

OBL_TEST_CASE("gui_combo: Construct leaves the kind alone for an unrecognized name and matches every script name")
{
    for (int kind = 0; kind < kKindCount; ++kind) {
        Fixture f(0);
        obl::ui::GuiListData list{};
        std::string name = kKindNames[kind];
        for (char& c : name) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        const int32_t rect[4] = {0, 0, 50, 20};
        Construct(list, rect, name.c_str(), 0, 0, 1, 0, 0, f.env);
        OBL_CHECK_EQ(list.m_kind, kind);
    }
    for (const char* name : {"", "GuestFilterX", "Guest", "unrecognized"}) {
        Fixture f(0);
        obl::ui::GuiListData list{};
        list.m_kind = 42;
        const int32_t rect[4] = {0, 0, 50, 20};
        Construct(list, rect, name, 0, 0, 1, 0, 0, f.env);
        OBL_CHECK_EQ(list.m_kind, 42);
    }
}

OBL_TEST_CASE("gui_combo: Construct takes the frame of the animation index and wraps the button rectangle")
{
    Fixture f(0);
    f.env.anim[1].m_width = 0xffff;
    f.env.anim[1].m_height = 0xffff;
    obl::ui::GuiListData list{};
    const int32_t rect[4] = {10, 20, 30, 40};
    Construct(list, rect, "x", 0, 0, 1, 0, 0, f.env);
    OBL_CHECK(Join(f.env.log).find("button:-65505,20,30,65555:") != std::string::npos);
}

OBL_TEST_CASE("gui_combo: Construct survives a failing button allocation (the field stays null, the box is still built)")
{
    Fixture f(0);
    f.env.failSize = sizeof(obl::ui::GuiButtonData);
    obl::ui::GuiListData list{};
    const int32_t rect[4] = {10, 20, 30, 40};
    Construct(list, rect, "x", 0, 0, 1, 0, 0, f.env);
    OBL_CHECK(list.m_button == nullptr);
    OBL_CHECK(list.m_box == &f.env.madeItem);
    OBL_CHECK(Join(f.env.log).find("button:") == std::string::npos);
}

OBL_TEST_CASE("gui_combo: Destruct deletes bar and panel, frees the items, clears context 7, hides the button and frees the storage")
{
    Fixture f(2);
    f.list.m_state = 1;
    f.list.m_panel = &f.panel;
    f.list.m_scroll = &f.bar;
    f.env.label = {{&f.items[0], "i0"}, {f.text[0], "t0"}, {&f.items[1], "i1"}, {f.text[1], "t1"}, {f.vec, "vec"}};
    Destruct(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"base-visible:100,0", "delete:104", "delete:101", "invalidate:100", "free:t0", "free:i0", "free:t1", "free:i1",
                    "context:7,0", "visible:103,0", "delete:103", "delete:102", "small:vec," + std::to_string(10 * sizeof(void*))}));
    OBL_CHECK(f.list.m_itemsEnd == f.list.m_itemsBegin);
    OBL_CHECK(f.list.m_scroll == nullptr && f.list.m_panel == nullptr);
    OBL_CHECK_EQ(f.list.m_state, 0);
}

OBL_TEST_CASE("gui_combo: Destruct of a closed list does not invalidate; separators have no text and big storage goes to operator_delete")
{
    Fixture f(3, 0b010);
    f.list.m_itemsCap = f.list.m_itemsBegin + 0x21;
    f.env.label = {{&f.items[0], "i0"}, {f.text[0], "t0"}, {&f.items[1], "i1"}, {&f.items[2], "i2"}, {f.text[2], "t2"}, {f.vec, "vec"}};
    Destruct(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"base-visible:100,0", "free:t0", "free:i0", "free:i1", "free:t2", "free:i2", "context:7,0", "visible:103,0", "delete:103",
                    "delete:102", "free:vec"}));
}

OBL_TEST_CASE("gui_combo: SetVisible passes the value on; showing refills and redraws, hiding closes and empties")
{
    Fixture f(2);
    f.list.m_state = 1;
    f.list.m_panel = &f.panel;
    f.env.label = {{&f.items[0], "i0"}, {f.text[0], "t0"}, {&f.items[1], "i1"}, {f.text[1], "t1"}};
    SetVisible(f.list, 0x101, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"base-visible:100,257", "invalidate:100", "visible:103,257"}));
    OBL_CHECK_EQ(f.list.m_refresh, 1);
    f.env.log.clear();
    SetVisible(f.list, 0x100, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"base-visible:100,256", "delete:101", "invalidate:100", "free:t0", "free:i0", "free:t1", "free:i1", "context:7,0",
                    "visible:103,256"}));
    OBL_CHECK(f.list.m_itemsEnd == f.list.m_itemsBegin);
    OBL_CHECK_EQ(f.list.m_state, 0);
}

OBL_TEST_CASE("gui_combo: AddItem builds the record and the text copy; a separator has none; a key without text leaves it null")
{
    Fixture f(0);
    f.list.m_itemsBegin = f.list.m_itemsEnd = f.list.m_itemsCap = nullptr;
    AddItem(f.list, "key", 5, f.env);
    AddItem(f.list, "none", 6, f.env);
    AddItem(f.list, "any", kSeparator, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"lookup:key", "new:" + std::to_string(sizeof(obl::ui::GuiListItem)), "new:" + std::to_string(5 * sizeof(obl::Char16)),
                    "words:" + std::to_string(1 * sizeof(void*)), "lookup:none", "new:" + std::to_string(sizeof(obl::ui::GuiListItem)),
                    "words:" + std::to_string(2 * sizeof(void*)), "small:?," + std::to_string(1 * sizeof(void*)),
                    "new:" + std::to_string(sizeof(obl::ui::GuiListItem)), "words:" + std::to_string(4 * sizeof(void*)),
                    "small:?," + std::to_string(2 * sizeof(void*))}));
    OBL_REQUIRE(f.list.m_itemsEnd - f.list.m_itemsBegin == 3);
    OBL_CHECK(obl::Text16(f.list.m_itemsBegin[0]->m_text) == u"Text");
    OBL_CHECK_EQ(f.list.m_itemsBegin[0]->m_value, 5);
    OBL_CHECK(f.list.m_itemsBegin[1]->m_text == nullptr);
    OBL_CHECK_EQ(f.list.m_itemsBegin[1]->m_value, 6);
    OBL_CHECK(f.list.m_itemsBegin[2]->m_text == nullptr);
    OBL_CHECK_EQ(f.list.m_itemsBegin[2]->m_value, kSeparator);
}

OBL_TEST_CASE("gui_combo: AddWideItem copies the given text without a lookup; a null text leaves it null")
{
    Fixture f(0);
    f.list.m_itemsBegin = f.list.m_itemsEnd = f.list.m_itemsCap = nullptr;
    AddWideItem(f.list, u"Beach", 3, f.env);
    AddWideItem(f.list, nullptr, 4, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"new:" + std::to_string(sizeof(obl::ui::GuiListItem)), "new:" + std::to_string(6 * sizeof(obl::Char16)),
                    "words:" + std::to_string(1 * sizeof(void*)), "new:" + std::to_string(sizeof(obl::ui::GuiListItem)),
                    "words:" + std::to_string(2 * sizeof(void*)), "small:?," + std::to_string(1 * sizeof(void*))}));
    OBL_REQUIRE(f.list.m_itemsEnd - f.list.m_itemsBegin == 2);
    OBL_CHECK(obl::Text16(f.list.m_itemsBegin[0]->m_text) == u"Beach");
    OBL_CHECK_EQ(f.list.m_itemsBegin[0]->m_value, 3);
    OBL_CHECK(f.list.m_itemsBegin[1]->m_text == nullptr);
    OBL_CHECK_EQ(f.list.m_itemsBegin[1]->m_value, 4);
}

OBL_TEST_CASE("gui_combo: AddItem with a failing allocation stores a null item")
{
    Fixture f(0);
    f.env.failAlloc = true;
    AddItem(f.list, "key", 5, f.env);
    OBL_CHECK(f.list.m_itemsEnd[-1] == nullptr);
    f.env.log.clear();
    AddItem(f.list, "key", kSeparator, f.env);
    OBL_CHECK(f.list.m_itemsEnd[-1] == nullptr);
}

OBL_TEST_CASE("gui_combo: ClearItems frees the items but keeps the storage and the selection")
{
    Fixture f(2);
    f.list.m_selected = 1;
    f.env.label = {{&f.items[0], "i0"}, {f.text[0], "t0"}, {&f.items[1], "i1"}, {f.text[1], "t1"}};
    ClearItems(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"free:t0", "free:i0", "free:t1", "free:i1"}));
    OBL_CHECK(f.list.m_itemsEnd == f.list.m_itemsBegin);
    OBL_CHECK(f.list.m_itemsCap == f.list.m_itemsBegin + kMaxItems + 4);
    OBL_CHECK_EQ(f.list.m_selected, 1u);
}

OBL_TEST_CASE("gui_combo: Translate, SetOrigin and Invalidate reach panel, bar, button and box in order")
{
    Fixture f(1);
    f.list.m_panel = &f.panel;
    f.list.m_scroll = &f.bar;
    Translate(f.list, 3, -4, f.env);
    SetOrigin(f.list, 5, 6, f.env);
    Invalidate(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"base-translate:100:3,-4", "translate:101:3,-4", "translate:104:3,-4", "translate:103:3,-4", "translate:102:3,-4",
                    "base-origin:100:5,6", "origin:101:5,6", "origin:104:5,6", "origin:103:5,6", "origin:102:5,6", "base-invalidate:100",
                    "invalidate:103", "invalidate:102"}));
    f.env.log.clear();
    f.list.m_panel = nullptr;
    f.list.m_scroll = nullptr;
    Translate(f.list, 1, 2, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"base-translate:100:1,2", "translate:103:1,2", "translate:102:1,2"}));
}

OBL_TEST_CASE("gui_combo: a press over the closed box opens the drop-down below it with a bar when more items than lines exist")
{
    Fixture f(5);
    f.list.m_selected = 2;
    f.env.hovered = 1u << 0; // the list itself
    OBL_CHECK_EQ(OnMouse(f.list, 40, 35, 1, f.env), 0);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"tooltip:100:40,35,1", "left:103:1", "hit:100:40,35", "hovered:100", "invalidate:100", "bounds:102", "bounds:102",
                    "new:" + std::to_string(sizeof(obl::ui::GuiScrollBarData)), "scroll:105,45,120,87,2,5,3,3", "bounds:102", "bounds:102",
                    "new:" + std::to_string(sizeof(obl::ui::GuiMainItemData)), "main:20,45,120,89,2"}));
    OBL_CHECK_EQ(f.list.m_state, 1);
    OBL_CHECK(f.list.m_scroll == &f.env.madeBar && f.list.m_panel == &f.env.madePanel);
    OBL_CHECK_EQ(f.list.m_scrollTop, 3u); // what the bar clamped it to
    OBL_CHECK_EQ(f.list.m_hover, 2u);
}

OBL_TEST_CASE("gui_combo: without a bar the panel is as tall as the item count; an upward list hangs above the box")
{
    Fixture f(2);
    f.env.hovered = 1;
    OnMouse(f.list, 1, 2, 1, f.env);
    OBL_CHECK_EQ(f.env.log.back(), "main:20,45,120,75,2"); // 14 * 2 + 45 + 2
    OBL_CHECK(f.list.m_scroll == nullptr);
    Fixture g(5);
    g.list.m_direction = 1;
    g.list.m_selected = 4;
    g.env.hovered = 1;
    OnMouse(g.list, 1, 2, 1, g.env);
    OBL_CHECK_EQ(Join(g.env.log),
            Join({"tooltip:100:1,2,1", "left:103:1", "hit:100:1,2", "hovered:100", "invalidate:100", "bounds:102", "bounds:102",
                    "new:" + std::to_string(sizeof(obl::ui::GuiScrollBarData)), "scroll:105,-11,120,31,4,5,3,3", "bounds:102", "bounds:102",
                    "new:" + std::to_string(sizeof(obl::ui::GuiMainItemData)), "main:20,-14,120,30,3"}));
}

OBL_TEST_CASE("gui_combo: a failed panel allocation leaves it null; closed, only a press over the box acts")
{
    Fixture f(2);
    f.env.hovered = 1;
    OnMouse(f.list, 1, 2, 0, f.env);
    OBL_CHECK_EQ(f.list.m_state, 0);
    OBL_CHECK_EQ(Join(f.env.log), Join({"tooltip:100:1,2,0", "left:103:0", "hit:100:1,2", "hovered:100"}));
    f.env.log.clear();
    f.env.hovered = 0;
    OnMouse(f.list, 1, 2, 1, f.env);
    OBL_CHECK_EQ(f.list.m_state, 0);
    f.list.m_state = 2;
    f.env.log.clear();
    OBL_CHECK_EQ(OnMouse(f.list, 1, 2, 1, f.env), 0);
    OBL_CHECK_EQ(Join(f.env.log), Join({"tooltip:100:1,2,1", "left:103:1", "hit:100:1,2"}));
}

OBL_TEST_CASE("gui_combo: open, a move over the panel sets the hovered row from the panel's top and the scroll position")
{
    Fixture f(5);
    f.Open();
    f.list.m_scrollTop = 1;
    f.env.hovered = 1u << 1; // the panel
    f.env.boxY0 = 60; // GetBounds answers the same rectangle for every widget
    OnMouse(f.list, 40, 60 + 14 * 2 + 3, 0, f.env);
    OBL_CHECK_EQ(f.list.m_hover, 3u);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"tooltip:100:40,91,0", "left:103:0", "hit:100:40,91", "left:101:0", "hovered:101", "bounds:101", "hovered:100",
                    "clicked:101", "hovered:100"}));
    // a pressed bar thumb (dragging) or a bar under the cursor keeps the row
    f.list.m_scroll = &f.bar;
    f.bar.m_dragging = 1;
    f.list.m_hover = 9;
    f.env.log.clear();
    OnMouse(f.list, 40, 89, 0, f.env);
    OBL_CHECK_EQ(f.list.m_hover, 9u);
}

OBL_TEST_CASE("gui_combo: the bar answering ends the event with its own byte")
{
    Fixture f(5);
    f.Open();
    f.list.m_scroll = &f.bar;
    f.env.left = 1u << 4;
    OBL_CHECK_EQ(OnMouse(f.list, 40, 89, 1, f.env), 5);
    OBL_CHECK_EQ(f.env.log.back(), "left:104:1");
    OBL_CHECK_EQ(f.list.m_state, 1);
}

OBL_TEST_CASE("gui_combo: a press over the box while open closes the drop-down and reports 1")
{
    Fixture f(5);
    f.Open();
    f.list.m_scroll = &f.bar;
    f.env.hovered = 1;
    OBL_CHECK_EQ(OnMouse(f.list, 1, 2, 1, f.env), 1);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"tooltip:100:1,2,1", "left:103:1", "hit:100:1,2", "left:101:1", "left:104:1", "hovered:100", "delete:104", "delete:101",
                    "invalidate:100"}));
    OBL_CHECK(f.list.m_scroll == nullptr && f.list.m_panel == nullptr);
    OBL_CHECK_EQ(f.list.m_state, 0);
}

OBL_TEST_CASE("gui_combo: clicking an item selects it, tells the owner and closes; a separator or an out-of-range row is ignored")
{
    Fixture f(3, 0b100);
    f.Open();
    f.env.clicked = 1u << 1;
    f.env.boxY0 = 40;
    OBL_CHECK_EQ(OnMouse(f.list, 5, 40 + 14, 1, f.env), 1); // row 1
    OBL_CHECK_EQ(f.list.m_selected, 1u);
    OBL_CHECK_EQ(f.panel.m_clicked, 0);
    OBL_CHECK_EQ(Join(f.env.log).find("selected:11") != std::string::npos, true);
    OBL_CHECK_EQ(f.list.m_state, 0);
    // a row far below clamps to the last item, which here is a separator: nothing happens
    Fixture g(3, 0b100);
    g.Open();
    g.env.clicked = 1u << 1;
    g.env.boxY0 = 40;
    OBL_CHECK_EQ(OnMouse(g.list, 5, 40 + 14 * 50, 1, g.env), 0);
    OBL_CHECK_EQ(g.list.m_selected, 0u);
    OBL_CHECK_EQ(g.list.m_state, 1);
    OBL_CHECK_EQ(Join(g.env.log).find("selected") == std::string::npos, true);
}

OBL_TEST_CASE("gui_combo: a press outside closes without choosing, a move outside leaves it open")
{
    Fixture f(3);
    f.Open();
    OBL_CHECK_EQ(OnMouse(f.list, 500, 500, 0, f.env), 0);
    OBL_CHECK_EQ(f.list.m_state, 1);
    OBL_CHECK_EQ(OnMouse(f.list, 500, 500, 1, f.env), 0);
    OBL_CHECK_EQ(f.list.m_state, 0);
    OBL_CHECK(f.list.m_panel == nullptr);
    f.Open();
    f.env.hovered = 1;
    f.env.log.clear();
    OBL_CHECK_EQ(OnMouse(f.list, 500, 500, 2, f.env), 0); // hovered but neither move nor press: nothing
    OBL_CHECK_EQ(f.list.m_state, 1);
}

OBL_TEST_CASE("gui_combo: an unrecognized state makes OnMouse ignore everything but the tooltip, the button and the hit test")
{
    Fixture f(3);
    f.list.m_state = 3;
    OBL_CHECK_EQ(OnMouse(f.list, 1, 2, 1, f.env), 0);
    OBL_CHECK_EQ(Join(f.env.log), Join({"tooltip:100:1,2,1", "left:103:1", "hit:100:1,2"}));
}

OBL_TEST_CASE("gui_combo: the wheel over the closed box steps forward and back over separators and tells the owner")
{
    Fixture f(5, 0b00110);
    f.env.hit = 1u << 2; // the box
    f.list.m_selected = 0;
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, -120, f.env), 1);
    OBL_CHECK_EQ(f.list.m_selected, 3u); // rows 1 and 2 are separators
    OBL_CHECK_EQ(Join(f.env.log), Join({"hit:102:1,2", "selected:13", "invalidate:100"}));
    f.env.log.clear();
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, 120, f.env), 1);
    OBL_CHECK_EQ(f.list.m_selected, 0u);
    f.list.m_selected = 4;
    OnMouseWheel(f.list, 1, 2, -120, f.env); // at the end: stays
    OBL_CHECK_EQ(f.list.m_selected, 4u);
    f.list.m_selected = 0;
    OnMouseWheel(f.list, 1, 2, 0, f.env); // at the start: stays (delta 0 counts as backwards)
    OBL_CHECK_EQ(f.list.m_selected, 0u);
}

OBL_TEST_CASE("gui_combo: the wheel stops on a separator at the edge and ignores a cursor outside the box")
{
    Fixture f(3, 0b100);
    f.env.hit = 1u << 2;
    f.list.m_selected = 0;
    OnMouseWheel(f.list, 1, 2, -120, f.env);
    OBL_CHECK_EQ(f.list.m_selected, 1u);
    OnMouseWheel(f.list, 1, 2, -120, f.env);
    OBL_CHECK_EQ(f.list.m_selected, 2u); // 1 < 3 - 1 steps onto the last row although it is a separator; the walk then stops at the end
    OnMouseWheel(f.list, 1, 2, -120, f.env);
    OBL_CHECK_EQ(f.list.m_selected, 2u);
    f.env.hit = 0;
    f.env.log.clear();
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, -120, f.env), 0);
    OBL_CHECK_EQ(Join(f.env.log), Join({"hit:102:1,2"}));
}

OBL_TEST_CASE("gui_combo: the open list hands the wheel to the bar when the cursor is over the bar or the panel")
{
    Fixture f(5);
    f.Open();
    f.list.m_scroll = &f.bar;
    f.env.wheel = 1u << 4;
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, 120, f.env), 0);
    OBL_CHECK_EQ(Join(f.env.log), Join({"hit:104:1,2", "hit:101:1,2"}));
    f.env.log.clear();
    f.env.hit = 1u << 1;
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, 120, f.env), 0x81);
    OBL_CHECK_EQ(Join(f.env.log), Join({"hit:104:1,2", "hit:101:1,2", "wheel:104:120"}));
    f.list.m_scroll = nullptr;
    f.env.log.clear();
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, 120, f.env), 0);
    OBL_CHECK(f.env.log.empty());
    f.list.m_state = 2;
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, 120, f.env), 0);
}

OBL_TEST_CASE("gui_combo: Draw redraws the box and writes the chosen item while dirty; the button always draws")
{
    Fixture f(3);
    f.list.m_selected = 1;
    f.list.m_dirty = 1;
    f.env.boxX0 = 20;
    f.env.boxY0 = 30;
    f.env.boxX1 = 120;
    f.env.boxY1 = 45;
    Draw(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"invalidate:102", "draw:102", "bounds:102", "talign:2,0", "toutline:2,0", "tcolor:2,1122867", "trect:2,26,30,100,15",
                    "tdraw:2,0,1:66", "draw:103"}));
    OBL_CHECK_EQ(f.list.m_dirty, 0);
    f.env.log.clear();
    Draw(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"draw:103"}));
    Fixture g(0);
    g.list.m_dirty = 1;
    Draw(g.list, g.env);
    OBL_CHECK_EQ(Join(g.env.log), Join({"invalidate:102", "draw:102", "draw:103"}));
    OBL_CHECK_EQ(g.list.m_dirty, 0);
}

OBL_TEST_CASE("gui_combo: DrawRowHighlight spans the box minus margins (and the bar) halfway down the row")
{
    Fixture f(1);
    DrawRowHighlight(f.list, 50, f.env);
    OBL_CHECK_EQ(f.env.log.back(), "line-fill:24,57,92,2"); // x0 + 4, y + 14/2, 120 - 20 - 8, 2
    f.env.log.clear();
    f.list.m_scroll = &f.bar;
    DrawRowHighlight(f.list, 50, f.env);
    OBL_CHECK_EQ(f.env.log.back(), "line-fill:24,57,77,2");
}

OBL_TEST_CASE(
        "gui_combo: DrawOverlay paints the window rows: the hovered one on a band with inverted text, separators as lines, then bar and "
        "button")
{
    Fixture f(5, 0b00100);
    f.Open();
    f.list.m_scroll = &f.bar;
    f.list.m_scrollTop = 1;
    f.list.m_hover = 3;
    f.env.boxX0 = 20;
    f.env.boxY0 = 60;
    f.env.boxX1 = 120;
    f.env.boxY1 = 75;
    DrawOverlay(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log),
            Join({"talign:2,0", "bounds:101", "invalidate:101", "draw:101",
                    // row 1: plain text at the panel's top
                    "tcolor:2,1122867", "bounds:102", "trect:2,20,60,100,14", "tdraw:2,4,1:66",
                    // row 2: a separator at one row down
                    "bounds:102", "bounds:102", "bounds:102", "line-fill:24,81,77,2",
                    // row 3: hovered band and inverted text two rows down
                    "bounds:102", "bounds:102", "hover-fill:22,88,118,102", "tcolor:2,4293844428", "bounds:102", "trect:2,20,88,100,14",
                    "tdraw:2,4,1:68", "overlay:104", "overlay:103"}));
}

OBL_TEST_CASE("gui_combo: DrawOverlay without a panel only draws the bar and the button; items outside the window are skipped")
{
    Fixture f(3);
    f.list.m_scroll = &f.bar;
    DrawOverlay(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"overlay:104", "overlay:103"}));
    Fixture g(5);
    g.Open();
    g.list.m_scrollTop = 4;
    g.list.m_lines = 1;
    g.list.m_hover = 9;
    DrawOverlay(g.list, g.env);
    OBL_CHECK_EQ(Join(g.env.log),
            Join({"talign:2,0", "bounds:101", "invalidate:101", "draw:101", "tcolor:2,1122867", "bounds:102", "trect:2,20,30,100,14",
                    "tdraw:2,4,1:69", "overlay:103"}));
}
