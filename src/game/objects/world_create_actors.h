#pragma once

#include "game/objects/object_create_params.h"
#include "game/objects/world_create_staff.h"

#include <cstdint>

// Actor factories: CreateBuilder, CreateShark and CreatePerformer, the factory
// of the six performers (weight lifter, singer, aerobic, cabaret,
// catwalk and wet T-shirt). All of them end like the staff template (state-change message to the new
// object, see StartMessage); what differs is in the middle. The objects and everything they run on the way (including the `rand` draws in the
// performer SetVariant methods, so the order of the calls is part of the contract) stay the game's: the Env supplies the calls.
//   Common with world_create_staff.h: SetType, SetState, Teleport, Post.
//   Entity* CreateObjectByKey(const char* key, ObjectType)  the manager's CreateObject(key, class); the argument is the record itself (it starts with
//                                                               the key) or, for the performers, the class's literal
// Builder (the staff template plus two steps; the builder's own state is written straight after the Teleport):
//   void LookUpStaff(const char* key)                           staff key lookup
//   void SetHomePoint(obj, x, y, z)                             Builder::m_HomePoint
//   void SetHome(obj, int)                                      SetHome
//   void SetBuilderNumber(obj, int)                             the builder's SetBuilderNumber
// Shark:
//   void SetSprite(obj, const char* name)                       creates the named sprite and assigns it to the object
//   void SetBounds(obj, const int (&words)[4])                  setting shark swimming bounds
//   void SetInitParam(obj, int)                                 setting the shark initial parameter
// Performers (the Env knows the class; the class literal is its key):
//   Entity* CreatePerformerObject()                         the manager's CreateObject(k_ObjectKey<Class>, class)
//   void SetVariant(obj, int)                                   selects the performer variant
//   void AttachToBuilding(obj, int)                             assigns the performer to its show building
namespace obl::world_create {

// The builder is a staff member of class 10 that starts in state 0 like most of them.
template<class Env> object::Base* CreateBuilder(Env& env, const object::BuilderCreateParams& record)
{
    env.LookUpStaff(record.m_key);
    object::Base* object = env.CreateObjectByKey(record.m_key, object::ObjectType::Builder);
    env.SetType(object, object::ObjectType::Builder);
    env.SetState(object, 0);
    env.Teleport(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetHomePoint(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetHome(object, record.m_homeId);
    env.SetBuilderNumber(object, record.m_builderNumber);
    messages::Message message = StartMessage(object->m_id);
    env.Post(message);
    return object;
}

// The sprite bank looks the name up without regard to case, so the spelling of this literal does not matter.
inline constexpr const char* kSharkSprite = "Shark";

template<class Env> object::Base* CreateShark(Env& env, const object::SharkCreateParams& record)
{
    object::Base* object = env.CreateObjectByKey(record.m_key, object::ObjectType::Shark);
    env.SetSprite(object, kSharkSprite);
    env.SetBounds(object, record.m_bounds);
    env.SetType(object, object::ObjectType::Shark);
    env.SetState(object, 0);
    env.Teleport(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetInitParam(object, record.m_initParam);
    messages::Message message = StartMessage(object->m_id);
    env.Post(message);
    return object;
}

struct PerformerKind {
    object::ObjectType type;
    bool typeBeforeState; // the order of the two independent setters differs between the factories (no observable effect)
};

constexpr PerformerKind kWeightLifter{object::ObjectType::WeightLifter, false};
constexpr PerformerKind kSinger{object::ObjectType::Singer, false};
constexpr PerformerKind kAerobic{object::ObjectType::Aerobic, false};
constexpr PerformerKind kCabaret{object::ObjectType::Cabaret, true};
constexpr PerformerKind kCatwalk{object::ObjectType::Catwalk, true};
constexpr PerformerKind kWetTShirt{object::ObjectType::WetTShirt, true};

template<class Env> object::Base* CreatePerformer(Env& env, const object::PerformerCreateParams& record, PerformerKind kind)
{
    object::Base* object = env.CreatePerformerObject();
    env.SetVariant(object, record.m_variant);
    if (kind.typeBeforeState) {
        env.SetType(object, kind.type);
        env.SetState(object, 0);
    } else {
        env.SetState(object, 0);
        env.SetType(object, kind.type);
    }
    env.Teleport(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.AttachToBuilding(object, record.m_buildingId);
    messages::Message message = StartMessage(object->m_id);
    env.Post(message);
    return object;
}

} // namespace obl::world_create
