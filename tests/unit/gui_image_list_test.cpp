#include "unit_test.h"
#include "game/ui/gui_image_list.h"
#include "game/ui/gui_image_list_widget.h"
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {
struct WidgetEnv {
    obl::tables::SceneryDef scenery{};
    int mode = 4, picked = -1, sounds = 0, rebuilds = 0, updates = 0, deletes = 0;
    int clicked = -1;
    std::vector<int> visited;
    int Mode() { return mode; }
    void InvalidateDialogs() {}
    void DeleteEntry(obl::gui_image_list::Entry&) { ++deletes; }
    void Free(void*) { ++deletes; }
    void DeleteWidget(obl::ui::GuiWidget&) { ++deletes; }
    void Populate(obl::gui_image_list::List&) { ++rebuilds; }
    void Refresh(obl::gui_image_list::List&) {}
    void Invalidate(obl::ui::GuiWidget&) {}
    void SetMode(int value) { mode = value; }
    const obl::tables::SceneryDef* PickedScenery() { return &scenery; }
    void UpdateImage(obl::ui::GuiWidget&, float) { ++updates; }
    void TrackTooltip(obl::ui::GuiWidget&, int, int, int) {}
    void OnMouseLeft(obl::ui::GuiWidget& w, int, int, int) { visited.push_back(w.m_id); }
    bool IsClicked(obl::ui::GuiWidget& w) { return w.m_id == clicked; }
    void ClickSound() { ++sounds; }
    void SetContext(int, int value) { picked = value; }
    void BaseSetVisible(obl::ui::GuiWidget& w, uint32_t visible) { w.m_visible = visible != 0; }
    void UpdateList(obl::gui_image_list::List&, float) { ++updates; }
    void CreateImages(obl::gui_image_list::List&) { ++rebuilds; }
};
} // namespace

OBL_TEST_CASE("gui_image_list: refresh compares the unrounded sum and skips disabled or NaN timers")
{
    obl::gui_image_list::List list{};
    WidgetEnv env;
    list.m_kind = 0;
    list.m_refresh = 1;
    list.m_refreshTimer = 1.99999988f;
    obl::gui_image_list::Update(list, 0.00000006f, env);
    OBL_CHECK_EQ(list.m_refreshTimer, 2.0f);
    OBL_CHECK_EQ(env.rebuilds, 0);
    obl::gui_image_list::Update(list, 0, env);
    OBL_CHECK_EQ(env.rebuilds, 1);
    OBL_CHECK_EQ(list.m_refreshTimer, 0);
    list.m_refreshTimer = std::numeric_limits<float>::quiet_NaN();
    obl::gui_image_list::Update(list, 10, env);
    OBL_CHECK_EQ(env.rebuilds, 1);
    list.m_refresh = 0;
    list.m_refreshTimer = 1;
    obl::gui_image_list::Update(list, 10, env);
    OBL_CHECK_EQ(list.m_refreshTimer, 1);
}

OBL_TEST_CASE("gui_image_list: a click consumes only the first clicked icon and changes context once")
{
    using namespace obl;
    gui_image_list::List list{};
    ui::GuiWidget icons[3]{};
    ui::GuiWidget* imageVec[] = {&icons[0], &icons[1], &icons[2]};
    gui_image_list::Entry entries[3]{};
    gui_image_list::Entry* entryVec[] = {&entries[0], &entries[1], &entries[2]};
    for (int i = 0; i < 3; ++i) {
        icons[i].m_id = i;
        icons[i].m_clicked = 1;
        entries[i].m_id = 100 + i;
    }
    list.m_imagesBegin = imageVec;
    list.m_imagesEnd = imageVec + 3;
    list.m_entriesBegin = entryVec;
    list.m_entriesEnd = entryVec + 3;
    list.m_kind = 4;
    list.m_selected = 0;
    WidgetEnv env;
    env.clicked = 1;
    OBL_CHECK_EQ(gui_image_list::OnMouseLeft(list, 0, 0, 0, env), 1);
    OBL_CHECK_EQ(env.visited.size(), 2u);
    OBL_CHECK_EQ(icons[1].m_clicked, 0);
    OBL_CHECK_EQ(icons[2].m_clicked, 1);
    OBL_CHECK_EQ(env.picked, 101);
    OBL_CHECK_EQ(env.mode, 7);
    OBL_CHECK_EQ(list.m_selected, 1);
    OBL_CHECK_EQ(env.sounds, 1);
    gui_image_list::OnMouseLeft(list, 0, 0, 0, env);
    OBL_CHECK_EQ(env.sounds, 1);
}

OBL_TEST_CASE("gui_image_list: hiding frees owned elements but keeps vector capacity for reuse")
{
    using namespace obl;
    gui_image_list::List list{};
    gui_image_list::Entry entry{};
    gui_image_list::Entry* entries[] = {&entry};
    gui_image_list::Label label{};
    gui_image_list::Label* labels[] = {&label};
    ui::GuiWidget icon{};
    ui::GuiWidget* images[] = {&icon};
    list.m_entriesBegin = entries;
    list.m_entriesEnd = list.m_entriesCap = entries + 1;
    list.m_labelsBegin = labels;
    list.m_labelsEnd = list.m_labelsCap = labels + 1;
    list.m_imagesBegin = images;
    list.m_imagesEnd = list.m_imagesCap = images + 1;
    WidgetEnv env;
    gui_image_list::SetVisible(list, 0, env);
    OBL_CHECK_EQ(env.deletes, 3);
    OBL_CHECK_EQ(list.m_entriesEnd, entries);
    OBL_CHECK_EQ(list.m_labelsEnd, labels);
    OBL_CHECK_EQ(list.m_imagesEnd, images);
    OBL_CHECK_EQ(list.m_entriesCap, entries + 1);
    gui_image_list::SetVisible(list, 0, env);
    OBL_CHECK_EQ(env.deletes, 3);
}

namespace {
using namespace obl;
using obl::gui_image_list::Entry;
using obl::gui_image_list::List;

struct Env {
    tables::BuildingDef buildings[3]{};
    tables::SceneryDef sceneries[4]{};
    uint32_t locked = 0, allowed = 0;
    int finished[3]{}, total[3]{};
    int tileset = 7;
    int pickedContext = 0;
    ui::SpriteAnim anim{};
    ui::Sprite spriteData{};
    ui::SpriteAnim* anims[1]{&anim};
    ui::SpriteInstance sprite{};
    ui::GuiWidget icons[8]{};
    int nextIcon = 0;
    int32_t bounds[4][4]{};
    std::vector<std::string> calls;
    std::vector<void*> blocks;

    Env()
    {
        spriteData.m_anims = anims;
        sprite.m_sprite = &spriteData;
        sprite.m_animIndex = 0;
    }
    ~Env()
    {
        for (void* block : blocks) std::free(block);
    }

    // The GUI records (`new T()` in the game) come from Alloc with the record's size.
    template<class T> T* NewRecord()
    {
        void* memory = Alloc(sizeof(T));
        return memory ? new (memory) T() : nullptr;
    }
    void* Alloc(size_t size)
    {
        void* p = std::calloc(1, size);
        blocks.push_back(p);
        return p;
    }
    void* AllocWords(size_t bytes, size_t)
    {
        calls.push_back("words:" + std::to_string(bytes));
        return Alloc(bytes);
    }
    void Free(void*) { calls.push_back("free"); }
    void DeallocateSmall(void*, uint32_t bytes) { calls.push_back("small:" + std::to_string(bytes)); }
    void DeallocateWords(void*, uint32_t bytes) { calls.push_back("dealloc:" + std::to_string(bytes)); }
    uint32_t BuildingCount() { return 3; }
    const tables::BuildingDef* Building(uint32_t id) { return &buildings[id]; }
    uint32_t SceneryCount() { return 4; }
    const tables::SceneryDef* Scenery(uint32_t id) { return &sceneries[id]; }
    const tables::SceneryDef* PickedScenery()
    {
        calls.push_back("picked");
        return &sceneries[pickedContext];
    }
    void BuildTip(const char* key, char* out) { std::snprintf(out, 128, "BUILD_%s", key); }
    void SceneryTip(const char* key, char* out) { std::snprintf(out, 128, "SP_%s", key); }
    bool Locked(uint32_t id) { return (locked >> id) & 1; }
    bool Allowed(uint32_t id) { return (allowed >> id) & 1; }
    int32_t Finished(uint32_t id) { return finished[id]; }
    int32_t Total(uint32_t id) { return total[id]; }
    int32_t Tileset() { return tileset; }
    const char* ThumbPath() { return "path"; }
    const char* ThumbGrass() { return "grass"; }
    void FormatCount(obl::Char16* out, int value)
    {
        const std::string text = std::to_string(value);
        size_t i = 0;
        for (char c : text) out[i++] = static_cast<obl::Char16>(c);
        out[i] = 0;
    }
    ui::SpriteInstance* CreateSprite(const char* name)
    {
        calls.push_back(std::string("sprite:") + name);
        return &sprite;
    }
    void ReleaseSprite(ui::SpriteInstance*) { calls.push_back("release"); }
    const int32_t* Bounds(ui::GuiWidget& w, int32_t* out)
    {
        out[0] = w.m_x0;
        out[1] = w.m_y0;
        out[2] = w.m_x1;
        out[3] = w.m_y1;
        return out;
    }
    ui::GuiWidget* ConstructImage(void*, int x0, int y0, int x1, int y1, const char* graphic)
    {
        ui::GuiWidget* w = &icons[nextIcon++];
        w->m_x0 = x0;
        w->m_y0 = y0;
        w->m_x1 = x1;
        w->m_y1 = y1;
        calls.push_back(std::string("image:") + graphic + ":" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) +
                "," + std::to_string(y1));
        return w;
    }
    void SetTooltip(ui::GuiWidget&, const char* text) { calls.push_back(std::string("tip:") + text); }
    void SetFrame(ui::GuiWidget&, int frame) { calls.push_back("frame:" + std::to_string(frame)); }
    void SetSprite(ui::GuiWidget&, const char* name) { calls.push_back(std::string("setsprite:") + name); }
    void SetContext(int index, int value) { calls.push_back("context:" + std::to_string(index) + "," + std::to_string(value)); }
    void SetMode(int mode) { calls.push_back("mode:" + std::to_string(mode)); }
};

std::vector<std::string> Starting(const std::vector<std::string>& calls, const std::string& prefix)
{
    std::vector<std::string> out;
    for (const auto& c : calls)
        if (c.compare(0, prefix.size(), prefix) == 0) out.push_back(c);
    return out;
}
} // namespace

OBL_TEST_CASE("gui_image_list: buildings keep the locked grey entry and count only allowed ones")
{
    Env env;
    for (int i = 0; i < 3; ++i) {
        env.buildings[i].m_key = "b" + std::to_string(i);
        env.buildings[i].m_Category = 1;
        env.buildings[i].m_Icon = 10 + i;
    }
    env.buildings[2].m_Category = 0;
    env.locked = 1; // building 0 locked by the tree
    env.allowed = 2; // building 1 allowed, 2 is of another category
    env.total[1] = 3;
    env.finished[1] = 2;
    List list{};
    gui_image_list::PopulateBuildings(list, 1, "thumb", "grey", env);
    OBL_CHECK_EQ(list.m_entriesEnd - list.m_entriesBegin, 2);
    const Entry& locked = *list.m_entriesBegin[0];
    OBL_CHECK_EQ(std::string(locked.m_sprite), "grey");
    OBL_CHECK_EQ(locked.m_frame, 10);
    OBL_CHECK_EQ(locked.m_id, 0);
    OBL_CHECK_EQ(std::string(locked.m_tooltip), "BUILD_b0");
    OBL_CHECK_EQ(locked.m_hasCount, 0);
    const Entry& open = *list.m_entriesBegin[1];
    OBL_CHECK_EQ(std::string(open.m_sprite), "thumb");
    OBL_CHECK_EQ(open.m_id, 1);
    OBL_CHECK_EQ(open.m_hasCount, 1);
    OBL_CHECK_EQ(int(open.m_text[0]), int('3'));
    OBL_CHECK_EQ(open.m_color, 0xff8000u); // two of three finished
    env.finished[1] = 3;
    env.calls.clear();
    gui_image_list::PopulateBuildings(list, 1, "thumb", "grey", env);
    OBL_CHECK_EQ(list.m_entriesBegin[3]->m_color, 0u); // all finished (the second pass appends a locked entry, then this one)
    env.total[1] = 0;
    gui_image_list::PopulateBuildings(list, 1, "thumb", "grey", env);
    OBL_CHECK_EQ(list.m_entriesBegin[5]->m_color, 0xffff0000u); // none built
}

OBL_TEST_CASE("gui_image_list: growing an unlocked list frees its heap storage, while a locked list uses the pool")
{
    Env env;
    env.buildings[0].m_Category = 0;
    env.buildings[1].m_Category = 0;
    env.locked = 1;
    env.allowed = 2;
    List list{};
    gui_image_list::PopulateBuildings(list, 0, "a", "b", env); // 1 entry (capacity 1), then a second: grows 1 -> 2
    OBL_CHECK_EQ(env.calls.back(), "dealloc:" + std::to_string(sizeof(void*)));
    env.locked = 3; // both locked: the next appends use the pool free
    env.calls.clear();
    gui_image_list::PopulateBuildings(list, 0, "a", "b", env); // size 2 (cap 2): grows to 4 -> small:8
    bool pool = false;
    for (const auto& c : env.calls) pool = pool || c == "small:" + std::to_string(2 * sizeof(void*));
    OBL_CHECK(pool);
}

OBL_TEST_CASE("gui_image_list: surfaces choose path or grass by the path flag and switch the refresh off")
{
    Env env;
    env.sceneries[0].m_Category = 1;
    env.sceneries[0].m_PathFlag = 0;
    env.sceneries[1].m_Category = 1;
    env.sceneries[1].m_PathFlag = 2;
    env.sceneries[1].m_Icon = 4;
    env.sceneries[2].m_Category = 2;
    env.sceneries[1].m_key = "grass1";
    List list{};
    list.m_refresh = 1;
    gui_image_list::PopulateSurfaces(list, env);
    OBL_CHECK_EQ(list.m_entriesEnd - list.m_entriesBegin, 2);
    OBL_CHECK_EQ(std::string(list.m_entriesBegin[0]->m_sprite), "path");
    OBL_CHECK_EQ(list.m_entriesBegin[0]->m_frame, 7);
    OBL_CHECK_EQ(std::string(list.m_entriesBegin[1]->m_sprite), "grass");
    OBL_CHECK_EQ(list.m_entriesBegin[1]->m_frame, 4);
    OBL_CHECK_EQ(std::string(list.m_entriesBegin[1]->m_tooltip), "SP_grass1");
    OBL_CHECK_EQ(list.m_refresh, 0);
    List scenery{};
    scenery.m_refresh = 1;
    gui_image_list::PopulateScenery(scenery, 2, "orn", env);
    OBL_CHECK_EQ(scenery.m_entriesEnd - scenery.m_entriesBegin, 1);
    OBL_CHECK_EQ(std::string(scenery.m_entriesBegin[0]->m_sprite), "orn");
    OBL_CHECK_EQ(scenery.m_entriesBegin[0]->m_id, 2);
    OBL_CHECK_EQ(scenery.m_refresh, 0);
}

OBL_TEST_CASE("gui_image_list: icons wrap at the right edge and keep the first row's bottom")
{
    Env env;
    env.anim.m_width = 10;
    env.anim.m_height = 5;
    List list{};
    list.m_x0 = 0;
    list.m_y0 = 100;
    list.m_x1 = 28;
    list.m_y1 = 200;
    list.m_spacingX = 2;
    list.m_spacingY = 3;
    Entry entries[3]{};
    Entry* vec[3];
    for (int i = 0; i < 3; ++i) {
        vec[i] = &entries[i];
        entries[i].m_sprite = "icon";
        entries[i].m_frame = i;
    }
    entries[1].m_hasCount = 1;
    entries[1].m_color = 0x1234;
    entries[1].m_text[0] = 'x';
    entries[1].m_text[1] = 0;
    char tip[] = "TIP";
    entries[2].m_tooltip = tip;
    list.m_entriesBegin = vec;
    list.m_entriesEnd = list.m_entriesCap = vec + 3;
    gui_image_list::CreateImageWidgets(list, env);
    // icon width 10 + gap 2: 0..10, 12..22, then 24..34 passes the right edge 28 and starts the next row at x 0.
    const auto images = Starting(env.calls, "image:");
    OBL_CHECK_EQ(images.size(), size_t{3});
    OBL_CHECK_EQ(images[0], "image:icon:0,100,10,105");
    OBL_CHECK_EQ(images[1], "image:icon:12,100,22,105");
    OBL_CHECK_EQ(images[2], "image:icon:0,108,10,105"); // y0 moved by gap + height, y1 did not
    OBL_CHECK_EQ(Starting(env.calls, "tip:").size(), size_t{1});
    OBL_CHECK_EQ(env.calls[0], "sprite:icon");
    OBL_CHECK_EQ(env.calls[1], "release");
    OBL_CHECK_EQ(list.m_imagesEnd - list.m_imagesBegin, 3);
    OBL_CHECK_EQ(list.m_labelsEnd - list.m_labelsBegin, 1);
    const auto& label = *list.m_labelsBegin[0];
    OBL_CHECK_EQ(int(label.m_text[0]), int('x'));
    OBL_CHECK_EQ(label.m_x, 15); // the icon's left edge 12 + 3
    OBL_CHECK_EQ(label.m_y, 100);
    OBL_CHECK_EQ(label.m_color, 0x1234u);
    OBL_CHECK_EQ(label.m_param, 50);
    OBL_CHECK_EQ(label.m_font, 0);
}

OBL_TEST_CASE("gui_image_list: refresh re-skins the icons and adds labels for counted entries")
{
    Env env;
    List list{};
    Entry entries[2]{};
    Entry* entryVec[2] = {&entries[0], &entries[1]};
    ui::GuiWidget* iconVec[2] = {&env.icons[0], &env.icons[1]};
    entries[0].m_sprite = "a";
    entries[1].m_sprite = "b";
    entries[1].m_hasCount = 1;
    env.icons[1].m_x0 = 40;
    env.icons[1].m_y0 = 9;
    list.m_entriesBegin = entryVec;
    list.m_entriesEnd = list.m_entriesCap = entryVec + 2;
    list.m_imagesBegin = iconVec;
    list.m_imagesEnd = list.m_imagesCap = iconVec + 2;
    gui_image_list::RefreshImagesAndCounts(list, env);
    OBL_CHECK_EQ(env.calls[0], "setsprite:a");
    OBL_CHECK_EQ(env.calls[1], "frame:0");
    OBL_CHECK_EQ(env.calls[2], "setsprite:b");
    OBL_CHECK_EQ(list.m_labelsEnd - list.m_labelsBegin, 1);
    OBL_CHECK_EQ(list.m_labelsBegin[0]->m_x, 43);
    OBL_CHECK_EQ(list.m_labelsBegin[0]->m_y, 9);
}

OBL_TEST_CASE("gui_image_list: selection sets the context and mode from the kind")
{
    Env env;
    Entry entries[2]{};
    Entry* entryVec[2] = {&entries[0], &entries[1]};
    entries[0].m_id = 11;
    entries[1].m_id = 22;
    ui::GuiWidget* iconVec[1] = {&env.icons[0]};
    List list{};
    list.m_entriesBegin = entryVec;
    list.m_entriesEnd = list.m_entriesCap = entryVec + 2;
    list.m_imagesBegin = iconVec;
    list.m_imagesEnd = list.m_imagesCap = iconVec + 1;
    list.m_selected = -1;
    list.m_kind = 1;
    gui_image_list::RefreshSelection(list, env);
    OBL_CHECK_EQ(list.m_selected, 0);
    OBL_CHECK_EQ(env.calls.size(), size_t{1});
    OBL_CHECK_EQ(env.calls[0], "context:0,11");
    env.calls.clear();
    list.m_selected = 1;
    list.m_kind = 6;
    gui_image_list::RefreshSelection(list, env);
    OBL_CHECK_EQ(env.calls[0], "context:5,22");
    OBL_CHECK_EQ(env.calls[1], "mode:1");
    env.calls.clear();
    list.m_kind = 4;
    env.sceneries[0].m_PathFlag = 0;
    gui_image_list::RefreshSelection(list, env);
    OBL_CHECK_EQ(env.calls[0], "context:5,22");
    OBL_CHECK_EQ(env.calls[2], "mode:7");
    env.calls.clear();
    env.sceneries[0].m_PathFlag = 1;
    gui_image_list::RefreshSelection(list, env);
    OBL_CHECK_EQ(env.calls[2], "mode:8");
    env.calls.clear();
    list.m_imagesEnd = list.m_imagesBegin; // no icons: no context, and kind 2 has no mode
    list.m_kind = 2;
    gui_image_list::RefreshSelection(list, env);
    OBL_CHECK(env.calls.empty());
}
