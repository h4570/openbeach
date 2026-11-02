#pragma once

#include "game/objects/entity_messages.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_base.h"
#include "game/objects/mover_route.h"
#include "game/objects/mover_target.h"
#include "game/map/path_data.h"
#include "game/ui/sprite_animation.h"

#include <cstdint>
#include <cstring>
#include <optional>

// The moving object's message handler (docs/architecture/objects.md), one message family at a time. It dispatches the types
// 5..29; every type outside that range ends in the base object's message handler. `Handles` says which types this file implements; the game
// handler (src/game/objects/mover_receive.cpp) passes every other message to the base handler. As in the other groups,
// everything outside the object goes through an `Env`:
//   Entity* Object(int32_t id)                                                    the object manager's lookup (callers expect the object to exist)
//   void FaceTowards(const Entity& who, const float from[3], const float to[3])   turns who's sprite to face the target
//   uint32_t BuildingWalkPointCount(int32_t id)                                       building->m_iNumPoints of the object with that id, asked for a building only
//   float GameTime                                                                  World::m_gameTime
//   void ServePosition(const Entity& building, float out[3])                      GetServePosition (building_api.h)
//   void QueueTailPosition(const Entity& building, float out[3])                  GetQueueTailPosition (building_api.h)
//   void MapTile(const float (&world)[3], uint32_t out[2])                            the map view world-to-tile conversion
//   PathCursor* Land(sx, sy, tx, ty, bool* found, uint32_t option)                    land route creation on g_Path
//   void ReleaseCursor(PathCursor*)                                                   releasing the route cursor
//   void SendDelayed(Mover&, const Message&, float)  /  void PostDelayed(const Message&, float)   as in mover_target.h
//   bool TileAllowed(uint32_t x, uint32_t y, int routeType)                           tests whether a tile supports the route type (1 = land, 4 = land or blocked)
//   bool Prepare(uint32_t* start, uint32_t* stop, uint32_t mask)  /  void Clear(...)  as in mover_route.h
//   PathCursor* Scenery(sx, sy, tx, ty, bool* found, uint32_t option)                 land-or-blocked route creation on g_Path
//   void ApproachPosition(const Entity& scenery, float out[3])                    the scenery approach point
//   void TilePosition(const Entity& object, uint32_t out[2])                      GetTilePosition (entity_accessors.h)
//   int Rand                                                                        the C runtime's rand
//   uint32_t TileFlags(uint32_t x, uint32_t y)                                        the flags of the (wrapped) tile
//   PathCursor* Sea(sx, sy, tx, ty, bool* found)                                      sea route creation on g_Path
//   void TileOrigin(uint32_t x, uint32_t y, float out[2])                             the tile origin (the tile's world point, kind 0)
//   PathCursor* Pavement(sx, sy, tx, ty, bool* found)                                 pavement route creation on g_Path (through BuildRoute)
//   bool Adjacent(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty)                 the wrapped tile adjacency test (g_Grid): pure neighbour test on the wrapped tiles
//   void Post(const Message&)                                                         posts to the message handler
// The default path in entity_messages.h uses HasState, OnExitState, OnEnterState and FatalError.
namespace obl::moving {

inline constexpr uint32_t kSeaMask = 0x60000000u; // the two sea bits of a tile's flags (MT_SWIM2SEAPOINT)

// Ported families: 21/24/29 (stop, carry on, stop and face the sender), 28 (walk inside a building), 26 (follow an object), 15/16 (direct move),
// 13/14 (move through scenery, to a point / to an object), 10..12 (the sea walks: land route with option 1, swim route, vehicle route) and
// 5/6, 8/9 (the land and pavement walks, to a point / to an object).
inline bool Handles(int type)
{
    switch (type) {
    case messages::MT_MOVE2POINT:
    case messages::MT_MOVE2OBJECT:
    case messages::MT_PAVEMENT2POINT:
    case messages::MT_PAVEMENT2OBJECT:
    case messages::MT_MOVE2SEAPOINT:
    case messages::MT_SWIM2SEAPOINT:
    case messages::MT_SEAVEHICLE_MOVE2POINT:
    case messages::MT_PATH_THROUGH_SCENERY:
    case messages::MT_PATH_THROUGH_SCENERY2OBJECT:
    case messages::MT_MOVESTOP:
    case messages::MT_MOVE_CARRY_ON:
    case messages::MT_STOP_AND_FACE_ME:
    case messages::MT_MOVE_INSIDE_BUILDING:
    case messages::MT_FOLLOWOBJECT:
    case messages::MT_MOVE_DIRECT_2POINT:
    case messages::MT_MOVE_DIRECT_2OBJECT: return true;
    default: return false;
    }
}

namespace detail {
// Positions are copied as raw words, never through a float load, so every bit pattern survives unchanged.
inline void CopyPosition(float (&out)[3], const float (&position)[3])
{
    std::memcpy(out, position, sizeof(out));
}

// The tile a route starts on: the object's own, or the serve position of the building it is inside (the scenery walks name it by m_BuildingLeavingID,
// the sea walks by m_DestObjectID; a building that is gone leaves the object's own position in place).
template<class Env> void StartTile(const object::Mover& object, int32_t buildingId, Env& env, uint32_t (&tile)[2])
{
    float spot[3];
    CopyPosition(spot, object.m_WorldXYZ);
    if (object.m_insideBuilding)
        if (const object::Base* building = env.Object(buildingId)) env.ServePosition(*building, spot);
    env.MapTile(spot, tile);
}

// The route of MT_SWIM2SEAPOINT (built here like BuildRoute): the tile test and the endpoint helper use the sea mask, the tile cleanup
// the land mask. Both tiles carrying a sea flag and differing = routed straight away, the same tile = nothing; every other pair goes through the
// helper, is routed with the corrected ends when that worked, and is cleaned up afterwards - also when the helper failed.
template<class Env> void BuildSwimRoute(object::Mover& object, uint32_t (&start)[2], uint32_t (&stop)[2], bool* found, Env& env)
{
    const auto sameTile = [&] { return start[0] == stop[0] && start[1] == stop[1]; };
    const auto route = [&] { object.m_pathCursor = env.Sea(start[0], start[1], stop[0], stop[1], found); };
    if ((env.TileFlags(start[0], start[1]) & kSeaMask) && (env.TileFlags(stop[0], stop[1]) & kSeaMask)) {
        if (!sameTile()) route();
        return;
    }
    if (sameTile()) return;
    if (env.Prepare(start, stop, kSeaMask)) route();
    env.Clear(start, stop, kLandMask);
}

// The route of the walks that test their end tiles with the route tile eligibility test (MT_PATH_THROUGH_SCENERY[2OBJECT] with route type 4 and the "land or
// blocked" route, MT_MOVE2POINT with type 1 and the land route; each builds it in place). The tile test is asked of the start tile
// first and, only when that passes, of the stop tile. Two passing tiles that differ are routed straight away; the same tile needs nothing.
// Every other pair goes through the endpoint helper (land mask), is routed with the corrected ends when that worked, and the temporary tile bits
// are removed again after it - also when the helper failed. The cursor replaces m_pathCursor without releasing the old one.
template<class Env, class Route> void BuildTestedRoute(uint32_t (&start)[2], uint32_t (&stop)[2], int routeType, Env& env, Route&& route)
{
    const auto sameTile = [&] { return start[0] == stop[0] && start[1] == stop[1]; };
    if (env.TileAllowed(start[0], start[1], routeType) && env.TileAllowed(stop[0], stop[1], routeType)) {
        if (!sameTile()) route();
        return;
    }
    if (sameTile()) return;
    if (env.Prepare(start, stop, kLandMask)) route();
    env.Clear(start, stop, kLandMask);
}

template<class Env> void BuildSceneryRoute(object::Mover& object, uint32_t (&start)[2], uint32_t (&stop)[2], bool* found, Env& env)
{
    BuildTestedRoute(start, stop, 4, env, [&] { object.m_pathCursor = env.Scenery(start[0], start[1], stop[0], stop[1], found, 0); });
}

template<class Env> void BuildLandWalkRoute(object::Mover& object, uint32_t (&start)[2], uint32_t (&stop)[2], bool* found, Env& env)
{
    BuildTestedRoute(start, stop, 1, env, [&] { object.m_pathCursor = env.Land(start[0], start[1], stop[0], stop[1], found, 0); });
}

// MT_CANT_MOVE / MT_PATHBLOCKED sent to the object itself: sender and receiver are its id, extra its state, the other words zero.
template<class Env> void PostBlocked(object::Mover& object, int type, Env& env)
{
    messages::Message message = MakeSelfMessage(object, type);
    message.extra = object.m_state;
    env.Post(message);
}

// The first half of the object walks (MT_PATH_THROUGH_SCENERY2OBJECT, MT_MOVE2OBJECT, MT_PAVEMENT2OBJECT): seek behaviour, the move position is
// the current one, an inside-building object remembers where it leaves from, the old route is released, and the object named in data becomes
// the destination object with its spot as the destination: a building's serve position (flags != 0) or queue tail (flags == 0), a scenery's
// approach position, the position of any other object. The start tile is the object's own (or the serve position of the building it leaves), the
// stop tile that of the destination - for "any other" object read off the object (GetTilePosition). Returns false when the id names
// no object (-1 included): the destination then stays at the origin and the caller answers true without routing.
template<class Env>
bool BeginObjectWalk(object::Mover& object, const messages::Message& message, Env& env, uint32_t (&start)[2], uint32_t (&stop)[2])
{
    object.m_behaviourState = 1;
    SetMovePosition(object, Bits(object.m_WorldXYZ[0]), Bits(object.m_WorldXYZ[1]));
    if (object.m_insideBuilding) object.m_BuildingLeavingID = object.m_DestObjectID;
    if (object.m_pathCursor) {
        env.ReleaseCursor(object.m_pathCursor);
        object.m_pathCursor = nullptr;
    }
    object.m_DestObjectID = message.data;
    object.m_DestXYZ[0] = object.m_DestXYZ[1] = object.m_DestXYZ[2] = 0.0f;
    object.m_moving = 1;
    StartTile(object, object.m_BuildingLeavingID, env, start);
    const object::Base* target = object.m_DestObjectID == -1 ? nullptr : env.Object(object.m_DestObjectID);
    if (!target) return false;
    if (target->m_type == object::ObjectType::Building || target->m_type == object::ObjectType::Scenery) {
        float spot[3];
        if (target->m_type == object::ObjectType::Scenery)
            env.ApproachPosition(*target, spot);
        else if (message.flags != 0)
            env.ServePosition(*target, spot);
        else
            env.QueueTailPosition(*target, spot);
        CopyPosition(object.m_DestXYZ, spot);
        env.MapTile(object.m_DestXYZ, stop);
    } else {
        env.TilePosition(*target, stop);
        CopyPosition(object.m_DestXYZ, target->m_WorldXYZ);
    }
    return true;
}

// After a pavement walk's BuildRoute built nothing: the tile flags are read again (after the helpers cleaned up) and, when neither end is a
// pavement tile or the tiles are neither neighbours nor the same, the object stops and tells itself MT_PATHBLOCKED.
template<class Env> void ReportPavementBlocked(object::Mover& object, const uint32_t (&start)[2], const uint32_t (&stop)[2], Env& env)
{
    const bool startOff = (env.TileFlags(start[0], start[1]) & kPavementMask) == 0;
    const bool stopOff = (env.TileFlags(stop[0], stop[1]) & kPavementMask) == 0;
    const bool reachable = env.Adjacent(start[0], start[1], stop[0], stop[1]) || (start[0] == stop[0] && start[1] == stop[1]);
    if ((startOff && stopOff) || !reachable) {
        object.m_moving = 0;
        PostBlocked(object, messages::MT_PATHBLOCKED, env);
    }
}

// What every successful move command ends with: the object waits for its MT_MOVEIMHERE from now on.
template<class Env> void WaitForArrival(object::Mover& object, Env& env)
{
    object.m_waitingForMoveImHere = 1;
    object.m_messageReceived = env.GameTime();
}
} // namespace detail

// MT_MOVESTOP / MT_MOVE_CARRY_ON: switch the object's walking on or off. A stop also drops the follow flag and returns to seek (behaviour 1).
// MT_STOP_AND_FACE_ME: the sender asks the receiver to stop and look at it. The follow target is forgotten, the object stops, and both sprites turn:
// first the sender's, which gets (receiver position, sender position), then the receiver's, which gets (sender position, receiver position).
// A sender that no longer exists is tolerated: the object only stops.
// MT_MOVE_INSIDE_BUILDING: walk the points of the building named in data (flags != 0 walks them backwards, from the last). A building without
// walk points (or one that is gone) does not start a walk: the object stops dead and tells itself it arrived
// (MT_MOVEIMHERE after 0.01 s, the same message as CompleteIfAtDestination). Starting a walk also resets the "waiting for MT_MOVEIMHERE" timestamp.
// An id that names no building counts as one without points: a demolished building's id reused by scenery must not read a point
// count off the scenery object (that gave a different walk in every run; the chaos bot's scenario 1 seed 2).
// MT_FOLLOWOBJECT: follow the object named in data at the distance flags. Behaviour 4, the "reached distance" flag is cleared, the move position
// becomes the current one, and an object inside a building steps out first (the leaving id is the building, the position the building's serve
// position, the inside flag drops; a vanished building leaves the position as it is). The sea flag drops and an open route
// is released. The target's position becomes m_DestXYZ (raw words), the object starts moving and gets a land route (option 1, `routeFound` starts
// true) between the tiles of its own position and of the target's, stored whatever the result (null when no route). The follow id, distance and
// flag are set and the "waiting for MT_MOVEIMHERE" timestamp restarts. A target that is gone reads as the origin.
// The route never starts from the serve position: the inside flag is already cleared by then.
// MT_MOVE_DIRECT_2POINT: like a move command (SetDestinationFromMessage), but it answers true at once when already there; otherwise it starts the
// walk and waits for MT_MOVEIMHERE with the timestamp reset. Unlike MT_MOVE_DIRECT_2OBJECT it never routes (both are "direct", straight-line seeks).
// MT_MOVE_DIRECT_2OBJECT: the destination test runs on the *old* destination first. Then the object remembers data as its destination object
// and the destination becomes that object's spot: a building's serve position when flags != 0, its queue tail position when flags == 0,
// the plain position of any other object. The walk starts (m_moving = 1) but the "waiting for MT_MOVEIMHERE" state is left alone. The id -1
// counts as a missing object, and a missing object leaves the destination at the origin.
// MT_PATH_THROUGH_SCENERY: like MT_MOVE_DIRECT_2POINT it starts with SetDestinationFromMessage and CompleteIfAtDestination, but then the object routes:
// from the tile it stands on (or, inside a building, the building's serve position) to the destination's tile (the map view world-to-tile conversion of m_DestXYZ),
// see detail::BuildSceneryRoute. `routeFound` starts true; the old route, if any, is not released here (SetDestinationFromMessage already did).
// MT_PATH_THROUGH_SCENERY2OBJECT: the same toward an object. It begins like SetDestinationFromMessage (seek, move position, leaving id, route
// released) but takes the destination from the object named in data: a building's serve position (flags != 0) or queue tail (flags == 0),
// a scenery's approach position, and for any other object its own position, with the stop tile read from the object itself
// (GetTilePosition) rather than from the position. `routeFound` starts false. The object keeps its inside-building flag and ends by
// drawing its position offset, rand % 2 + 1 (m_posOffset), even when no route was needed. An id that names no object (-1 included)
// leaves the destination at the origin and routes nothing.
// Both end waiting for MT_MOVEIMHERE with the timestamp reset, and answer true.
// MT_MOVE2SEAPOINT (10), MT_SWIM2SEAPOINT (11), MT_SEAVEHICLE_MOVE2POINT (12): the sea walks. All start with SetDestinationFromMessage and
// CompleteIfAtDestination (arrived = true) and start moving. 10 and 11 route from the tile of the object, or - inside a building - of the serve position
// of the building named by m_DestObjectID (not the leaving id), to the tile of m_DestXYZ. 10 takes behaviour 2 and a *land* route with option 1
// (`routeFound` starts true), whatever its name says; 11 keeps the behaviour and routes by the sea flags of the tiles (detail::BuildSwimRoute).
// 12 routes by sea from the tile of the object's own position and, with no route, drops the vehicle-path flag and answers true. Otherwise it points
// the object at the first tile of the route (the tile origin against m_vPosition, normalised as in FaceTowards, one of eight directions in m_lastDir)
// and sets the vehicle-path flag, behaviour 1, no direction control, position offset 0 and turn type 1. 10, 11 and the routed 12 end waiting for
// MT_MOVEIMHERE (timestamp reset) and fall into the base object's message handler, which does nothing for these types and answers false.
// MT_MOVE2POINT (5): like the scenery walk to a point (SetDestinationFromMessage, CompleteIfAtDestination -> true, start tile of the object or of the
// serve position of m_BuildingLeavingID, stop tile of m_DestXYZ, moving) but the land route is tested with the route tile eligibility test type 1 and built with
// Land route creation option 0 (detail::BuildLandWalkRoute; `routeFound` starts true). Then it draws rand % 2 + 1 into m_posOffset, posts
// MT_CANT_MOVE to itself when there is no route (before the wait state is set) and waits for MT_MOVEIMHERE.
// MT_MOVE2OBJECT (6): detail::BeginObjectWalk, then the land route of BuildRoute with the land mask (flags & mask on both tiles, `routeFound`
// starts false); the offset is drawn and the wait state set first, MT_CANT_MOVE is posted last when there is no route.
// MT_PAVEMENT2POINT (8) / MT_PAVEMENT2OBJECT (9): the same beginnings (`routeFound` true for 8, false for 9; 9 draws the offset) and BuildRoute
// with the pavement mask. Without a route the flags of the end tiles are read again (BuildRoute may have rewritten the ends; its helpers have cleaned
// up by then) and detail::ReportPavementBlocked decides whether the object stops and posts MT_PATHBLOCKED (both ends off the pavement, or not neighbours). All end waiting for MT_MOVEIMHERE
// and answer true.
// Returns nullopt for a type this file does not implement.
template<class Env> std::optional<bool> ReceiveMessage(object::Mover& object, const messages::Message& message, Env& env)
{
    switch (message.type) {
    case messages::MT_MOVESTOP:
        object.m_moving = 0;
        object.m_followObject = 0;
        object.m_behaviourState = 1;
        return true;
    case messages::MT_MOVE_CARRY_ON: object.m_moving = 1; return true;
    case messages::MT_STOP_AND_FACE_ME: {
        if (object.m_followId != -1) object.m_followId = -1;
        object::Base* sender = env.Object(message.sender);
        object.m_moving = 0;
        if (!sender) return true;
        float senderPosition[3], objectPosition[3];
        detail::CopyPosition(senderPosition, sender->m_WorldXYZ);
        detail::CopyPosition(objectPosition, object.m_WorldXYZ);
        env.FaceTowards(*sender, objectPosition, senderPosition);
        detail::CopyPosition(objectPosition, object.m_WorldXYZ);
        detail::CopyPosition(senderPosition, sender->m_WorldXYZ);
        env.FaceTowards(object, senderPosition, objectPosition);
        return true;
    }
    case messages::MT_MOVE_INSIDE_BUILDING: {
        object.m_buildingId = message.data;
        const object::Base* building = env.Object(message.data);
        const uint32_t points = building && building->m_type == object::ObjectType::Building ? env.BuildingWalkPointCount(message.data) : 0;
        object.m_numWalkPts = points;
        if (points != 0) {
            object.m_moving = 1;
            object.m_walkInBuilding = 1;
            const bool backwards = static_cast<uint32_t>(message.flags) > 0;
            object.m_buildingTraversal = backwards;
            object.m_currWalkPt = backwards ? points - 1 : 0;
            object.m_waitingForMoveImHere = 1;
            object.m_messageReceived = env.GameTime();
            return true;
        }
        object.m_vVelocity[0] = object.m_vVelocity[1] = 0.0f;
        object.m_vSteerForce[0] = object.m_vSteerForce[1] = 0.0f;
        SendArrivalMessage(object, env);
        return true;
    }
    case messages::MT_FOLLOWOBJECT: {
        object.m_reachedFollowDistance = 0;
        object.m_behaviourState = 4;
        SetMovePosition(object, Bits(object.m_WorldXYZ[0]), Bits(object.m_WorldXYZ[1]));
        if (object.m_insideBuilding) {
            object.m_BuildingLeavingID = object.m_DestObjectID;
            float serve[3];
            if (const object::Base* building = env.Object(object.m_DestObjectID)) {
                env.ServePosition(*building, serve);
                detail::CopyPosition(object.m_WorldXYZ, serve);
            }
            object.m_insideBuilding = 0;
        }
        object.m_enteredTheSea = 0;
        if (object.m_pathCursor) {
            env.ReleaseCursor(object.m_pathCursor);
            object.m_pathCursor = nullptr;
        }
        const int32_t targetId = message.data;
        float target[3] = {0.0f, 0.0f, 0.0f};
        if (const object::Base* followed = env.Object(targetId)) detail::CopyPosition(target, followed->m_WorldXYZ);
        detail::CopyPosition(object.m_DestXYZ, target);
        object.m_moving = 1;
        float start[3];
        detail::CopyPosition(start, object.m_WorldXYZ);
        uint32_t from[2], to[2];
        env.MapTile(start, from);
        env.MapTile(object.m_DestXYZ, to);
        bool found = true;
        object.m_pathCursor = env.Land(from[0], from[1], to[0], to[1], &found, 1);
        object.m_followId = targetId;
        object.m_followDistance = message.flags;
        object.m_followObject = 1;
        object.m_waitingForMoveImHere = 1;
        object.m_messageReceived = env.GameTime();
        return true;
    }
    case messages::MT_MOVE_DIRECT_2POINT:
        SetDestinationFromMessage(object, message, env);
        if (CompleteIfAtDestination(object, env)) return true;
        object.m_moving = 1;
        object.m_waitingForMoveImHere = 1;
        object.m_messageReceived = env.GameTime();
        return true;
    case messages::MT_MOVE_DIRECT_2OBJECT: {
        if (CompleteIfAtDestination(object, env)) return true;
        object.m_moving = 1;
        object.m_DestObjectID = message.data;
        object.m_DestXYZ[0] = object.m_DestXYZ[1] = object.m_DestXYZ[2] = 0.0f;
        const object::Base* target = object.m_DestObjectID == -1 ? nullptr : env.Object(object.m_DestObjectID);
        if (!target) return true;
        float spot[3];
        if (target->m_type == object::ObjectType::Building) {
            if (message.flags != 0)
                env.ServePosition(*target, spot);
            else
                env.QueueTailPosition(*target, spot);
        } else {
            detail::CopyPosition(spot, target->m_WorldXYZ);
        }
        detail::CopyPosition(object.m_DestXYZ, spot);
        return true;
    }
    case messages::MT_MOVE2SEAPOINT: {
        SetDestinationFromMessage(object, message, env);
        if (CompleteIfAtDestination(object, env)) return true;
        object.m_behaviourState = 2;
        object.m_moving = 1;
        uint32_t start[2], stop[2];
        detail::StartTile(object, object.m_DestObjectID, env, start);
        env.MapTile(object.m_DestXYZ, stop);
        bool found = true;
        object.m_pathCursor = env.Land(start[0], start[1], stop[0], stop[1], &found, 1);
        detail::WaitForArrival(object, env);
        return false;
    }
    case messages::MT_SWIM2SEAPOINT: {
        SetDestinationFromMessage(object, message, env);
        if (CompleteIfAtDestination(object, env)) return true;
        object.m_moving = 1;
        uint32_t start[2], stop[2];
        detail::StartTile(object, object.m_DestObjectID, env, start);
        env.MapTile(object.m_DestXYZ, stop);
        bool found = true;
        detail::BuildSwimRoute(object, start, stop, &found, env);
        detail::WaitForArrival(object, env);
        return false;
    }
    case messages::MT_SEAVEHICLE_MOVE2POINT: {
        SetDestinationFromMessage(object, message, env);
        if (CompleteIfAtDestination(object, env)) return true;
        object.m_moving = 1;
        float own[3];
        detail::CopyPosition(own, object.m_WorldXYZ);
        uint32_t start[2], stop[2];
        env.MapTile(own, start);
        env.MapTile(object.m_DestXYZ, stop);
        bool found = true;
        path::RouteCursor* cursor = env.Sea(start[0], start[1], stop[0], stop[1], &found);
        object.m_pathCursor = cursor;
        if (!cursor) {
            object.m_vehiclePath = 0;
            return true;
        }
        const map::TilePoint& step = cursor->m_route->m_steps[cursor->m_currentStep];
        float origin[2];
        env.TileOrigin(step.x, step.y, origin);
        const sprite_animation::Vector2 heading =
                sprite_animation::FaceVector(origin[0], origin[1], object.m_vPosition[0], object.m_vPosition[1]);
        object.m_lastDir = static_cast<uint32_t>(path::DirectionIndex(heading.x, heading.y, 8));
        object.m_vehiclePath = 1;
        object.m_controlDirection = 0;
        object.m_behaviourState = 1;
        object.m_posOffset = 0;
        object.m_turnType = 1;
        detail::WaitForArrival(object, env);
        return false;
    }
    case messages::MT_PATH_THROUGH_SCENERY: {
        SetDestinationFromMessage(object, message, env);
        if (CompleteIfAtDestination(object, env)) return true;
        uint32_t start[2], stop[2];
        detail::StartTile(object, object.m_BuildingLeavingID, env, start);
        env.MapTile(object.m_DestXYZ, stop);
        object.m_moving = 1;
        bool found = true;
        detail::BuildSceneryRoute(object, start, stop, &found, env);
        detail::WaitForArrival(object, env);
        return true;
    }
    case messages::MT_PATH_THROUGH_SCENERY2OBJECT: {
        uint32_t start[2], stop[2];
        if (!detail::BeginObjectWalk(object, message, env, start, stop)) return true;
        bool found = false;
        detail::BuildSceneryRoute(object, start, stop, &found, env);
        object.m_posOffset = static_cast<uint32_t>(env.Rand() % 2 + 1);
        detail::WaitForArrival(object, env);
        return true;
    }
    case messages::MT_MOVE2POINT: {
        SetDestinationFromMessage(object, message, env);
        if (CompleteIfAtDestination(object, env)) return true;
        uint32_t start[2], stop[2];
        detail::StartTile(object, object.m_BuildingLeavingID, env, start);
        env.MapTile(object.m_DestXYZ, stop);
        object.m_moving = 1;
        bool found = true;
        detail::BuildLandWalkRoute(object, start, stop, &found, env);
        object.m_posOffset = static_cast<uint32_t>(env.Rand() % 2 + 1);
        if (!object.m_pathCursor) detail::PostBlocked(object, messages::MT_CANT_MOVE, env);
        detail::WaitForArrival(object, env);
        return true;
    }
    case messages::MT_MOVE2OBJECT: {
        uint32_t start[2], stop[2];
        if (!detail::BeginObjectWalk(object, message, env, start, stop)) return true;
        bool found = false;
        BuildRoute(object, start, stop, &found, kLandMask, env);
        object.m_posOffset = static_cast<uint32_t>(env.Rand() % 2 + 1);
        detail::WaitForArrival(object, env);
        if (!object.m_pathCursor) detail::PostBlocked(object, messages::MT_CANT_MOVE, env);
        return true;
    }
    case messages::MT_PAVEMENT2POINT: {
        SetDestinationFromMessage(object, message, env);
        if (CompleteIfAtDestination(object, env)) return true;
        uint32_t start[2], stop[2];
        detail::StartTile(object, object.m_BuildingLeavingID, env, start);
        env.MapTile(object.m_DestXYZ, stop);
        object.m_moving = 1;
        bool found = true;
        BuildRoute(object, start, stop, &found, kPavementMask, env);
        if (!object.m_pathCursor) detail::ReportPavementBlocked(object, start, stop, env);
        detail::WaitForArrival(object, env);
        return true;
    }
    case messages::MT_PAVEMENT2OBJECT: {
        uint32_t start[2], stop[2];
        if (!detail::BeginObjectWalk(object, message, env, start, stop)) return true;
        bool found = false;
        BuildRoute(object, start, stop, &found, kPavementMask, env);
        object.m_posOffset = static_cast<uint32_t>(env.Rand() % 2 + 1);
        if (!object.m_pathCursor) detail::ReportPavementBlocked(object, start, stop, env);
        detail::WaitForArrival(object, env);
        return true;
    }
    default: return std::nullopt;
    }
}

// The whole moving object message handler: a family above, or - for every type outside 5..29 and every type without a family (7, 17..20,
// 22, 23, 25, 27) - the base object handler as the fallback. The Env adds the virtual calls of entity::ReceiveMessage.
template<class Env> bool ReceiveWithDefault(object::Mover& object, const messages::Message& message, Env& env)
{
    if (const std::optional<bool> answered = ReceiveMessage(object, message, env)) return *answered;
    return entity::ReceiveMessage(object, message, env);
}

} // namespace obl::moving
