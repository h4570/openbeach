#pragma once

#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"

#include <algorithm>
#include <cstdint>

namespace obl::guest_draw {
using object::Guest;
using ui::SpriteInstance;
inline SpriteInstance* Sprite(void* p)
{
    return static_cast<SpriteInstance*>(p);
}
inline const ui::SpriteAnim& Anim(const SpriteInstance& s)
{
    return *s.m_sprite->m_anims[s.m_animIndex];
}
inline int Trunc(double v)
{
    return static_cast<int>(v);
}
inline int MoodFrame(float happiness)
{
    const int frame = Trunc(double(happiness) * 5);
    return frame >= 5 ? frame - 1 : frame;
}

template<class Env> void Draw(Guest& g, Env& env)
{
    if (g.m_insideBuilding) return;
    if (auto* attached = Sprite(g.m_attachedSprite)) {
        // The saved offsets are zero-extended before they are subtracted.
        float position[3] = {float(double(g.m_WorldXYZ[0]) - uint32_t(g.m_AttachedOffsetX)),
                float(double(g.m_WorldXYZ[1]) - uint32_t(g.m_AttachedOffsetY)), g.m_WorldXYZ[2]};
        attached->m_direction = std::min(Sprite(g.m_sprite)->m_direction, int(Anim(*attached).m_directions) - 1);
        env.InWorld(attached, position, env.Time());
    }
    float tone;
    if (g.m_sunBurnLevel < 0)
        tone = std::min(float(1.0 - double(g.m_sunBurnLevel)), 2.0f);
    else
        tone = std::min(
                float(((double(env.DayTime()) - g.m_timeArrived) + double(env.Day() - g.m_DayArrived) * 24) * double(0.013888889f)), 1.0f);
    env.Toned(Sprite(g.m_sprite), g.m_WorldXYZ, env.Time(), tone, g.m_foodIll, env.DevToggle() ? 1u : uint32_t(g.m_topless));
}

template<class Env> void Marker(int* p, float happiness, SpriteInstance* mood, Env& env)
{
    mood->m_frameIndex = MoodFrame(happiness);
    const auto bright = env.Color(255, 255, 0), dark = env.Color(201, 202, 0);
    const double zoom = env.Zoom();
    p[0] = Trunc(p[0] * zoom);
    p[1] = Trunc(p[1] * zoom);
    const int half = Trunc(zoom * 25), arm = Trunc(zoom * 17), bottom = Trunc(double(p[1]) + half * 2.5);
    p[0] -= half;
    const int top = p[1] - half;
    env.Line(p[0] + 1, top, p[0] + arm, top, bright);
    env.Line(p[0], top, p[0], top + arm, dark);
    env.Line(p[0] + 2, top + 1, p[0] + arm, top + 1, dark);
    env.Line(p[0] + 1, top, p[0] + 1, top + arm, bright);
    env.Clipped(mood, p[0] + 4, top + 4);
    env.Line(p[0], bottom, p[0] + arm, bottom, dark);
    env.Line(p[0], bottom, p[0], bottom - arm, dark);
    env.Line(p[0] + 1, bottom - 1, p[0] + arm, bottom - 1, bright);
    env.Line(p[0] + 1, bottom - 1, p[0] + 1, bottom - arm, bright);
    p[0] += half * 2;
    env.Line(p[0], top, p[0] - arm, top, bright);
    env.Line(p[0], top, p[0], top + arm, bright);
    env.Line(p[0] - 1, top + 1, p[0] - arm, top + 1, dark);
    env.Line(p[0] - 1, top + 1, p[0] - 1, top + arm, dark);
    env.Line(p[0], bottom, p[0] - arm, bottom, dark);
    env.Line(p[0], bottom - 1, p[0], bottom - arm, bright);
    env.Line(p[0], bottom - 1, p[0] - arm, bottom - 1, bright);
    env.Line(p[0] - 1, bottom - 2, p[0] - 1, bottom - arm, dark);
}

template<class Env> void StatBar(Guest& g, int* p, Env& env)
{
    const double zoom = env.Zoom();
    p[1] = Trunc(p[1] * zoom);
    const int half = Trunc(zoom * 25), y = p[1] + half * 2;
    p[0] = Trunc(p[0] * zoom) - half;
    float value;
    bool reversed = false;
    switch (env.Stat()) {
    case 1: value = g.m_happiness; break;
    case 5:
        value = g.m_sunBurnLevel < 0 ? std::min(-g.m_sunBurnLevel, 1.0f) : 0;
        reversed = true;
        break;
    case 6:
        value = g.m_desires[4];
        reversed = true;
        break;
    case 7:
        value = g.m_drunk;
        reversed = true;
        break;
    case 8:
        value = g.m_desires[14];
        reversed = true;
        break;
    case 9:
        value = g.m_foodIll;
        reversed = true;
        break;
    case 10: value = g.m_opinion[3]; break;
    case 11: value = g.m_opinion[6]; break;
    case 12: value = g.m_opinion[1]; break;
    case 13: value = g.m_opinion[0]; break;
    case 14: value = g.m_opinion[5]; break;
    case 15: value = g.m_opinion[2]; break;
    case 16: value = g.m_opinion[4]; break;
    default:
        Sprite(g.m_happinessSprite)->m_frameIndex = MoodFrame(g.m_happiness);
        env.Clipped(Sprite(g.m_happinessSprite), p[0] + 4, p[1] - half + 4);
        return;
    }
    int width = Trunc(double(value) * 35);
    if (width == 0) width = 1;
    auto color = env.Color(26, 219, 123);
    const auto background = env.Color(220, 232, 255);
    const double v = value;
    double r = 255, green = 200;
    if (reversed) {
        if (v < 0.5) r = v * 2 * 255;
        if (v > 0.5) green = (1 - v) * 2 * 200;
    } else {
        if (v > 0.5) r = (1 - v) * 2 * 255;
        if (v < 0.5) green = v * 2 * 200;
    }
    color = env.Color(Trunc(r), Trunc(green), 0);
    // Width and height stay unscaled, including out-of-range values.
    env.Fill(p[0], y, width, 4, color);
    env.Fill(p[0] + width, y, 35 - width, 4, background);
}

template<class Env> void Overlay(Guest& g, Env& env)
{
    if (g.m_state == 0x2b || (g.m_state == 0x29 && g.m_ActionState == 14)) return;
    if (g.m_SelectLevel > 0 && g.m_SelectLevel < 3) {
        int p[2];
        env.Anchor(g, p);
        env.Marker(p, g.m_happiness, Sprite(g.m_happinessSprite));
    }
    if (env.ShowStats()) {
        int p[2];
        env.Anchor(g, p);
        env.StatBar(g, p);
    }
    if (g.m_moodIconTime > 0) {
        int p[2];
        env.Anchor(g, p);
        auto* mood = g.m_moodIcon;
        const double zoom = env.Zoom();
        env.Clipped(mood, Trunc(p[0] * zoom) + Trunc(zoom * -25), Trunc(p[1] * zoom) - Anim(*mood).m_height - 2);
    }
}
} // namespace obl::guest_draw
