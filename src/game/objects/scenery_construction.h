#pragma once

#include "game/objects/scenery_names.h"
#include "core/ascii_case.h"
#include "game/objects/object_scenery.h"
#include "game/ui/sprite.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// Construct (Scenery's constructor, after Prop's, which copied the key into the name) and Destruct (Scenery's destructor):
// docs/architecture/objects.md. Pure logic; the game side (src/game/objects/scenery_lifecycle.cpp) supplies the Env.
//
// Env:
//   const SceneryDef* Definition(const char* key)     scenery definition of the world parameters
// void SetGridSize(Scenery&, uint8_t w, uint8_t h) grid width and height
// SpriteInstance* CreateSprite(const char* name) sprite from the sprite bank
// void SetAnim(SpriteInstance*, const char* name) start the named animation at time 0
//   int Rand()                                        the game's RNG
// Scenery::DrawFunc LightFunc / NoOpFunc SceneryDrawLight / SceneryDrawNothing
// const char* NoMenuText the GUI's "None" text
// void ReleaseSprite(SpriteInstance*) return the sprite to the bank
namespace obl::scenery_construction {

using object::Scenery;
using ui::SpriteInstance;

// The decorations that give light: the table is walked in this order, the first match wins. Keys compare without case.
// Streetlight-style entries draw their lights-on and lights-off hours from rand(); the fire has fixed hours.
struct LightEntry {
    const char* name;
    const char* sprite;
};
inline constexpr LightEntry kLights[] = {
        {"STREET_LAMP", "streetlightmap"},
        {"FOUNTAIN01", "fountain01_Lightmap"},
        {"FOUNTAIN02", "fountain02_Lightmap"},
        {"FOUNTAIN03", "fountain03_Lightmap"},
        {"FOUNTAIN04", "fountain04_Lightmap"},
        {"STATUE01", "statue01_Lightmap"},
        {"LOWLIGHT01", "light01_Lightmap"},
        {"LOWLIGHT02", "light02_Lightmap"},
        {"LOWLIGHT03", "light03_Lightmap"},
};
inline constexpr const char* kStreetLamp = "STREETLAMP";
inline constexpr const char* kStreetLampSprite = "streetlightmap";
inline constexpr const char* kBeachFire = "BEACHFIRE";
inline constexpr const char* kBeachFireSprite = "beachfiremap";
inline constexpr const char* kStringLight = "STRINGLIGHT";
inline constexpr const char* kStringLightSprite = "lightstring_light";

// (the float 1/32767), (19.5) and (5.5): computed at 53-bit precision and stored as a float.
inline constexpr double kRandScale = 3.0518509447574615e-05;
inline float LightsOnHour(int r)
{
    return static_cast<float>(static_cast<double>(r) * kRandScale + 19.5);
}
inline float LightsOffHour(int r)
{
    return static_cast<float>(static_cast<double>(r) * kRandScale + 5.5);
}
inline constexpr float kBeachFireOn = 19.0f;
inline constexpr float kBeachFireOff = 6.5f;

template<class Env> void SetupLight(Scenery& s, Env& env)
{
    const char* name = s.m_Name;
    const char* lightSprite = nullptr;
    bool fixed = false;
    if (ascii::EqualsNoCase(kStreetLamp, name))
        lightSprite = kStreetLampSprite;
    else {
        for (const LightEntry& entry : kLights)
            if (ascii::EqualsNoCase(entry.name, name)) {
                lightSprite = entry.sprite;
                break;
            }
        if (!lightSprite && ascii::EqualsNoCase(kBeachFire, name)) {
            lightSprite = kBeachFireSprite;
            fixed = true;
        }
        if (!lightSprite && ascii::EqualsNoCase(kStringLight, name)) lightSprite = kStringLightSprite;
    }
    if (!lightSprite) {
        s.m_drawLight = env.NoOpFunc();
        s.m_lightSprite = nullptr;
        return;
    }
    s.m_drawLight = env.LightFunc();
    s.m_lightSprite = env.CreateSprite(lightSprite);
    if (fixed) {
        s.m_lightsOn = kBeachFireOn;
        s.m_lightsOff = kBeachFireOff;
        return;
    }
    s.m_lightsOn = LightsOnHour(env.Rand());
    s.m_lightsOff = LightsOffHour(env.Rand());
}

template<class Env> Scenery* Construct(Scenery& s, const char* key, Env& env)
{
    const tables::SceneryDef* def = env.Definition(key);
    s.m_definition = def;
    std::memcpy(s.m_ObjectName, key, std::strlen(key) + 1); // unbounded: keys always fit the name
    env.SetGridSize(s, static_cast<uint8_t>(def->m_GridWidth), static_cast<uint8_t>(def->m_GridHeight));
    SpriteInstance* sprite = env.CreateSprite(def->m_Sprite.c_str());
    s.m_sprite = sprite;
    if (def->m_Anim[0]) env.SetAnim(sprite, def->m_Anim.c_str());
    // A sprite whose current animation has no directions faces "-1".
    const ui::SpriteAnim* anim = sprite->m_sprite->m_anims[sprite->m_animIndex];
    sprite->m_direction = anim->m_directions == 0 ? -1 : 0;
    s.m_AttachedGuest[0] = s.m_AttachedGuest[1] = s.m_AttachedGuestState[0] = s.m_AttachedGuestState[1] = -1;
    s.m_MaxNumAttachedGuests = ascii::EqualsNoCase(key, "BENCH") ? 2 : 1;
    SetupLight(s, env);
    s.m_ScrXYRect = screen::Rect{0, 0, 0, 0};
    s.m_vandalised = 0;
    s.m_flashLight = 0;
    s.m_RepairState = 0;
    s.m_NumAttachedGuests = 0;
    s.m_PreVandlismAnim = 0;
    // The fill starts at 0: `capacity * 0.7 < 0` never holds. A bin whose capacity is 0 therefore counts as full (fill == capacity).
    const uint32_t capacity = static_cast<uint32_t>(def->m_Capacity);
    const uint32_t fill = 0;
    s.m_NumItems = static_cast<int32_t>(fill);
    if (scenery_names::IsBin(s.m_Name)) env.SetAnim(sprite, fill == capacity ? "bin_fullbin_static" : "bin_emptybin_static");
    s.m_bakePending = 1;
    s.m_soundPlaying = 0;
    s.m_hasMenu = !ascii::EqualsNoCase(def->m_Menu.c_str(), env.NoMenuText()) ? 1 : 0;
    s.m_canSelect = 0;
    return &s;
}

// The light sprite goes back to the bank; the rest is the base's.
template<class Env> void Destruct(Scenery& s, Env& env)
{
    if (s.m_lightSprite) env.ReleaseSprite(s.m_lightSprite);
}

} // namespace obl::scenery_construction
