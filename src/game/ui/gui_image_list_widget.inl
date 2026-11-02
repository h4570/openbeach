// Image-list adapters share the population environment.
void ImgDeleteEntry(ImageEntry* entry, uint32_t flags)
{
    GuiImageListEntryDelete(entry, static_cast<uint8_t>(flags));
}
void ImgDialogs(void* manager, uint32_t flag)
{
    static_cast<ui::GuiManager*>(manager)->InvalidateAllDialogs(static_cast<uint8_t>(flag));
}
void ImgSound(audio::Sound* sound, const char* key, int channel, float delay)
{
    sound->Play(key, channel, delay);
}
void ImgPopulateBuildingsCall(ImageList* list, int category, const char* sprite, const char* locked)
{
    list->PopulateBuildings(category, sprite, locked);
}
void ImgPopulateSceneryCall(ImageList* list, int category, const char* sprite)
{
    list->PopulateScenery(category, sprite);
}
void ImgPopulateSurfacesCall(ImageList* list)
{
    list->PopulateSurfaces();
}
void ImgRefreshCall(ImageList* list)
{
    list->RefreshImagesAndCounts();
}
void ImgCreateCall(ImageList* list)
{
    list->CreateImageWidgets();
}

struct ImageWidgetEnv : ImageEnv {
    const char* KindName(int kind) { return gui_data::kGuiImageListKindNames[kind]; }
    int Mode() { return game::GetGuiManager()->m_mode; }
    void DeleteEntry(ImageEntry& entry) { ImgDeleteEntry(&entry, 1); }
    template<class T> void ReleaseStorage(T* begin, T* cap) { ImgDeallocWordsCall(begin, static_cast<uint32_t>(cap - begin)); }
    void InvalidateDialogs() { ImgDialogs(game::GetGuiManager(), 0); }
    void ClickSound() { ImgSound(audio::CurrentSound(), gui_data::kButtonClickSound, 1, 0.0f); }
    void UpdateImage(Widget& w, float dt) { w.Update(dt); }
    void UpdateList(ImageList& list, float dt) { UpdateImage(list, dt); }
    void CreateImages(ImageList& list) { ImgCreateCall(&list); }
    void Refresh(ImageList& list) { ImgRefreshCall(&list); }
    void Populate(ImageList& list)
    {
        const auto* names = gui_data::kGuiImageListThumbNames;
        if (list.m_kind >= 0 && list.m_kind <= 2)
            ImgPopulateBuildingsCall(&list, list.m_kind, names[0], names[1]);
        else if (list.m_kind == 4)
            ImgPopulateSurfacesCall(&list);
        else if (list.m_kind == 3)
            ImgPopulateSceneryCall(&list, 0, names[2]);
        else if (list.m_kind >= 5 && list.m_kind <= 9)
            ImgPopulateSceneryCall(&list, list.m_kind - 3, names[list.m_kind]);
    }
    void DrawLabel(ImageLabel& label)
    {
        TextAlign(label.m_font, 0);
        TextOutlined(label.m_font, 0);
        TextColor(label.m_font, label.m_color);
        TextRect(label.m_font, label.m_x, label.m_y, label.m_param, 15);
        TextDraw(label.m_font, 0, 0, label.m_text);
    }
};

} // namespace

namespace {
