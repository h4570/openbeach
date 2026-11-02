#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/objects/guest_shadow.h"
#include "game/objects/guest_appearance.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/tables/world_params_tables.h"
#include "core/ascii_case.h"
#include "core/call_counter.h"

#include <cstdint>
#include <cstring>

// The guest's look and what it reports about it (src/game/objects/guest_appearance.h, guest look, docs/architecture/objects.md): RandomizePalette and
// RandomizeAttachedPalette (the script calls them through the guest's CallFunction, ids in src/game/objects/guest_call_table.h), and the getters
// GetPaletteTone, GetFoodIllness, GetSunburnSeverity, IsApproachable (reps and security guards)
// and HasState. The palette functions write the palette index of a sprite instance, not the guest.
namespace obl::object {
using game::GetWorld;
using game::Rand;

namespace {

using object::Guest;
using ui::Sprite;
using ui::SpriteInstance;

// Call counters (`call-report <name>`).
test::CallCounter g_addFoodIll("objects.guest-add-food-ill");
test::CallCounter g_randomise("objects.guest-randomise-pallete");
test::CallCounter g_attached("objects.guest-randomise-attached-pallete");
test::CallCounter g_tone("objects.guest-get-palette-tone");
test::CallCounter g_food("objects.guest-get-food-illness");
test::CallCounter g_sunburn("objects.guest-get-sunburn-severity");
test::CallCounter g_approachable("objects.guest-is-approachable");
test::CallCounter g_hasState("objects.guest-has-state");

SpriteInstance* SpriteOf(Guest& guest)
{
    return static_cast<SpriteInstance*>(guest.m_sprite);
}

// ---- the functions -----------------------------------------------------------------------------------------------------------------------

// A missing sprite instance, sprite or guest definition ends in UnhandledInput.
bool Randomise(Guest& guest)
{
    SpriteInstance* sprite = SpriteOf(guest);
    if (!sprite || !sprite->m_sprite || !guest.m_definition) return false;
    const tables::GuestDef& def = *guest.m_definition;
    // The sprite name is only compared (and only for a male guest).
    if (def.m_Gender != 1 && !sprite->m_name) return false;
    const bool fatBloke = def.m_Gender != 1 && ascii::EqualsNoCase(sprite->m_name, "FAT_BLOKE");
    sprite->m_paletteIndex = guest_appearance::RandomPalette(
            def.m_Gender, def.m_Colour != 0, fatBloke, sprite->m_sprite->m_paletteCount, [] { return Rand(); });
    return true;
}

bool Attached(Guest& guest)
{
    auto* attached = static_cast<SpriteInstance*>(guest.m_attachedSprite);
    if (!attached) return true;
    if (!attached->m_name || !attached->m_sprite) return false;
    const bool luggage = ascii::EqualsNoCase(attached->m_name, "LUGGAGE");
    SpriteInstance* own = SpriteOf(guest);
    if (luggage && (!own || !own->m_sprite)) return false;
    const auto index = guest_appearance::RandomAttachedPalette(luggage, luggage ? own->m_paletteIndex : 0,
            luggage ? own->m_sprite->m_paletteCount : 0, attached->m_sprite->m_paletteCount, [] { return Rand(); });
    if (!index) return false;
    attached->m_paletteIndex = *index;
    return true;
}

double Tone(const Guest& guest)
{
    // The clock is read only on the fade-in branch: a sunburnt guest needs no world.
    const layout::World* world = GetWorld();
    const int32_t today = world ? world->m_gameDay : 0;
    const float time = world ? world->m_gameDayTime : 0.0f;
    return guest_appearance::PaletteTone(guest.m_sunBurnLevel, today, time, guest.m_DayArrived, guest.m_timeArrived);
}

// ---- handlers -------------------------------------------------------------------------------------------------------------------------

} // namespace

void GuestAddFoodIll(Guest& guest, float amount)
{
    guest.m_foodIll = guest_appearance::AddFoodIll(guest.m_foodIll, amount);
    g_addFoodIll.Count();
}

float GuestSunburnSeverity(const Guest& guest)
{
    g_sunburn.Count();
    return guest_appearance::SunburnSeverity(guest.m_sunBurnLevel);
}

void Guest::RandomizePalette()
{
    if (!Randomise(*this)) fatal::UnhandledInput("objects.guest-randomise-pallete", "the guest has no sprite or definition");
    g_randomise.Count();
}

void Guest::RandomizeAttachedPalette()
{
    if (!Attached(*this)) fatal::UnhandledInput("objects.guest-randomise-attached-pallete", "the attached sprite has no name or palette");
    g_attached.Count();
}

double GuestPaletteTone(Guest& guest)
{
    g_tone.Count();
    return Tone(guest);
}

float GuestFoodIllness(Guest& guest)
{
    g_food.Count();
    return guest.m_foodIll;
}

bool GuestIsApproachable(Guest& guest)
{
    g_approachable.Count();
    return guest_appearance::IsApproachable(static_cast<uint32_t>(guest.m_state));
}

// The two getters the GUI and the reps call only on a click or a patrol, through the handlers (tests/guest.txt).
void ForceGuestAppearance()
{
    Guest* guest = guest_shadow::GuestAt(0);
    if (!guest) return;
    guest_shadow::GuestBackup backup(guest);
    for (const float level : {0.0f, -0.5f, -2.0f}) {
        guest->m_sunBurnLevel = level;
        GuestSunburnSeverity(*guest);
    }
    for (const int32_t state : {0, 0xc, 0xd, 0x3a, 0x40}) {
        guest->m_state = state;
        GuestIsApproachable(*guest);
    }
    // Natural play reaches these two through the GUI and the draw; which guests that touches depends on the world, so the counters do not rely on it.
    GuestPaletteTone(*guest);
    GuestFoodIllness(*guest);
    GuestAddFoodIll(*guest, 0.25f);
}

} // namespace obl::object

namespace obl::object {
bool Guest::HasState(uint32_t state)
{
    g_hasState.Count();
    return guest_appearance::HasState(state);
}
} // namespace obl::object
