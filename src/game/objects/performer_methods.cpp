#include "game/ui/gui_types.h"
#include "game/ui/sprite_bank.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_base.h"
#include "game/objects/building_events.h"
#include "game/objects/building_events_state.h"
#include "game/map/grid_state.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/performer_methods.h"

#include "game/objects/performer_lifecycle.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/performer_virtuals.h"
#include "game/objects/object_performers.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_building.h"
#include "game/objects/performer_attach.h"
#include "game/messages/message_state.h"
#include "game/objects/performer_state.h"
#include "game/objects/performer_variant.h"
#include "core/call_counter.h"
#include "game/ui/gui_widgets_image_bar.h"

#include <cstdint>

// The small methods of the six show performers (docs/architecture/objects.md): HasState of Catwalk, Singer (shared with Aerobic), WeightLifter and
// WetTShirt (Cabaret keeps the base's), SetVariant (the act or outfit: sprite and palette, `rand` draws) and AttachToBuilding (the show building and the stage
// point). Every method counts in its `objects.<subject>-<method>` counter.
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::Rand;

namespace {
using namespace layout;
using ui::SpriteInstance;
using namespace ui;
using object::Building;
using namespace object;

// ---- HasState ----------------------------------------------------------------------------------------------------------------------

#define OBL_PERFORMER_HAS_STATE(Name, Subject, Struct, Logic)                                                                              \
    struct Name##HasState {                                                                                                                \
        static inline test::CallCounter counter{"objects." Subject "-has-state"};                                                          \
        static bool Run(Struct*, uint32_t state)                                                                                           \
        {                                                                                                                                  \
            counter.Count();                                                                                                               \
            return performer::Logic(state);                                                                                                \
        }                                                                                                                                  \
    };
OBL_PERFORMER_HAS_STATE(Catwalk, "catwalk", Catwalk, ShowHasState)
OBL_PERFORMER_HAS_STATE(Singer, "singer", Singer, SingerHasState)
OBL_PERFORMER_HAS_STATE(WeightLifter, "weight-lifter", WeightLifter, ShowHasState)
OBL_PERFORMER_HAS_STATE(WetTShirt, "wet-t-shirt", WetTShirt, ShowHasState)
#undef OBL_PERFORMER_HAS_STATE

// ---- SetVariant --------------------------------------------------------------------------------------------------------------------

// The game's sprite bank and rand.
struct VariantEnv {
    SpriteInstance* CreateSprite(const char* name)
    {
        return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name);
    }
    int Rand() { return game::Rand(); }
};

template<class T> struct VariantCounters;
#define OBL_PERFORMER_VARIANT_COUNTERS(Subject, Struct)                                                                                    \
    template<> struct VariantCounters<Struct> {                                                                                            \
        static inline test::CallCounter counter{"objects." Subject "-set-variant"};                                                        \
    };
OBL_PERFORMER_VARIANT_COUNTERS("aerobic", Aerobic)
OBL_PERFORMER_VARIANT_COUNTERS("cabaret", Cabaret)
OBL_PERFORMER_VARIANT_COUNTERS("catwalk", Catwalk)
OBL_PERFORMER_VARIANT_COUNTERS("singer", Singer)
OBL_PERFORMER_VARIANT_COUNTERS("weight-lifter", WeightLifter)
OBL_PERFORMER_VARIANT_COUNTERS("wet-t-shirt", WetTShirt)
#undef OBL_PERFORMER_VARIANT_COUNTERS

template<class T> void RunVariant(T* performer, int variant)
{
    VariantEnv env;
    performer::SetVariant(*performer, variant, env);
    VariantCounters<T>::counter.Count();
}

// ---- AttachToBuilding --------------------------------------------------------------------------------------------------------------

object::Building& BuildingOf(int id)
{
    return *static_cast<object::Building*>(object_manager::ObjectById(GetObjectManager(), id));
}
void Footprint(object::Building& building, float offsetX, float offsetY, float (&out)[2])
{
    int tile[2];
    building.GetTilePosition(tile);
    auto* sprite = static_cast<SpriteInstance*>(building.m_sprite);
    const grid_math::PathPoint point = GetGrid()->GetFootprintOffsetPoint(tile[0], tile[1], building.m_definition->m_GridWidth,
            building.m_definition->m_GridHeight, offsetX, offsetY, static_cast<uint32_t>(sprite->m_direction));
    out[0] = point.x;
    out[1] = point.y;
}

struct AttachEnv {
    object::Building& Building(int id) { return BuildingOf(id); }
    void AddActor(object::Building& building, int id) { building.m_events->AddActor(id); }
    void FootprintPoint(object::Building& building, float offsetX, float offsetY, float (&out)[2])
    {
        Footprint(building, offsetX, offsetY, out);
    }
};

template<class T> struct AttachCounters;
#define OBL_PERFORMER_ATTACH_COUNTERS(Subject, Struct)                                                                                     \
    template<> struct AttachCounters<Struct> {                                                                                             \
        static inline test::CallCounter counter{"objects." Subject "-attach-to-building"};                                                 \
    };
OBL_PERFORMER_ATTACH_COUNTERS("aerobic", Aerobic)
OBL_PERFORMER_ATTACH_COUNTERS("cabaret", Cabaret)
OBL_PERFORMER_ATTACH_COUNTERS("catwalk", Catwalk)
OBL_PERFORMER_ATTACH_COUNTERS("singer", Singer)
OBL_PERFORMER_ATTACH_COUNTERS("weight-lifter", WeightLifter)
OBL_PERFORMER_ATTACH_COUNTERS("wet-t-shirt", WetTShirt)
#undef OBL_PERFORMER_ATTACH_COUNTERS

template<class T> void RunAttach(T* performer, int buildingId)
{
    AttachEnv env;
    performer::AttachToBuilding(*performer, buildingId, env);
    AttachCounters<T>::counter.Count();
}

// ---- GetValue / SetValue (GetValue and SetValue) -----------------------------------------------------------------------------------------------

// Three pass-throughs the game never calls in a normal run: RunGetValue asks the human GetValue and returns `out`,
// RunCabaretSetValue hands its four arguments to the human SetValue, RunSetValueNoOp does nothing. The base class is
// called through its entry.
test::CallCounter g_getValue("objects.performer-get-value"), g_cabaretSetValue("objects.cabaret-set-value"),
        g_setValueNoOp("objects.performer-set-value-no-op");

void* RunGetValue(void* performer, void* out, int key, intptr_t data)
{
    g_getValue.Count();
    object::AsPerson(*static_cast<object::Mover*>(performer)).Person::GetValue(static_cast<script::ScriptValue*>(out), key, data);
    return out;
}
void RunCabaretSetValue(void* performer, int key, intptr_t data, int type, int bits)
{
    g_cabaretSetValue.Count();
    object::AsPerson(*static_cast<object::Mover*>(performer)).Person::SetValue(key, data, type, bits);
}
void RunSetValueNoOp(void*, int, int, int, int)
{
    g_setValueNoOp.Count();
}
template<class T> void SetVariantEntry(object::Base* performer, int variant)
{
    RunVariant(static_cast<T*>(performer), variant);
}
template<class T> void AttachEntry(object::Base* performer, int buildingId)
{
    RunAttach(static_cast<T*>(performer), buildingId);
}
} // namespace

const PerformerMethods kAerobicMethods{&SetVariantEntry<Aerobic>, &AttachEntry<Aerobic>};
const PerformerMethods kCabaretMethods{&SetVariantEntry<Cabaret>, &AttachEntry<Cabaret>};
const PerformerMethods kCatwalkMethods{&SetVariantEntry<Catwalk>, &AttachEntry<Catwalk>};
const PerformerMethods kSingerMethods{&SetVariantEntry<Singer>, &AttachEntry<Singer>};
const PerformerMethods kWeightLifterMethods{&SetVariantEntry<WeightLifter>, &AttachEntry<WeightLifter>};
const PerformerMethods kWetTShirtMethods{&SetVariantEntry<WetTShirt>, &AttachEntry<WetTShirt>};
} // namespace obl::object

namespace obl::object {
// Aerobic shares its HasState check with another performer class; it reads only the state number.
bool Aerobic::HasState(uint32_t state)
{
    return object::SingerHasState::Run(reinterpret_cast<Singer*>(this), state);
}
bool Singer::HasState(uint32_t state)
{
    return object::SingerHasState::Run(this, state);
}
script::ScriptValue* Aerobic::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return static_cast<script::ScriptValue*>(object::RunGetValue(this, out, key, data));
}
script::ScriptValue* Cabaret::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return static_cast<script::ScriptValue*>(object::RunGetValue(this, out, key, data));
}
script::ScriptValue* Catwalk::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return static_cast<script::ScriptValue*>(object::RunGetValue(this, out, key, data));
}
script::ScriptValue* Singer::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return static_cast<script::ScriptValue*>(object::RunGetValue(this, out, key, data));
}
script::ScriptValue* WeightLifter::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return static_cast<script::ScriptValue*>(object::RunGetValue(this, out, key, data));
}
script::ScriptValue* WetTShirt::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return static_cast<script::ScriptValue*>(object::RunGetValue(this, out, key, data));
}
void Aerobic::SetValue(int key, intptr_t data, int type, int bits)
{
    object::RunSetValueNoOp(this, key, data, type, bits);
}
void Catwalk::SetValue(int key, intptr_t data, int type, int bits)
{
    object::RunSetValueNoOp(this, key, data, type, bits);
}
void Singer::SetValue(int key, intptr_t data, int type, int bits)
{
    object::RunSetValueNoOp(this, key, data, type, bits);
}
void WeightLifter::SetValue(int key, intptr_t data, int type, int bits)
{
    object::RunSetValueNoOp(this, key, data, type, bits);
}
void WetTShirt::SetValue(int key, intptr_t data, int type, int bits)
{
    object::RunSetValueNoOp(this, key, data, type, bits);
}
void Cabaret::SetValue(int key, intptr_t data, int type, int bits)
{
    object::RunCabaretSetValue(this, key, data, type, bits);
}
bool Catwalk::HasState(uint32_t state)
{
    return object::CatwalkHasState::Run(this, state);
}
bool WeightLifter::HasState(uint32_t state)
{
    return object::WeightLifterHasState::Run(this, state);
}
bool WetTShirt::HasState(uint32_t state)
{
    return object::WetTShirtHasState::Run(this, state);
}
void Cabaret::CallFunction(int) {}
} // namespace obl::object
