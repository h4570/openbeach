#include "game/ui/sprite_bank.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_base.h"
#include "game/objects/building_events.h"
#include "game/objects/building_events_state.h"
#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/performer_virtuals.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/messages/message_state.h"
#include "game/objects/building_events_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_performers.h"
#include "game/objects/object_building.h"
#include "game/objects/performer_states.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/performer_receive.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>
#include <vector>
#include "game/game/diagnostics.h"

// Life cycle and state methods of the six show performers (performer life cycle and states, docs/architecture/objects.md; logic in src/game/objects/performer_states.h):
// The constructors and destructors, Update, OnEnterState, OnExitState, PlayAnimation and Draw. Every method counts in its
// `objects.<subject>-<method>` counter.
namespace obl::object {
using game::FatalError;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::Rand;

namespace {
using namespace layout;
using messages::Message;
using object::Building;
using ui::SpriteInstance;
using namespace object;
using performer::Kind;

template<class T> struct Traits;
#define OBL_PERFORMER_TRAITS(Name, Subject)                                                                                                \
    template<> struct Traits<Name> {                                                                                                       \
        static constexpr Kind kKind = Kind::Name;                                                                                          \
        static inline test::CallCounter construct{"objects." Subject "-construct"}, destroy{"objects." Subject "-destroy"},                \
                update{"objects." Subject "-update"}, enter{"objects." Subject "-on-enter-state"},                                         \
                receive{"objects." Subject "-receive-message"};                                                                            \
    };
OBL_PERFORMER_TRAITS(Aerobic, "aerobic")
OBL_PERFORMER_TRAITS(Cabaret, "cabaret")
OBL_PERFORMER_TRAITS(Catwalk, "catwalk")
OBL_PERFORMER_TRAITS(Singer, "singer")
OBL_PERFORMER_TRAITS(WeightLifter, "weight-lifter")
OBL_PERFORMER_TRAITS(WetTShirt, "wet-t-shirt")
#undef OBL_PERFORMER_TRAITS

std::vector<object::Base*> ObjectsOfClass(int objectClass)
{
    object::Manager* manager = GetObjectManager();
    if (!manager) return {};
    return object_manager::GetListByClass(manager, objectClass);
}

// ---- Env ---------------------------------------------------------------------------------------------------------------------------------

struct GameEnv {
    std::vector<WeightLifter*> WeightLifters()
    {
        std::vector<WeightLifter*> actors;
        for (auto* member : ObjectsOfClass(16)) actors.push_back(static_cast<WeightLifter*>(member));
        return actors;
    }
    std::vector<Catwalk*> Catwalks()
    {
        std::vector<Catwalk*> actors;
        for (auto* member : ObjectsOfClass(17)) actors.push_back(static_cast<Catwalk*>(member));
        return actors;
    }
    std::vector<WetTShirt*> WetTShirts()
    {
        std::vector<WetTShirt*> actors;
        for (auto* member : ObjectsOfClass(18)) actors.push_back(static_cast<WetTShirt*>(member));
        return actors;
    }
    int Rand() { return game::Rand(); }
    uint8_t HumanReceive(Mover& performer, const Message& message)
    {
        Message copy = message;
        return object::AsPerson(performer).Person::ReceiveMessage(&copy) ? 1 : 0;
    }
    void PostGuests(object::Building& building, const Message& message, float delay)
    {
        Message copy = message;
        building.m_events->PostToGuestsDelayed(&copy, delay);
    }
    SpriteInstance* CreateSprite(const char* name)
    {
        return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name);
    }
    void ReleaseSprite(SpriteInstance* sprite) { (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(sprite); }
    float SetAnim(SpriteInstance* sprite, const char* name, float* position)
    {
        return (sprite)->SetAnimByName(name, GetWorld()->m_gameTime, position);
    }
    bool HumanUpdate(Mover& performer) { return object::AsPerson(performer).Person::Update(); }
    void Fatal(const char* format, const char* name) { FatalError(format, name); }
    object::Building& Building(int id) { return *static_cast<object::Building*>(object_manager::ObjectById(GetObjectManager(), id)); }
    void FootprintPoint(object::Building& building, float offsetX, float offsetY, float (&out)[2])
    {
        int tile[2];
        building.GetTilePosition(tile);
        const grid_math::PathPoint point = GetGrid()->GetFootprintOffsetPoint(tile[0], tile[1], building.m_definition->m_GridWidth,
                building.m_definition->m_GridHeight, offsetX, offsetY,
                static_cast<uint32_t>(static_cast<SpriteInstance*>(building.m_sprite)->m_direction));
        out[0] = point.x;
        out[1] = point.y;
    }
    void Post(const Message& message)
    {
        Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    void PostDelayed(const Message& message, float delay)
    {
        Message copy = message;
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &copy, delay);
    }
    void Teleport(Mover& performer, float x, float y, float z) { performer.Teleport(x, y, z); }
    void SendDelayed(Mover& performer, const Message& message, float delay) { performer.SendDelayed(message, delay); }
    void Draw(SpriteInstance* sprite, float* position) { (sprite)->DrawInWorld(position, GetWorld()->m_gameTime); }
};

// ---- Methods -----------------------------------------------------------------------------------------------------------------------------

template<class T> void RunConstruct(T* performer)
{
    Traits<T>::construct.Count();
    GameEnv env;
    performer::Construct(*performer, env);
}
template<class T> void RunDestroy(T* performer)
{
    Traits<T>::destroy.Count();
    GameEnv env;
    performer::Destruct(*performer, env);
}
template<class T> uint8_t RunUpdate(T* performer)
{
    Traits<T>::update.Count();
    GameEnv env;
    return performer::Update(*performer, Traits<T>::kKind, env);
}
template<class T> uint8_t RunEnter(T* performer, uint32_t state)
{
    Traits<T>::enter.Count();
    GameEnv env;
    return performer::EnterState(*performer, state, env);
}

// The stage shows' exit (aerobic, cabaret, singer) and the catwalk shows' exit (catwalk, wet T-shirt): the answer depends on the current state only.
#define OBL_PERFORMER_EXIT(Name, Subject, Logic)                                                                                           \
    struct Name##Exit {                                                                                                                    \
        static inline test::CallCounter counter{"objects." Subject "-on-exit-state"};                                                      \
        static uint8_t Run(Mover* performer)                                                                                               \
        {                                                                                                                                  \
            counter.Count();                                                                                                               \
            return performer::Logic(performer->m_state);                                                                                   \
        }                                                                                                                                  \
    };
OBL_PERFORMER_EXIT(Aerobic, "aerobic", StageExitState)
OBL_PERFORMER_EXIT(Catwalk, "catwalk", ShowExitState)
#undef OBL_PERFORMER_EXIT

// The weight lifter's exit releases the weights.
struct WeightLifterExit {
    static inline test::CallCounter counter{"objects.weight-lifter-on-exit-state"};
    static uint8_t Run(WeightLifter* performer)
    {
        counter.Count();
        GameEnv env;
        return performer::ExitState(*performer, env);
    }
};

template<class T> struct DrawRun {
    static inline test::CallCounter counter{std::is_same_v<T, Singer> ? "objects.singer-draw" : "objects.weight-lifter-draw"};
    static void Run(T* performer)
    {
        counter.Count();
        GameEnv env;
        performer::Draw(*performer, env);
    }
};

// PlayAnimation of all six.
struct PlayAnimationRun {
    static inline test::CallCounter counter{"objects.performer-play-animation"};
    static float Run(Mover* performer, const char* animation, uint32_t countdown, int32_t endState, uint32_t looped, const char*)
    {
        counter.Count();
        GameEnv env;
        return performer::PlayAnimation(*performer, animation, (countdown & 0xff) != 0, endState, (looped & 0xff) != 0, env);
    }
};

template<class T> struct ReceiveRun {
    static uint8_t Run(T* performer, Message* message)
    {
        Traits<T>::receive.Count();
        GameEnv env;
        return performer::ReceiveMessage(*performer, *message, env);
    }
};

} // namespace

Aerobic::Aerobic(const char* name) : Mover(name)
{
    RunConstruct<object::Aerobic>(this);
}
Cabaret::Cabaret(const char* name) : Mover(name)
{
    RunConstruct<object::Cabaret>(this);
}
Catwalk::Catwalk(const char* name) : Mover(name)
{
    RunConstruct<object::Catwalk>(this);
}
Singer::Singer(const char* name) : Mover(name)
{
    RunConstruct<object::Singer>(this);
}
WeightLifter::WeightLifter(const char* name) : Mover(name)
{
    RunConstruct<object::WeightLifter>(this);
}
WetTShirt::WetTShirt(const char* name) : Mover(name)
{
    RunConstruct<object::WetTShirt>(this);
}
Aerobic::~Aerobic()
{
    RunDestroy<object::Aerobic>(this);
}
bool Aerobic::OnEnterState(uint32_t state)
{
    return RunEnter<object::Aerobic>(this, state);
}
bool Aerobic::Update()
{
    return RunUpdate<object::Aerobic>(this);
}
bool Aerobic::OnExitState()
{
    return AerobicExit::Run(this);
}
bool Cabaret::OnExitState()
{
    return AerobicExit::Run(this);
}
bool Singer::OnExitState()
{
    return AerobicExit::Run(this);
}
bool Aerobic::ReceiveMessage(messages::Message* message)
{
    return ReceiveRun<object::Aerobic>::Run(this, message);
}
float Aerobic::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return PlayAnimationRun::Run(this, name, reverse, exitState, loop, finish);
}
float Cabaret::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return PlayAnimationRun::Run(this, name, reverse, exitState, loop, finish);
}
float Catwalk::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return PlayAnimationRun::Run(this, name, reverse, exitState, loop, finish);
}
float Singer::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return PlayAnimationRun::Run(this, name, reverse, exitState, loop, finish);
}
float WeightLifter::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return PlayAnimationRun::Run(this, name, reverse, exitState, loop, finish);
}
float WetTShirt::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return PlayAnimationRun::Run(this, name, reverse, exitState, loop, finish);
}
Cabaret::~Cabaret()
{
    RunDestroy<object::Cabaret>(this);
}
bool Cabaret::OnEnterState(uint32_t state)
{
    return RunEnter<object::Cabaret>(this, state);
}
bool Cabaret::Update()
{
    return RunUpdate<object::Cabaret>(this);
}
bool Cabaret::ReceiveMessage(messages::Message* message)
{
    return ReceiveRun<object::Cabaret>::Run(this, message);
}
Catwalk::~Catwalk()
{
    RunDestroy<object::Catwalk>(this);
}
bool Catwalk::OnEnterState(uint32_t state)
{
    return RunEnter<object::Catwalk>(this, state);
}
bool Catwalk::Update()
{
    return RunUpdate<object::Catwalk>(this);
}
bool Catwalk::OnExitState()
{
    return CatwalkExit::Run(this);
}
bool WetTShirt::OnExitState()
{
    return CatwalkExit::Run(this);
}
bool Catwalk::ReceiveMessage(messages::Message* message)
{
    return ReceiveRun<object::Catwalk>::Run(this, message);
}
Singer::~Singer()
{
    RunDestroy<object::Singer>(this);
}
bool Singer::OnEnterState(uint32_t state)
{
    return RunEnter<object::Singer>(this, state);
}
bool Singer::Update()
{
    return RunUpdate<object::Singer>(this);
}
bool Singer::ReceiveMessage(messages::Message* message)
{
    return ReceiveRun<object::Singer>::Run(this, message);
}
void Singer::Draw()
{
    DrawRun<object::Singer>::Run(this);
}
WeightLifter::~WeightLifter()
{
    RunDestroy<object::WeightLifter>(this);
}
bool WeightLifter::OnEnterState(uint32_t state)
{
    return RunEnter<object::WeightLifter>(this, state);
}
bool WeightLifter::Update()
{
    return RunUpdate<object::WeightLifter>(this);
}
bool WeightLifter::OnExitState()
{
    return WeightLifterExit::Run(this);
}
bool WeightLifter::ReceiveMessage(messages::Message* message)
{
    return ReceiveRun<object::WeightLifter>::Run(this, message);
}
void WeightLifter::Draw()
{
    DrawRun<object::WeightLifter>::Run(this);
}
WetTShirt::~WetTShirt()
{
    RunDestroy<object::WetTShirt>(this);
}
bool WetTShirt::OnEnterState(uint32_t state)
{
    return RunEnter<object::WetTShirt>(this, state);
}
bool WetTShirt::Update()
{
    return RunUpdate<object::WetTShirt>(this);
}
bool WetTShirt::ReceiveMessage(messages::Message* message)
{
    return ReceiveRun<object::WetTShirt>::Run(this, message);
}
} // namespace obl::object
