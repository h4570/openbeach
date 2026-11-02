#pragma once

#include "core/pointer_width.h"
#include "game/ui/gui_lists.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/sprite.h"
#include "game/tables/world_params_tables.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace obl::gui_image_list {

using List = ui::GuiImageListData;
using Entry = ui::GuiImageListEntry;
using Label = ui::GuiImageListLabel;
using Widget = ui::GuiWidget;
using gui_widgets::Add;
using gui_widgets::Sub;
using obl::Char16;

inline constexpr uint32_t kCountNone = 0xffff0000; // counter colour: nothing of that kind built
inline constexpr uint32_t kCountBuilding = 0xff8000; // some of them still under construction
inline constexpr int32_t kLabelParam = 50;
inline constexpr int32_t kLabelMarginX = 3; // label starts this far inside the icon
inline constexpr int kTooltipSlot = 20; // index of the virtual SetTooltip(text) in the widget table
inline constexpr int32_t kBuildingContext = 0; // GuiManager::SetContext indices: building picked, scenery picked
inline constexpr int32_t kSceneryContext = 5;
inline constexpr int32_t kModeScenery = 1; // GuiManager::SetMode values RefreshSelection chooses
inline constexpr int32_t kModePath = 7;
inline constexpr int32_t kModeGrass = 8;
inline constexpr int32_t kSurfacesKind = 4;

template<class T> uint32_t Count(T* const* begin, T* const* end)
{
    return static_cast<uint32_t>(static_cast<int32_t>(end - begin));
}

// The three-field vector append with the free the callers differ in: `Words` hands the old block back to the container allocator (the
// allowed-building path of PopulateBuildings), the others free it through the pool below 0x80 bytes and operator delete above
// (gui_lists::AppendWord).
template<class T, class Env> void AppendWords(T*& begin, T*& end, T*& cap, T value, Env& env)
{
    if (end == cap) {
        const size_t count = begin ? end - begin : 0;
        const size_t capacity = count + (count > 1 ? count : 1);
        // T is often a pointer: the slot size is wanted.
        auto* storage = static_cast<T*>(env.AllocWords(capacity * sizeof(T), capacity * 4)); // NOLINT(bugprone-sizeof-expression)
        if (count) std::memmove(storage, begin, count * sizeof(T));
        storage[count] = value;
        if (begin) env.DeallocateWords(begin, static_cast<uint32_t>((cap - begin) * sizeof(T)));
        begin = storage;
        end = storage + count + 1;
        cap = storage + capacity;
    } else {
        if (end) *end = value;
        ++end;
    }
}

template<class Env> Entry* NewEntry(Env& env)
{
    auto* entry = env.template NewRecord<Entry>();
    entry->m_sprite = nullptr;
    entry->m_frame = 0;
    entry->m_tooltip = nullptr;
    entry->m_id = -1;
    entry->m_hasCount = 0;
    return entry;
}

template<class Env> void SetTooltip(Entry& entry, const char* text, Env& env)
{
    const size_t bytes = std::strlen(text) + 1;
    entry.m_tooltip = static_cast<char*>(env.Alloc(bytes));
    std::memcpy(entry.m_tooltip, text, bytes);
}

template<class Env> void PopulateBuildings(List& list, int32_t category, const char* sprite, const char* lockedSprite, Env& env)
{
    const uint32_t count = env.BuildingCount();
    for (uint32_t id = 0; id < count; ++id) {
        const tables::BuildingDef* def = env.Building(id);
        if (def->m_Category != category) continue;
        char tip[128];
        env.BuildTip(def->m_key.c_str(), tip);
        if (env.Locked(id)) {
            Entry* entry = NewEntry(env);
            entry->m_sprite = lockedSprite;
            entry->m_frame = def->m_Icon;
            entry->m_id = static_cast<int32_t>(id);
            SetTooltip(*entry, tip, env);
            gui_lists::AppendWord(list.m_entriesBegin, list.m_entriesEnd, list.m_entriesCap, entry, env);
        } else if (env.Allowed(id)) {
            Entry* entry = NewEntry(env);
            entry->m_sprite = sprite;
            entry->m_frame = def->m_Icon;
            entry->m_id = static_cast<int32_t>(id);
            SetTooltip(*entry, tip, env);
            entry->m_hasCount = 1;
            const int32_t finished = env.Finished(id);
            const int32_t total = env.Total(id);
            env.FormatCount(entry->m_text, total);
            entry->m_color = total == 0 ? kCountNone : finished == total ? 0 : kCountBuilding;
            AppendWords(list.m_entriesBegin, list.m_entriesEnd, list.m_entriesCap, entry, env);
        }
    }
}

// PopulateSurfaces: the scenery of category 1 (paths and grass). A definition without the path flag is the path thumbnail (frame =
// the current tileset), the others the grass one (frame = the definition's icon). Always switches the periodic refresh off, also without sceneries.
template<class Env> void PopulateSurfaces(List& list, Env& env)
{
    const uint32_t count = env.SceneryCount();
    for (uint32_t id = 0; id < count; ++id) {
        const tables::SceneryDef* def = env.Scenery(id);
        if (def->m_Category != 1) continue;
        char tip[128];
        env.SceneryTip(def->m_key.c_str(), tip);
        Entry* entry = NewEntry(env);
        if (def->m_PathFlag == 0) {
            entry->m_sprite = env.ThumbPath();
            entry->m_frame = env.Tileset();
        } else {
            entry->m_sprite = env.ThumbGrass();
            entry->m_frame = def->m_Icon;
        }
        entry->m_id = static_cast<int32_t>(id);
        SetTooltip(*entry, tip, env);
        gui_lists::AppendWord(list.m_entriesBegin, list.m_entriesEnd, list.m_entriesCap, entry, env);
    }
    list.m_refresh = 0;
}

// PopulateScenery: the scenery of `category` with the caller's sprite name; the periodic refresh is switched off.
template<class Env> void PopulateScenery(List& list, int32_t category, const char* sprite, Env& env)
{
    const uint32_t count = env.SceneryCount();
    for (uint32_t id = 0; id < count; ++id) {
        const tables::SceneryDef* def = env.Scenery(id);
        if (def->m_Category != category) continue;
        char tip[128];
        env.SceneryTip(def->m_key.c_str(), tip);
        Entry* entry = NewEntry(env);
        entry->m_sprite = sprite;
        entry->m_frame = def->m_Icon;
        entry->m_id = static_cast<int32_t>(id);
        SetTooltip(*entry, tip, env);
        gui_lists::AppendWord(list.m_entriesBegin, list.m_entriesEnd, list.m_entriesCap, entry, env);
    }
    list.m_refresh = 0;
}

// The counter label of an icon (the two icon updates build it the same way): the entry's text, the colour, and the icon's left edge + 3 / top
// edge as read from its current bounds. Bytes 0x80..0x83 are zeroed and the heap's old content stays after the text.
template<class Env> Label* MakeLabel(const Entry& entry, Widget& icon, Env& env)
{
    auto* label = env.template NewRecord<Label>();
    label->m_font = 0;
    const Char16* from = entry.m_text;
    Char16* to = label->m_text;
    while ((*to++ = *from++) != 0) {}
    label->m_param = kLabelParam;
    int32_t out[4];
    label->m_x = Add(env.Bounds(icon, out)[0], kLabelMarginX);
    int32_t out2[4];
    label->m_y = env.Bounds(icon, out2)[1];
    label->m_color = entry.m_color;
    return label;
}

template<class Env> void CreateImageWidgets(List& list, Env& env)
{
    if (list.m_entriesBegin == list.m_entriesEnd) return;
    ui::SpriteInstance* sprite = env.CreateSprite(list.m_entriesBegin[0]->m_sprite);
    const ui::SpriteAnim& anim = *sprite->m_sprite->m_anims[sprite->m_animIndex];
    const int32_t width = anim.m_width, height = anim.m_height;
    env.ReleaseSprite(sprite);
    int32_t out[4];
    const int32_t* bounds = env.Bounds(list, out);
    const int32_t left = bounds[0], right = bounds[2];
    int32_t rowTop = bounds[1];
    const int32_t rowBottom = Add(rowTop, height);
    int32_t x0 = left, x1 = Add(width, left);
    for (uint32_t i = 0; i < Count(list.m_entriesBegin, list.m_entriesEnd); ++i) {
        Entry* entry = list.m_entriesBegin[i];
        void* memory = env.template NewRecord<ui::GuiImageData>();
        Widget* icon = memory ? env.ConstructImage(memory, x0, rowTop, x1, rowBottom, entry->m_sprite) : nullptr;
        if (entry->m_tooltip) env.SetTooltip(*icon, entry->m_tooltip);
        int32_t nextX0 = Add(x1, list.m_spacingX);
        int32_t nextX1 = Add(nextX0, width);
        if (right < nextX1) {
            nextX0 = left;
            nextX1 = Add(left, width);
            rowTop = Add(Add(rowTop, list.m_spacingY), height);
        }
        env.SetFrame(*icon, entry->m_frame);
        gui_lists::AppendWord(list.m_imagesBegin, list.m_imagesEnd, list.m_imagesCap, icon, env);
        if (entry->m_hasCount)
            gui_lists::AppendWord(list.m_labelsBegin, list.m_labelsEnd, list.m_labelsCap, MakeLabel(*entry, *icon, env), env);
        x0 = nextX0;
        x1 = nextX1;
    }
}

// RefreshImagesAndCounts: every existing icon takes its entry's sprite and frame again, and counted entries get a new label (the caller
// has dropped the old ones). The icon vector sets the count; the entry vector must be at least as long.
template<class Env> void RefreshImagesAndCounts(List& list, Env& env)
{
    for (uint32_t i = 0; i < Count(list.m_imagesBegin, list.m_imagesEnd); ++i) {
        Entry* entry = list.m_entriesBegin[i];
        Widget* icon = list.m_imagesBegin[i];
        env.SetSprite(*icon, entry->m_sprite);
        env.SetFrame(*icon, entry->m_frame);
        if (entry->m_hasCount)
            gui_lists::AppendWord(list.m_labelsBegin, list.m_labelsEnd, list.m_labelsCap, MakeLabel(*entry, *icon, env), env);
    }
}

template<class Env> void RefreshSelection(List& list, Env& env)
{
    if (list.m_selected == -1) list.m_selected = 0;
    const int32_t selected = list.m_selected;
    if (list.m_imagesBegin != list.m_imagesEnd) {
        const int32_t id = list.m_entriesBegin[selected]->m_id;
        if (list.m_kind >= 0 && list.m_kind <= 2)
            env.SetContext(kBuildingContext, id);
        else if (list.m_kind >= 3 && list.m_kind <= 9)
            env.SetContext(kSceneryContext, id);
    }
    switch (list.m_kind) {
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9: env.SetMode(kModeScenery); break;
    case kSurfacesKind: env.SetMode(env.PickedScenery()->m_PathFlag != 0 ? kModeGrass : kModePath); break;
    }
}

} // namespace obl::gui_image_list
