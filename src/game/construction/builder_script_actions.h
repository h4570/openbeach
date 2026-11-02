#pragma once

#include "game/map/grid_math.h"
#include "core/float_to_int.h"

#include <cstdint>

namespace obl::construction {

constexpr int kMsgBuilderWhistle = 0x36; // a female guest is close: the builder whistles
constexpr int kMsgBuilderNoGuests = 0x3c; // nobody to whistle at
constexpr int kMsgBuilderSource = 0x67; // `extra` of both messages (the sender kind the script tests)
constexpr int kMaxWhistlers = 2; // g_NumBuildersWhistling stops counting here

// construction::CheckForFemaleGuests. Walks the guests (object class 3) in list order; the list pointer and length are read once and every
// entry is read when its turn comes, because the Post of an earlier guest delivers the builder's script at once, which may ask for another class
// and rewrite the list's buffer. A guest counts when its tile is within two tiles of the builder's in both axes (the integer distances are made
// absolute with the `(x ^ s) - s` idiom and compared as floats), its definition's gender is 1 and fewer than two builders are whistling already.
// Each counted guest takes one place of the counter and posts MSG 0x36 (sender = receiver = the builder); when none counted, one MSG 0x3c follows.
//   using Object = const void*;                          opaque handle of an object (the builder `builder` too)
//   const Object* GuestList(uint32_t& count)             the object manager's list of class 3
//   void TilePosition(Object, int32_t out[2])            object::Base::GetTilePosition
//   bool IsFemale(Object guest)                          guest->m_definition->m_Gender == 1
//   int32_t& Whistling()                                 g_NumBuildersWhistling
//   void Post(int sender, int receiver, int type)        messages::Post with flags 0, data 0, extra 0x67, floatData 0
template<class Env> void CheckForFemaleGuests(typename Env::Object builder, int builderId, Env& env)
{
    uint32_t count = 0;
    const typename Env::Object* guests = env.GuestList(count);
    bool whistled = false;
    for (uint32_t i = 0; i < count; ++i) {
        int32_t guestTile[2], ownTile[2];
        env.TilePosition(guests[i], guestTile);
        env.TilePosition(builder, ownTile);
        auto absolute = [](int32_t value) {
            const uint32_t sign = static_cast<uint32_t>(value) >> 31 ? 0xffffffffu : 0u; // all ones for a negative difference
            return static_cast<int32_t>((static_cast<uint32_t>(value) ^ sign) - sign);
        };
        const int32_t dx = absolute(static_cast<int32_t>(static_cast<uint32_t>(guestTile[0]) - static_cast<uint32_t>(ownTile[0])));
        const int32_t dy = absolute(static_cast<int32_t>(static_cast<uint32_t>(guestTile[1]) - static_cast<uint32_t>(ownTile[1])));
        if (static_cast<float>(dx) <= 2.0f && static_cast<float>(dy) <= 2.0f && env.IsFemale(guests[i]) &&
                env.Whistling() < kMaxWhistlers) {
            ++env.Whistling();
            whistled = true;
            env.Post(builderId, builderId, kMsgBuilderWhistle);
        }
    }
    if (!whistled) env.Post(builderId, builderId, kMsgBuilderNoGuests);
}

template<class Env> bool FindNearestValidTile(typename Env::Object builder, Env& env, int32_t out[2])
{
    int32_t position[2];
    env.TilePosition(builder, position);
    const uint32_t width = env.Width();
    const uint32_t height = env.Height();
    auto valid = [](uint32_t flags) { return (flags & 0x80000000u) != 0 && (flags & 0x10u) == 0; };
    auto point = [&](grid_math::TileXY tile) {
        const grid_math::WorldPoint at = grid_math::TileToWorldPoint(env.TileSize(), tile.x, tile.y, 0);
        out[0] = FloatToIntWrap(static_cast<double>(at.x));
        out[1] = FloatToIntWrap(static_cast<double>(at.y));
    };
    grid_math::TileXY tile{static_cast<uint32_t>(position[0]), static_cast<uint32_t>(position[1])};
    if (valid(env.CellFlags(tile.x, tile.y))) {
        point(tile);
        return true;
    }
    for (uint32_t ring = 0; ring < height; ++ring) {
        tile = grid_math::Neighbour(width, height, tile.x, tile.y, 3);
        if (valid(env.CellFlags(tile.x, tile.y))) {
            point(tile);
            return true;
        }
    }
    return false;
}

} // namespace obl::construction
