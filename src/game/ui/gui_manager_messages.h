#pragma once
#include "core/text16.h"
#include "game/ui/gui_manager_types.h"
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace obl::gui_manager_messages {
using Manager = ui::GuiManager;
struct Rect {
    int32_t x0, y0, x1, y1;
};

inline constexpr uint32_t kBalloonColor = 0x2a0fb2;
inline constexpr int kBalloonFlags = 2;
inline constexpr int kWideScreenWidth = 800; // narrower screens use the 640 layout
inline constexpr float kMaxTickerStep = 0.5f; // dt is clamped, the balloon never jumps over a long frame
inline constexpr double kTickerPixelsPerSecond = -80.0; // scaled by the game speed

inline bool CanShowMessage(const Manager* manager)
{
    return manager->m_balloonActive == 0;
}

// The balloon is created at the right edge of the strip, one pixel wider than its text, and scrolls left (UpdateTickerAnimation).
// The mode is how many times it enters: 2 = scroll through twice, 1 = scroll in and stay.
template<class Env> void ShowMessage(Env& env, Manager* manager, const Char16* text, int mode)
{
    if (manager->m_balloon) {
        env.DeleteBalloon(manager->m_balloon);
        manager->m_balloon = nullptr;
    }
    auto& strip = *manager;
    manager->m_messageMode = mode;
    manager->m_balloonActive = 1;
    int width = env.MeasureText(manager->m_textRenderers[0], text);
    int font = env.ScreenWidth() < kWideScreenWidth ? 3 : 0;
    manager->m_balloon = static_cast<ui::GuiWidget*>(env.CreateBalloon(strip.m_tickerRight - 1, strip.m_tickerTop,
            width + 1 + strip.m_tickerRight - 1, strip.m_tickerBottom, text, font, kBalloonColor, kBalloonFlags));
    env.PlayNewMessage();
}

template<class Env> void DropMessage(Env& env, Manager* manager)
{
    if (manager->m_balloon) {
        env.DeleteBalloon(manager->m_balloon);
        manager->m_balloon = nullptr;
    }
    manager->m_balloonActive = 0;
}

inline int32_t Truncate(double value)
{
    if (!(value > -2147483649.0 && value < 2147483648.0)) return INT32_MIN;
    return static_cast<int32_t>(value);
}

template<class Env> void UpdateTickerAnimation(Env& env, Manager* manager, float dt)
{
    if (!(dt <= kMaxTickerStep)) dt = kMaxTickerStep;
    if (env.GamePaused() || !manager->m_balloonActive) return;
    auto& strip = *manager;
    void* balloon = manager->m_balloon;
    env.Translate(balloon, Truncate(static_cast<double>(env.GameSpeed()) * (static_cast<double>(dt) * kTickerPixelsPerSecond)), 0);
    Rect bounds = env.Bounds(balloon);
    if (bounds.x0 <= strip.m_tickerLeft + 5) {
        bounds = env.Bounds(balloon);
        if (bounds.x1 < strip.m_tickerRight - 0x28 && manager->m_messageMode == 1) {
            bounds = env.Bounds(balloon);
            env.Translate(balloon, strip.m_tickerLeft - bounds.x0, 0);
            manager->m_balloonActive = 0;
        }
    }
    if (manager->m_balloonActive) {
        bounds = env.Bounds(balloon);
        if (bounds.x1 < strip.m_tickerLeft) {
            --manager->m_messageMode;
            bounds = env.Bounds(balloon);
            env.Translate(balloon, strip.m_tickerRight - bounds.x0, 0);
        }
    }
}

template<class Env> void SetTickerBounds(Env& env, Manager* manager, int left, int top, int right, int bottom)
{
    auto& strip = *manager;
    strip.m_tickerLeft = left;
    strip.m_tickerTop = top;
    strip.m_tickerRight = right;
    strip.m_tickerBottom = bottom;
    if (manager->m_balloonActive) env.Translate(manager->m_balloon, 0, top - env.Bounds(manager->m_balloon).y0);
}

// ---- TickerList ----
inline constexpr int kTickerTextBuffer = 512;
inline const char* TickerListBoxName(int screenWidth)
{
    return screenWidth >= kWideScreenWidth ? "TickerListBox800" : "TickerListBox640";
}

inline void FormatTickerLine(Char16* out, const Char16* dayLabel, uint32_t day, int hour, int quarter, const Char16* text)
{
    if (text16::Format(out, kTickerTextBuffer, u"%ls %d: %02d:%02d - %ls", dayLabel, static_cast<int>(day) + 1, hour, quarter, text) < 0)
        out[kTickerTextBuffer - 1] = 0;
}

template<class Env> void TickerRebuild(Env& env, ui::TickerList* manager)
{
    manager->m_rows = 0;
    bool wide = env.ScreenWidth() >= kWideScreenWidth;
    const char* goButton = wide ? "UI3_bar_message_goto" : "UI2_bar_message_goto";
    int width = wide ? 400 : 0x13a;
    int buttonOffset = wide ? 0x19a : 0x144;
    int font = wide ? 0 : 3;
    if (!manager->m_listBox) return;
    env.ClearRows(manager->m_listBox);
    float hour;
    uint32_t day, objectId;
    const Char16* text = env.BeginHistory(&hour, &day, &objectId);
    while (text) {
        Char16 line[kTickerTextBuffer];
        FormatTickerLine(line, env.DayLabel(), day, env.WholeHour(hour), env.QuarterMinutes(hour), text);
        env.AddText(manager->m_listBox, static_cast<int>(manager->m_rows), 5, width, 0xf, kBalloonFlags, font, kBalloonColor, line, 1);
        if (objectId != 0xffffffffu && objectId != 0xfffffffeu)
            env.SetRowButton(manager->m_listBox, static_cast<int>(manager->m_rows), buttonOffset, goButton, static_cast<int>(objectId));
        ++manager->m_rows;
        text = env.NextHistory(&hour, &day, &objectId);
    }
}

template<class Env> void TickerSetVisible(Env& env, ui::TickerList* manager, uint8_t visible)
{
    manager->m_visible = visible;
    if (!visible) return;
    manager->m_listBox = env.FindListBox(TickerListBoxName(env.ScreenWidth()));
    if (manager->m_listBox) env.DisableListBox(manager->m_listBox);
    TickerRebuild(env, manager);
}

// Jump to the object behind the first row whose button was pressed (the loop stops at the first press).
template<class Env> void TickerUpdate(Env& env, ui::TickerList* manager)
{
    if (!manager->m_visible) return;
    TickerRebuild(env, manager);
    bool pressed = false;
    for (uint32_t row = 0; row < manager->m_rows && !pressed; ++row) {
        int objectId = 0;
        pressed = env.PollRowButton(manager->m_listBox, row, &objectId);
        if (pressed) env.SelectObject(objectId);
    }
}
} // namespace obl::gui_manager_messages
