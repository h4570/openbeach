#pragma once

#include <cstdint>
#include <cstring>

// Pure scenery decisions: supported states, selectability, available seats, bin fill, names and approach points.
// src/game/objects/scenery.cpp reads the object fields and applies these decisions.
namespace obl::scenery {

// Scenery supports states 0, 1, 3 and 4. All other values, including negative values converted to unsigned, are rejected.
inline bool StateIsKnown(uint32_t state)
{
    return state <= 4 && state != 2;
}

// Scenery baked into the background cannot be selected. Otherwise either m_canSelect or m_hasMenu permits selection.
inline bool IsSelectable(bool burnedIntoBackground, bool canSelect, bool hasMenu)
{
    return !burnedIntoBackground && (hasMenu || canSelect);
}

// Finding a free scenery seat: the next seat to give out is the highest one (`m_FreeSeat = m_MaxNumAttachedGuests`), the result is how many
// seats are left. Both values are plain 32-bit ints (a wrapping subtraction).
inline int32_t FreeSeatsLeft(int32_t maxAttached, int32_t attached)
{
    return static_cast<int32_t>(static_cast<uint32_t>(maxAttached) - static_cast<uint32_t>(attached));
}

// A fill request above 70% of capacity fills the bin completely: 8 of 10 becomes 10, while 7 stays 7.
// Compare in double precision so the capacity threshold is rounded consistently.
inline uint32_t ClampBinFill(uint32_t requested, uint32_t capacity)
{
    return static_cast<double>(capacity) * 0.7 < static_cast<double>(requested) ? capacity : requested;
}

// The animation SetBinFill starts on a bin (the name is "Bin" in any spelling): "bin_fullbin_static" only for a full bin,
// "bin_emptybin_static" for every other fill (there is no "partly filled" animation); other scenery keeps its animation.
enum class BinAnim { None, Full, Empty };

inline BinAnim BinFillAnim(bool isBin, uint32_t fill, uint32_t capacity)
{
    if (!isBin) return BinAnim::None;
    return fill == capacity ? BinAnim::Full : BinAnim::Empty;
}

inline const char* BinAnimName(BinAnim anim)
{
    switch (anim) {
    case BinAnim::Full: return "bin_fullbin_static";
    case BinAnim::Empty: return "bin_emptybin_static";
    default: return nullptr;
    }
}

// Copy the name with its terminating zero. The caller must provide enough storage for strlen(name) + 1 bytes.
// Callers use short scenery definition keys. This updates the base object name; the scenery name query reads its separate key.
inline void CopyName(char* target, const char* name)
{
    std::memcpy(target, name, std::strlen(name) + 1);
}

// The scenery approach point on a bench: the point a guest walks to is the bench position plus an offset by the seat that is handed out
// next (`m_FreeSeat` 0 or 1) and by the direction of the sprite (0..7, in world units); any other seat number gives a zero offset. The
// offset is undefined for directions above 7 with seat 0 or 1: `defined` is false there and callers use zero.
struct SeatOffset {
    float x = 0.0f;
    float y = 0.0f;
    bool defined = true;
};

inline SeatOffset BenchSeatOffset(int32_t freeSeat, uint32_t direction)
{
    static constexpr float kSeat0[8][2] = {{-13, 20}, {-22, 8}, {-19, -20}, {-3, -24}, {15, -12}, {27, 0}, {22, -16}, {24, 2}};
    static constexpr float kSeat1[8][2] = {{15, 20}, {-1, 28}, {-19, 14}, {-24, -4}, {-15, -12}, {2, -24}, {22, 16}, {6, 24}};
    SeatOffset offset;
    if (freeSeat != 0 && freeSeat != 1) return offset;
    if (direction > 7) {
        offset.defined = false;
        return offset;
    }
    const float (&row)[2] = freeSeat == 0 ? kSeat0[direction] : kSeat1[direction];
    offset.x = row[0];
    offset.y = row[1];
    return offset;
}

} // namespace obl::scenery
