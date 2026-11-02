#pragma once

#include "game/ui/gui_image_list.h"

namespace obl::gui_image_list {

template<class Env>
List* Construct(List& list, int x0, int y0, int x1, int y1, const char* name, const char* highlight, int spacingX, int spacingY, Env& env)
{
    list.m_spacingX = spacingX;
    list.m_spacingY = spacingY;
    list.m_refreshTimer = 0;
    list.m_refresh = 1;
    list.m_entriesBegin = list.m_entriesEnd = list.m_entriesCap = nullptr;
    list.m_imagesBegin = list.m_imagesEnd = list.m_imagesCap = nullptr;
    list.m_labelsBegin = list.m_labelsEnd = list.m_labelsCap = nullptr;
    for (int kind = 0; kind < 10; ++kind) {
        if (std::strcmp(name, env.KindName(kind)) == 0) {
            list.m_kind = kind;
            break;
        }
    }

    void* memory = env.template NewRecord<ui::GuiImageData>();
    list.m_highlight = memory ? env.ConstructImage(memory, x0, y0, x1, y1, highlight) : nullptr;
    list.m_type = 0x11;
    return &list;
}

template<class Env> void ClearEntries(List& list, Env& env)
{
    auto* const end = list.m_entriesEnd;
    for (auto* p = list.m_entriesBegin; p != end; ++p)
        if (*p) env.DeleteEntry(**p);
}

template<class Env> void ClearLabels(List& list, Env& env)
{
    auto* const end = list.m_labelsEnd;
    for (auto* p = list.m_labelsBegin; p != end; ++p)
        if (*p) env.Free(*p);
}

template<class Env> void ClearImages(List& list, Env& env)
{
    auto* const end = list.m_imagesEnd;
    for (auto* p = list.m_imagesBegin; p != end; ++p)
        if (*p) env.DeleteWidget(**p);
    list.m_imagesEnd = list.m_imagesBegin;
}

template<class Env> void Destruct(List& list, Env& env)
{
    ClearEntries(list, env);
    list.m_entriesEnd = list.m_entriesBegin;
    ClearLabels(list, env);
    list.m_labelsEnd = list.m_labelsBegin;
    ClearImages(list, env);
    if (list.m_highlight) env.DeleteWidget(*static_cast<Widget*>(list.m_highlight));
    if (list.m_labelsBegin) env.ReleaseStorage(list.m_labelsBegin, list.m_labelsCap);
    if (list.m_imagesBegin) env.ReleaseStorage(list.m_imagesBegin, list.m_imagesCap);
    if (list.m_entriesBegin) env.ReleaseStorage(list.m_entriesBegin, list.m_entriesCap);
}

template<class Env> void SetPaletteMode(List& list, Env& env)
{
    if (list.m_kind == 4)
        env.SetMode(env.PickedScenery()->m_PathFlag == 0 ? kModePath : kModeGrass);
    else if (list.m_kind == 3 || (list.m_kind >= 5 && list.m_kind <= 9))
        env.SetMode(kModeScenery);
}

template<class Env> void SetPaletteContext(List& list, int selected, Env& env)
{
    if (list.m_kind >= 0 && list.m_kind <= 9)
        env.SetContext(list.m_kind <= 2 ? kBuildingContext : kSceneryContext, list.m_entriesBegin[selected]->m_id);
}

template<class Env> void Update(List& list, float dt, Env& env)
{
    if (env.Mode() == 10 && list.m_imagesBegin != list.m_imagesEnd) {
        list.m_selected = -1;
        env.InvalidateDialogs();
    }
    if (list.m_refresh) {
        const double elapsed = static_cast<double>(dt) + list.m_refreshTimer;
        list.m_refreshTimer = static_cast<float>(elapsed);
        if (elapsed >= 2.0) {
            ClearEntries(list, env);
            list.m_entriesEnd = list.m_entriesBegin;
            ClearLabels(list, env);
            list.m_labelsEnd = list.m_labelsBegin;
            list.m_refreshTimer = 0;
            env.Populate(list);
            env.Refresh(list);
            env.Invalidate(list);
            SetPaletteMode(list, env);
        }
    }
    for (auto* p = list.m_imagesBegin; p != list.m_imagesEnd; ++p) env.UpdateImage(**p, dt);
}

template<class Env> uint8_t OnMouseLeft(List& list, int x, int y, int state, Env& env)
{
    env.TrackTooltip(list, x, y, state);
    int selected = -1;
    bool clicked = false;
    for (auto* p = list.m_imagesBegin; p != list.m_imagesEnd && !clicked; ++p) {
        env.OnMouseLeft(**p, x, y, state);
        ++selected;
        if (env.IsClicked(**p)) {
            (*p)->m_clicked = 0;
            clicked = true;
        }
    }
    if (clicked && selected != list.m_selected) {
        env.ClickSound();
        if (list.m_imagesBegin != list.m_imagesEnd) {
            list.m_selected = selected;
            if (selected == -1)
                env.InvalidateDialogs();
            else
                SetPaletteContext(list, selected, env);
        }
        SetPaletteMode(list, env);
    }
    return clicked;
}

template<class Env> void Draw(List& list, Env& env)
{
    if (!list.m_dirty) return;
    auto* const end = list.m_imagesEnd;
    for (auto* p = list.m_imagesBegin; p != end; ++p) env.Draw(**p);
    if (list.m_imagesBegin != list.m_imagesEnd && list.m_selected != -1) {
        int rect[4];
        const int* bounds = env.Bounds(*list.m_imagesBegin[list.m_selected], rect);
        auto& highlight = *static_cast<Widget*>(list.m_highlight);
        env.SetPosition(highlight, Sub(bounds[0], 2), Sub(bounds[1], 2));
        env.Draw(highlight);
    }
    auto* const labelEnd = list.m_labelsEnd;
    for (auto* p = list.m_labelsBegin; p != labelEnd; ++p) env.DrawLabel(**p);
    list.m_dirty = 0;
}

template<class Env> void Invalidate(List& list, Env& env)
{
    env.BaseInvalidate(list);
    auto* const end = list.m_imagesEnd;
    for (auto* p = list.m_imagesBegin; p != end; ++p) env.Invalidate(**p);
    env.Invalidate(*static_cast<Widget*>(list.m_highlight));
}

template<class Env> void Translate(List& list, int dx, int dy, Env& env)
{
    env.BaseTranslate(list, dx, dy);
    for (auto* p = list.m_imagesBegin; p != list.m_imagesEnd; ++p) env.Translate(**p, dx, dy);
    for (auto* p = list.m_labelsBegin; p != list.m_labelsEnd; ++p) {
        (*p)->m_x = Add((*p)->m_x, dx);
        (*p)->m_y = Add((*p)->m_y, dy);
    }
}

template<class Env> void SetVisible(List& list, uint32_t visible, Env& env)
{
    env.BaseSetVisible(list, visible);
    if (!list.m_visible) {
        ClearEntries(list, env);
        list.m_entriesEnd = list.m_entriesBegin;
        ClearLabels(list, env);
        list.m_labelsEnd = list.m_labelsBegin;
        ClearImages(list, env);
        return;
    }
    list.m_refresh = 1;
    env.UpdateList(list, 2.0f);
    env.CreateImages(list);
    if (list.m_imagesBegin != list.m_imagesEnd) {
        list.m_selected = 0;
        SetPaletteContext(list, 0, env);
    }
    SetPaletteMode(list, env);
}

} // namespace obl::gui_image_list
