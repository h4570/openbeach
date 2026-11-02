// Included in gui_lists.cpp after gui_lists_populate.inl: GuiImageList filling and selection.
using ImageList = ui::GuiImageListData;
using ImageEntry = ui::GuiImageListEntry;
using ImageLabel = ui::GuiImageListLabel;

test::CallCounter g_imageCalls("ui.gui-image-list");

tables::BuildingDef* ImgBuildingDef(void* params, uint32_t id)
{
    return world::world_params::GetBuildingDef(static_cast<tables::WorldParams*>(params), static_cast<int>(id));
}
tables::SceneryDef* ImgSceneryDef(void* params, int id)
{
    return world::world_params::GetSceneryDef(static_cast<tables::WorldParams*>(params), id);
}
uint8_t ImgLocked(void* world, int id)
{
    return static_cast<world_data::World*>(world)->BuildingLockedByTree(id);
}
uint8_t ImgAllowed(void* world, int id)
{
    return static_cast<world_data::World*>(world)->BuildingAllowed(id);
}
int ImgFinished(void* world, int id)
{
    return static_cast<world_data::World*>(world)->CountFinishedBuildings(id);
}
int ImgTotal(void* world, int id)
{
    return static_cast<world_data::World*>(world)->CountBuildings(id);
}
void ImgSetMode(void* manager, int mode)
{
    (static_cast<ui::GuiManager*>(manager))->SetMode(mode);
}
void ImgSetSprite(Widget* w, const char* name)
{
    GuiImageSetSprite(static_cast<ui::GuiImage*>(w), name);
}
void ImgFree(void* block)
{
    ::operator delete(block);
}
void ImgDeallocSmall(void* block, uint32_t)
{
    ::operator delete(block);
}
void ImgDeallocWords(void* block, uint32_t)
{
    ::operator delete(block);
}
void ImgDeallocWordsCall(void* block, uint32_t)
{
    ::operator delete(block);
}

struct ImageEnv : GameEnv {
    uint32_t BuildingCount() { return game::GetWorldParams()->m_numBuildings; }
    const tables::BuildingDef* Building(uint32_t id) { return ImgBuildingDef(game::GetWorldParams(), id); }
    uint32_t SceneryCount() { return game::GetWorldParams()->m_numScenery; }
    const tables::SceneryDef* Scenery(uint32_t id) { return ImgSceneryDef(game::GetWorldParams(), static_cast<int>(id)); }
    const tables::SceneryDef* PickedScenery() { return ImgSceneryDef(game::GetWorldParams(), game::GetGuiManager()->m_context[5]); }
    // The tooltip keys: "BUILD_TIPTEXT_<key>" / "SP_DESC_<key>" upper-cased (the keys are ASCII, so the CRT's locale does not matter).
    static void Tip(const char* format, const char* key, char* out)
    {
        std::snprintf(out, 128, format, key);
        _strupr(out);
    }
    void BuildTip(const char* key, char* out) { Tip(gui_data::kGuiBuildTipFormat, key, out); }
    void SceneryTip(const char* key, char* out) { Tip(gui_data::kGuiSceneryTipFormat, key, out); }
    bool Locked(uint32_t id) { return ImgLocked(static_cast<void*>(game::GetWorld()), static_cast<int>(id)) != 0; }
    bool Allowed(uint32_t id) { return ImgAllowed(static_cast<void*>(game::GetWorld()), static_cast<int>(id)) != 0; }
    int32_t Finished(uint32_t id) { return ImgFinished(static_cast<void*>(game::GetWorld()), static_cast<int>(id)); }
    int32_t Total(uint32_t id) { return ImgTotal(static_cast<void*>(game::GetWorld()), static_cast<int>(id)); }
    int32_t Tileset() { return game::GetPathTiles()->GetTilesetNumber(); }
    const char* ThumbPath() { return gui_data::kGuiImageListThumbNames[3]; }
    const char* ThumbGrass() { return gui_data::kGuiImageListThumbNames[4]; }
    void FormatCount(obl::Char16* out, int value)
    {
        const auto print = ::obl::ui::host_text::Print;
        print(out, gui_data::kListNumberFormat, value);
    }
    void* AllocWords(size_t bytes, size_t objectBytes) { return objectBytes > 0x80 ? CallNew(bytes) : CallAllocateWords(bytes); }
    void Free(void* block) { ImgFree(block); }
    void DeallocateSmall(void* block, uint32_t bytes) { ImgDeallocSmall(block, bytes); }
    void DeallocateWords(void* block, uint32_t bytes) { ImgDeallocWords(block, bytes); }
    Widget* ConstructImage(void* memory, int x0, int y0, int x1, int y1, const char* graphic)
    {
        return ConstructImageWidget(memory, x0, y0, x1, y1, graphic);
    }
    void SetTooltip(Widget& w, const char* text) { w.SetTooltip(text); }
    void SetSprite(Widget& w, const char* name) { ImgSetSprite(&w, name); }
    void SetMode(int mode) { ImgSetMode(game::GetGuiManager(), mode); }
};

} // namespace

namespace {

#include "core/char16.h"
#include "gui_image_list_widget.inl"
