#include "core/char16.h"
#include "game/ui/sprite_bank.h"
#include "game/world/world_script_host_game.h"
#include "game/ui/sprite.h"
#include "game/objects/guest_lifecycle_driver.h"
#include "game/objects/object_moving.h"

#include "core/code_page.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/guest.h"
#include "game/objects/guest_construction.h"
#include "game/objects/guest_lifecycle.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <cstring>
#include "game/ui/text_table.h"
#include "game/world/world_params.h"

namespace obl::object {
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;
using game::GetWorldScriptHost;
using game::GuestAttractionCounts;
using game::GuestConstructDefinition;
using game::GuestPopulation;
using game::Rand;

namespace {
using namespace layout;
using obl::object::Guest;
using ui::SpriteInstance;
using namespace tables;

// Call counters (`call-report <name>`); `g_fixture` carries `call-drive GuestLifecycleAttachedFixture`.
test::CallCounter g_destructCalls("objects.guest-destruct"), g_destroyCalls("objects.guest-destroy"),
        g_constructCalls("objects.guest-construct");
test::CallCounter g_fixture("GuestLifecycleAttachedFixture");

void ReleaseSprite(void* sprite)
{
    (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(sprite);
}

// Guest's teardown: the name and sprites go back and the population statistics of the construction are undone (the counted slot is
// read from the definition now); Person's and the base destructors follow.
void ReleaseGuestParts(Guest& guest)
{
    if (guest.m_name) ::operator delete(guest.m_name);
    if (guest.m_attachedSprite) ReleaseSprite(guest.m_attachedSprite);
    ReleaseSprite(guest.m_happinessSprite);
    ReleaseSprite(guest.m_moodIcon);
    const tables::GuestDef* definition = guest.m_definition;
    --GuestAttractionCounts()[guest_lifecycle::AttractionSlot(definition->m_Attractive, definition->m_Gender)];
    --GuestPopulation();
}

// ---- Construct -------------------------------------------------------------------------------------------------------------------------

class ConstructEnv {
public:
    const tables::GuestDef* Definition(const void* params)
    {
        WorldParams* world = GetWorldParams();
        const int id = world::world_params::GetGuestId(world, static_cast<const char*>(params));
        const auto* definition = world::world_params::GetGuestDefinition(world, id);
        GuestConstructDefinition() = definition;
        return definition;
    }
    int Rand() { return game::Rand(); }
    float GameTime() { return GetWorld()->m_gameTime; }
    int32_t GameDay() { return GetWorld()->m_gameDay; }
    float DayTime() { return GetWorld()->m_gameDayTime; }
    int32_t* Counts() { return GuestAttractionCounts().data(); }
    int32_t& Population() { return GuestPopulation(); }
    void* ScriptLookup(const char* name) { return GetWorldScriptHost()->Find(name); }
    uint32_t NameCount(int list) { return static_cast<uint32_t>(GetWorldParams()->m_nameCounts[list]); }
    const char* NameKey(int list, uint32_t index) { return GetWorldParams()->m_names[(list == 2 ? 0x118 : list == 1 ? 0x8c : 0) + index]; }
    void* New(size_t size) { return ::operator new(size); }
    void Delete(void* memory) { ::operator delete(memory); }
    void Widen(const char* source, Char16* destination, size_t count)
    {
        code_page::Widen(source, reinterpret_cast<char16_t*>(destination), count);
    }
    const Char16* Lookup(const Char16* key) { return ui::text_table::LookupW(key); }
    void* CreateSprite(const char* name) { return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateInstance(name); }
};

// The constructor does not write m_CurID / m_CurTypeID, the m_LastID / m_lastActivity ring and a few more fields, so without clearing a
// new guest would keep whatever the heap block held before. SearchDesire passes m_CurID to the provider search as the building to skip:
// a block that was a guest before could still name a live building (the first search then refused it), any other block named nothing. The outcome
// would follow the heap layout, which would make runs of one scenario differ (guest constructor, docs/architecture/objects.md). The object is cleared before the constructor
// and m_CurID starts as "no building", like m_DestObjectID; a loaded guest gets both fields from the save afterwards.
// ---- Drivers ---------------------------------------------------------------------------------------------------------------------------

// `call-drive GuestLifecycleAttachedFixture <n>`: odd `n` gives every guest an attached sprite (the carried item), even `n` takes it away, so a world
// save/load round trip meets both branches of the record.
void DriveAttachedFixture(int count)
{
    object::Manager* manager = GetObjectManager();
    if (!manager) return;
    for (uint32_t i = 0; i < manager->m_guests.size(); ++i) {
        auto* guest = static_cast<Guest*>(manager->m_guests[i]);
        if (!guest->m_sprite) continue;
        if (guest->m_attachedSprite) ReleaseSprite(guest->m_attachedSprite);
        guest->m_attachedSprite = nullptr;
        if (count % 2)
            guest->m_attachedSprite = (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))
                                              ->CreateObservedInstance(static_cast<SpriteInstance*>(guest->m_sprite)->m_name);
        g_fixture.Count();
    }
}
} // namespace

void RegisterGuestLifecycleDriver()
{
    g_fixture.SetDriver(&DriveAttachedFixture);
}

} // namespace obl::object

namespace obl::object {
Guest::Guest(const char* params) : Person(params)
{
    object::g_constructCalls.Count();
    object::ConstructEnv env;
    guest::construct::Construct(*this, params, env);
    m_CurID = -1;
}

Guest::~Guest()
{
    g_destroyCalls.Count();
    ReleaseGuestParts(*this);
}
} // namespace obl::object
