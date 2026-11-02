#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_performers.h"
#include "game/ui/sprite.h"

// SetVariant for the six performer classes (docs/architecture/objects.md, "SetVariant"). The act or outfit picks the sprite and a random palette of it; the order
// of the `rand` draws is part of the contract (the world's CreateObjectFromMessage runs them in a fixed sequence, so the later draws of the world depend on it).
//   Env::CreateSprite(const char* name)   creates and returns the named sprite instance from the global bank
//   Env::Rand                           the game's rand
// A sprite without palettes (count 0) divides by zero; real sprites always have some.
namespace obl::performer {

namespace detail {
// "if (count <= index) index = count - 1", the unsigned-looking clamp of every variant: palette index of the sprite instance.
inline void Palette(ui::SpriteInstance& sprite, int index)
{
    const int count = sprite.m_sprite->m_paletteCount;
    sprite.m_paletteIndex = count <= index ? count - 1 : index;
}
inline ui::SpriteInstance& Own(object::Mover& performer)
{
    return *static_cast<ui::SpriteInstance*>(performer.m_sprite);
}
// The two draws of the girl variants: the first picks the tens (modulo `tens`), the second the units.
template<class Env> void GirlPalette(ui::SpriteInstance& sprite, int tens, Env& env)
{
    const int first = env.Rand();
    const int second = env.Rand();
    Palette(sprite, second % 7 + 2 + (first % tens) * 10);
}
template<class Env> void AnyPalette(ui::SpriteInstance& sprite, Env& env)
{
    Palette(sprite, env.Rand() % sprite.m_sprite->m_paletteCount);
}
} // namespace detail

template<class Env> void SetVariant(object::Aerobic& performer, int variant, Env& env)
{
    performer.m_variant = static_cast<uint32_t>(variant);
    auto* sprite = env.CreateSprite(variant == 0 ? "Gail" : "Shaz");
    performer.m_sprite = sprite;
    detail::GirlPalette(*sprite, 6, env);
}

template<class Env> void SetVariant(object::Cabaret& performer, int variant, Env& env)
{
    performer.m_variant = static_cast<uint32_t>(variant);
    const char* name = nullptr;
    switch (variant) {
    case 0: name = "GAIL"; break;
    case 1: name = "SHAZ"; break;
    case 2: name = "FAT_BLOKE"; break;
    case 3: name = "FIT_BLOKE"; break;
    default: return; // any other act keeps whatever sprite the object has (none)
    }
    auto* sprite = env.CreateSprite(name);
    performer.m_sprite = sprite;
    if (variant < 2)
        detail::GirlPalette(*sprite, 4, env);
    else
        detail::AnyPalette(*sprite, env);
}

// The sprite is the constructor's "Gail".
template<class Env> void SetVariant(object::Catwalk& performer, int variant, Env& env)
{
    performer.m_variant = static_cast<uint32_t>(variant);
    detail::GirlPalette(detail::Own(performer), 4, env);
}

// Use the stage event sprite created by Construct.
template<class Env> void SetVariant(object::Singer& performer, int variant, Env& env)
{
    performer.m_variant = static_cast<uint32_t>(variant);
    detail::GirlPalette(detail::Own(performer), 6, env);
}

// Three draws: a palette that the next two overwrite (kept: it is the first write and the draw count matters), then a parity draw that
// chooses between the 41..60 and the 11..40 range.
template<class Env> void SetVariant(object::WeightLifter& performer, int variant, Env& env)
{
    performer.m_variant = static_cast<uint32_t>(variant);
    ui::SpriteInstance& sprite = detail::Own(performer);
    detail::AnyPalette(sprite, env);
    const bool even = (env.Rand() & 1) == 0;
    const int units = env.Rand();
    detail::Palette(sprite, even ? units % 20 + 41 : units % 30 + 11);
}

// Outfit 3 is the contestant ("wtsc"); every other one burns a draw before the sprite is made (the result is never used).
template<class Env> void SetVariant(object::WetTShirt& performer, int variant, Env& env)
{
    performer.m_variant = static_cast<uint32_t>(variant);
    const char* name = "wtsc";
    if (variant != 3) {
        env.Rand();
        name = "wts";
    }
    auto* sprite = env.CreateSprite(name);
    performer.m_sprite = sprite;
    detail::AnyPalette(*sprite, env);
}

} // namespace obl::performer
