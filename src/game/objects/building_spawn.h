#pragma once

#include "core/ascii_case.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

// Building spawning (docs/architecture/objects.md): what a finished building creates around itself and the staff list.
// SpawnVehicles   posts one place command (kind 4) per vehicle of the definition
// SpawnTables     posts one place command (kind 5) per table of the definition
// DismissStaff    removes a staff id from the serving list
// No game memory here: the glue in src/game/objects/building_methods.cpp supplies an `Env`, the unit tests a fake one.
//
// Env contract:
//   Tile BuildingTile()                       GetTilePosition of the building
//   Tile Neighbour(Tile, int direction)       the neighbouring tile (6 = one tile in X, 0 = one tile in Y for the orientations below)
//   Point TileCenter(Tile)                    the tile centre
//   Point FootprintPoint(Tile, int width, int height, float offsetX, float offsetY, uint32_t rotation)   the rotated footprint offset point
//   const char* VehicleKey(int id) / TableKey(int id)   the key of the WorldParams record
//   void Place(const PlaceCommand&)           posts the place command (message and payload) to g_MessageHandler
//   bool StaffIsFemale(int32_t id)            StaffDef::m_female of the object with that id (DismissStaff only)
//   void Leave(int32_t id)                    Sack of that object (DismissStaff only)
namespace obl {

struct Tile {
    uint32_t x = 0, y = 0;
};
struct Point {
    float x = 0, y = 0;
};

constexpr int kPlaceVehicle = 4; // Message::data of the command
constexpr int kPlaceTable = 5;

// The message {-2, -2, 0, 0, kind} and the payload {key[0x40], x, y, z, owner, rotation} (layout VehicleCreateParams / TableCreateParams).
// SpawnVehicles never writes `z` (it is undefined), so `zWritten` tells a test not to compare it.
struct PlaceCommand {
    int kind = 0;
    char key[0x40] = {};
    float x = 0, y = 0, z = 0;
    bool zWritten = false;
    int32_t owner = 0;
    int32_t rotation = 0;
    bool rotationWritten = true; // SpawnInitialStaff leaves the payload's rotation unset for most staff
    bool pointKnown = true; // false: the point is stack leftovers (SERVE of a building whose sprite direction is above 3), not compared
};

// What the building's definition and sprite give the spawners (the BuildingDef and the sprite's direction).
struct SpawnSite {
    int32_t owner = 0; // Building::m_id
    uint32_t rotation = 0; // sprite direction 0..3
    int32_t width = 0, height = 0; // BuildingDef::m_GridWidth / m_GridHeight
    uint32_t numVehicles = 0;
    int32_t vehicle[5] = {};
    int32_t vehicleOffset[5][2] = {}; // m_VehicleGridOffset
    int32_t vehicleDir[5] = {};
    uint32_t numTables = 0;
    int32_t table[10] = {};
    float tableOffset[10][2] = {};
};

namespace detail {

template<class Env> Tile Walk(Env& env, Tile tile, int direction, int steps)
{
    for (int i = 0; i < steps; ++i) tile = env.Neighbour(tile, direction);
    return tile;
}

// The number of steps of a loop over n points: n - 1 times, never negative.
constexpr int Less(int n)
{
    return std::max(n - 1, 0);
}

inline void CopyKey(char (&to)[0x40], const char* from)
{
    const size_t length = std::min(std::strlen(from), sizeof(to) - 1);
    std::memcpy(to, from, length);
    to[length] = 0;
}

} // namespace detail

// The tile a vehicle waits on: the building's own tile walked along its edges to the grid offset of the vehicle place, by orientation.
template<class Env> Tile VehicleTile(Env& env, const SpawnSite& site, uint32_t i)
{
    Tile tile = env.BuildingTile();
    const int ox = site.vehicleOffset[i][0];
    const int oy = site.vehicleOffset[i][1];
    switch (site.rotation) {
    case 0:
        tile = detail::Walk(env, tile, 6, std::max(ox, 0));
        tile = detail::Walk(env, tile, 0, std::max(oy, 0));
        break;
    case 1:
        tile = detail::Walk(env, tile, 0, detail::Less(site.width - ox));
        tile = detail::Walk(env, tile, 6, std::max(oy, 0));
        break;
    case 2:
        tile = detail::Walk(env, tile, 6, detail::Less(site.width - ox));
        tile = detail::Walk(env, tile, 0, detail::Less(site.height - oy));
        break;
    case 3:
        tile = detail::Walk(env, tile, 0, std::max(ox, 0));
        tile = detail::Walk(env, tile, 6, detail::Less(site.height - oy));
        break;
    default: break;
    }
    return tile;
}

// The direction the vehicle faces: the definition's direction turned against the building's rotation, a signed remainder of 8 (C's `%`).
constexpr int32_t VehicleRotation(const SpawnSite& site, uint32_t i)
{
    return (site.vehicleDir[i] + (4 - static_cast<int32_t>(site.rotation)) * 2) % 8;
}

template<class Env> void SpawnVehicles(Env& env, const SpawnSite& site)
{
    const uint32_t count = std::min<uint32_t>(site.numVehicles, 5);
    for (uint32_t i = 0; i < count; ++i) {
        PlaceCommand command;
        command.kind = kPlaceVehicle;
        detail::CopyKey(command.key, env.VehicleKey(site.vehicle[i]));
        const Point point = env.TileCenter(VehicleTile(env, site, i));
        command.x = point.x;
        command.y = point.y;
        command.owner = site.owner;
        command.rotation = VehicleRotation(site, i);
        env.Place(command);
    }
}

template<class Env> void SpawnTables(Env& env, const SpawnSite& site)
{
    const uint32_t count = std::min<uint32_t>(site.numTables, 10);
    for (uint32_t i = 0; i < count; ++i) {
        PlaceCommand command;
        command.kind = kPlaceTable;
        detail::CopyKey(command.key, env.TableKey(site.table[i]));
        const Point point = env.FootprintPoint(
                env.BuildingTile(), site.width, site.height, site.tableOffset[i][0], site.tableOffset[i][1], site.rotation);
        command.x = point.x;
        command.y = point.y;
        command.z = 0;
        command.zWritten = true;
        command.owner = site.owner;
        command.rotation = static_cast<int32_t>(site.rotation);
        env.Place(command);
    }
}

// The serving list: males then females (`males` + `females` entries; both are counted even though the list holds ten).
struct DismissResult {
    bool found = false;
    bool female = false; // the counter that was decremented rather than the other one
};

// Remove the id and shift the later entries before looking up StaffDef::m_female through env.StaffIsFemale.
// The entry beyond the new end retains its value. Finally env.Leave calls Sack on the staff member.
// Nothing happens when the id is absent from the first `males + females` entries.
template<class Env> DismissResult DismissStaff(Env& env, int32_t (&staff)[10], int32_t& males, int32_t& females, int32_t staffId)
{
    DismissResult result;
    const uint32_t total = static_cast<uint32_t>(females + males);
    uint32_t index = 0;
    while (index < total && index < 10 && staff[index] != staffId) ++index;
    if (index >= total || index >= 10) return result;
    result.found = true;
    for (uint32_t i = index; i + 1 < total && i + 1 < 10; ++i) staff[i] = staff[i + 1];
    if (env.StaffIsFemale(staffId)) {
        --females;
        result.female = true;
    } else {
        --males;
    }
    env.Leave(staffId);
    return result;
}

// ---- staff: HireStaff and SpawnInitialStaff ---------------------------------------------------------------
// Both post one place command per staff member with the same 0x54 B payload as the vehicles and tables; the kind follows the staff
// definition's type string (StaffDef::m_ObjectType). Extra Env contract:
//   StaffInfo StaffDefinition(int definitionId)   the staff definition type and key
//   int BuilderSlot(int32_t staffId)              Builder::m_BuilderNumber of the object with that id (HireStaff, builders only)
//   int Rand()                                    the C runtime's rand (SpawnInitialStaff: lifeguard and repairman pick a sprite by parity)
constexpr int kPlaceServe = 6;
constexpr int kPlaceLifeguard = 7;
constexpr int kPlaceMechanic = 8;
constexpr int kPlaceRepairman = 9;
constexpr int kPlaceBuilder = 10;
constexpr int kPlaceCleaner = 11;
constexpr int kPlaceSecurityGuard = 12;

struct StaffInfo {
    const char* type = ""; // "LIFEGUARD", "MECHANIC", "REP", "CLEANER", "SECURITYGUARD", "BUILDER", "SERVE"
    const char* key = "";
};

// The building as the two staff functions read it.
struct StaffSite {
    int32_t owner = 0;
    uint32_t rotation = 0; // sprite direction 0..3
    int32_t width = 0, height = 0;
    float servePoint[3] = {}; // Building::m_serve_point, where the staff appears
    int32_t males = 0, females = 0; // m_numMalestaff / m_numFemalestaff
    int32_t staff[10] = {}; // m_staff, the serving list
    int32_t maxStaff = 0; // BuildingDef::m_MaxStaff
    uint32_t numInitial = 0; // BuildingDef::m_NumStaff
    int32_t initial[10] = {}; // BuildingDef::m_Staff, definition ids
    int16_t staffOffset[2] = {}; // BuildingDef::m_StaffOffset (the SERVE spot)
    char maleKey[0x40] = {}; // BuildingDef::m_MaleFileName / m_FemaleFileName
    char femaleKey[0x40] = {};
};

// Each builder gets a world offset around the building according to its index.
inline void ApplyStaffOffset(int index, float& x, float& y)
{
    switch (index) {
    case 1: x += 30.0f; break;
    case 2: y += 30.0f; break;
    case 3: x -= 30.0f; break;
    case 4:
        x += 30.0f;
        y += 30.0f;
        break;
    case 5:
        x -= 30.0f;
        y -= 30.0f;
        break;
    case 6:
        x -= 15.0f;
        y -= 25.0f;
        break;
    case 7:
        x -= 20.0f;
        y += 25.0f;
        break;
    case 8:
        x += 25.0f;
        y += 45.0f;
        break;
    case 9:
        x -= 50.0f;
        y -= 15.0f;
        break;
    default: break;
    }
}

namespace detail {

// The kind of staff a type string stands for; 0 when HireStaff/SpawnInitialStaff ignore it. SERVE is only known to SpawnInitialStaff.
inline int StaffKind(const char* type)
{
    struct Entry {
        const char* type;
        int kind;
    };
    constexpr Entry kinds[] = {{"SERVE", kPlaceServe}, {"LIFEGUARD", kPlaceLifeguard}, {"MECHANIC", kPlaceMechanic},
            {"REP", kPlaceRepairman}, {"BUILDER", kPlaceBuilder}, {"CLEANER", kPlaceCleaner}, {"SECURITYGUARD", kPlaceSecurityGuard}};
    for (const Entry& entry : kinds)
        if (ascii::EqualsNoCase(type, entry.type)) return entry.kind;
    return 0;
}

inline PlaceCommand StaffCommand(const StaffSite& site, int kind, const char* key, int32_t rotation)
{
    PlaceCommand command;
    command.kind = kind;
    CopyKey(command.key, key);
    command.x = site.servePoint[0];
    command.y = site.servePoint[1];
    command.z = site.servePoint[2];
    command.zWritten = true;
    command.owner = site.owner;
    command.rotation = rotation;
    return command;
}

} // namespace detail

// Hires one more member of the staff the building's first definition names. False when the list is full (`==` the maximum, the exact test
// matters) or the type is none of the hireable ones (SERVE is not hireable). The new member's index is the lowest builder number the current
// builders do not use, otherwise the count; it travels as the payload's rotation, and builders are offset by it (ApplyStaffOffset).
template<class Env> bool HireStaff(Env& env, const StaffSite& site)
{
    if (site.males + site.females == site.maxStaff) return false;
    const StaffInfo info = env.StaffDefinition(site.initial[0]);
    const int kind = detail::StaffKind(info.type);
    if (kind == 0 || kind == kPlaceServe) return false;
    const uint32_t total = static_cast<uint32_t>(site.males + site.females);
    uint32_t slot = total;
    if (kind == kPlaceBuilder) {
        bool used[50] = {};
        for (uint32_t i = 0; i < total && i < 10; ++i) {
            const int number = env.BuilderSlot(site.staff[i]);
            if (number >= 0 && number < 50) used[number] = true;
        }
        for (uint32_t i = 0; i < total && i < 50; ++i) {
            if (!used[i]) {
                slot = i;
                break;
            }
        }
    }
    PlaceCommand command = detail::StaffCommand(site, kind,
            static_cast<uint32_t>(site.males) > static_cast<uint32_t>(site.females) ? site.maleKey : site.femaleKey,
            static_cast<int32_t>(slot));
    if (kind == kPlaceBuilder) ApplyStaffOffset(static_cast<int>(slot), command.x, command.y);
    env.Place(command);
    return true;
}

// Creates the staff the definition lists, in order. SERVE stands at the staff offset of the footprint (no z, no rotation in the payload); lifeguard
// and repairman take the male or the female key by the parity of one rand() call each; the rest use the staff definition's key. Unknown types are skipped.
template<class Env> void SpawnInitialStaff(Env& env, const StaffSite& site)
{
    const uint32_t count = std::min<uint32_t>(site.numInitial, 10);
    for (uint32_t i = 0; i < count; ++i) {
        const StaffInfo info = env.StaffDefinition(site.initial[i]);
        const int kind = detail::StaffKind(info.type);
        switch (kind) {
        case kPlaceServe: {
            PlaceCommand command;
            command.kind = kind;
            detail::CopyKey(command.key, info.key);
            const Point point = env.FootprintPoint(env.BuildingTile(), site.width, site.height, static_cast<float>(site.staffOffset[0]),
                    static_cast<float>(site.staffOffset[1]), site.rotation);
            command.x = point.x;
            command.y = point.y;
            command.owner = site.owner;
            command.rotationWritten = false;
            command.pointKnown = site.rotation <= 3; // Footprint placement is defined only for rotations 0..3
            env.Place(command);
            break;
        }
        case kPlaceLifeguard:
        case kPlaceRepairman: {
            PlaceCommand command = detail::StaffCommand(site, kind, (env.Rand() & 1) == 0 ? site.femaleKey : site.maleKey, 0);
            command.rotationWritten = false;
            env.Place(command);
            break;
        }
        case kPlaceBuilder: {
            PlaceCommand command = detail::StaffCommand(site, kind, info.key, static_cast<int32_t>(i));
            ApplyStaffOffset(static_cast<int>(i), command.x, command.y);
            env.Place(command);
            break;
        }
        case kPlaceMechanic:
        case kPlaceCleaner:
        case kPlaceSecurityGuard: {
            PlaceCommand command = detail::StaffCommand(site, kind, info.key, 0);
            command.rotationWritten = false;
            env.Place(command);
            break;
        }
        default: break;
        }
    }
}

} // namespace obl
