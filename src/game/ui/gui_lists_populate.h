#pragma once

#include "core/char16.h"
#include "game/ui/gui_combo.h"
#include <cwchar>

namespace obl::gui_populate {

template<class List, class Env> void PopulateDrinks(List& list, bool cocktails, Env& env)
{
    list.m_refresh = 0;
    env.Invalidate(list);
    const uint32_t selected = env.Provider()->m_BeerType;
    gui_combo::ClearItems(list, env);
    const uint32_t count = env.BeerCount();
    uint32_t row = 0;
    for (uint32_t id = 0; id < count; ++id) {
        if (env.Beer(id)->m_scriptFlag) {
            const auto* beer = env.Beer(id);
            env.AddItem(list, cocktails ? beer->m_Cocktail : beer->m_Brand, id);
            if (id == selected) list.m_selected = row;
            ++row;
        }
    }
}

template<class List, class Env> void PopulateStaff(List& list, Env& env)
{
    env.Invalidate(list);
    gui_combo::ClearItems(list, env);
    uint32_t row = 0;
    for (const auto& entry : env.Table(8)) {
        env.AddItem(list, entry.key, entry.value);
        if (entry.value == env.Context(11)) list.m_selected = row;
        ++row;
    }
}

template<class List, class Env> void PopulateEvent(List& list, int slot, Env& env)
{
    env.Invalidate(list);
    gui_combo::ClearItems(list, env);
    auto* provider = env.Provider();
    list.m_selected = 0;
    if (env.SlotActive(provider->m_events, slot)) {
        env.AddItem(list, env.EventInProgress(), 6);
        return;
    }
    uint32_t row = 0;
    for (const auto& entry : env.Table(9)) {
        if (env.Event(entry.value)->m_available) {
            env.AddItem(list, entry.key, entry.value);
            if (env.SlotEvent(provider->m_events, slot) == entry.value) list.m_selected = row;
            ++row;
        }
    }
}

template<class List, class Env> void Populate(List& list, uint32_t dt, Env& env)
{
    env.BaseUpdate(list, dt);
    if (list.m_scroll) list.m_scrollTop = list.m_scroll->m_top;
    if (list.m_refresh) {
        switch (list.m_kind) {
        case 0:
        case 6:
            env.Invalidate(list);
            gui_combo::ClearItems(list, env);
            list.m_refresh = 0;
            for (const auto& e : env.Table(list.m_kind)) env.AddItem(list, e.key, e.value);
            if (list.m_kind == 0) env.SetContext(7, 1);
            break;
        case 1: {
            const int height = env.ScreenHeight();
            list.m_refresh = 0;
            env.Invalidate(list);
            uint32_t row = 0;
            for (const auto& e : env.Resolutions()) {
                if (env.ModeSupported(e.modeIndex)) {
                    env.AddItem(list, e.textKey, e.height);
                    if (e.height == height) list.m_selected = row;
                }
                ++row; // Index in the source table, including unsupported modes.
            }
            break;
        }
        case 2:
            env.Invalidate(list);
            list.m_refresh = 0;
            for (const auto& e : env.Table(2)) env.AddItem(list, e.key, e.value);
            break;
        case 3:
            gui_combo::ClearItems(list, env);
            list.m_refresh = 0;
            for (uint32_t i = 0; i < env.ScenarioCount(); ++i) env.AddLiteral(list, env.Scenario(i), i);
            break;
        case 4: PopulateDrinks(list, false, env); break;
        case 5: {
            gui_combo::ClearItems(list, env);
            Char16 text[128];
            for (int i = 0; i < 100; ++i) {
                env.FormatNumber(text, i);
                env.AddLiteral(list, text, i);
            }
            break;
        }
        case 7: {
            env.Invalidate(list);
            env.ClearItems(list);
            uint32_t row = 0;
            for (const auto& e : env.Table(7)) {
                env.AddItem(list, e.key, e.value);
                if (e.value == env.Context(10)) list.m_selected = row;
                ++row;
            }
            break;
        }
        case 8: PopulateStaff(list, env); break;
        case 9:
        case 10:
        case 11: PopulateEvent(list, list.m_kind - 9, env); break;
        case 12: PopulateDrinks(list, true, env); break;
        }
    }
    env.Update(*list.m_button, dt);
    if (list.m_scroll) {
        env.Update(*list.m_scroll, dt);
        list.m_scrollTop = list.m_scroll->m_top;
    }
}

template<class List, class Item, class Env> void OnItemSelected(List& list, const Item& item, Env& env)
{
    const int value = item.m_value;
    switch (list.m_kind) {
    case 0: env.SetContext(1, value); break;
    case 1:
        switch (value) {
        case 480: env.RequestResolution(640, 480); break;
        case 600: env.RequestResolution(800, 600); break;
        case 768: env.RequestResolution(1024, 768); break;
        case 960: env.RequestResolution(1280, 960); break;
        }
        break;
    case 2: env.SetContext(3, value); break;
    case 3: env.SetContext(4, value); break;
    case 4:
    case 12: env.SetBeer(env.Provider(), value); break;
    case 6: env.SetContext(2, value); break;
    case 7:
        env.SetContext(10, value);
        if (value != 6) env.SetContext(11, value);
        break;
    case 8: env.SetContext(11, value); break;
    case 9:
    case 10:
    case 11:
        if (value != 6) env.SetEvent(env.Provider(), list.m_kind - 9, value);
        break;
    }
}

} // namespace obl::gui_populate
