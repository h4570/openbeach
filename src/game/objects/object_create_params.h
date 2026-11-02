#pragma once

#include <cstdint>

// The records the world script hands to the Create* calls by pointer: buildings, guests, staff, builders, performers,
// sharks, scenery, tables and vehicles. These are C++ arguments, never stored by the save writer.
namespace obl::object {

// Argument of CreateBuilding (by pointer): one "Create Building" of the scenario script.
struct BuildingCreateParams {
    char m_key[0x40]; // BuildingDef key
    int m_tileX;
    int m_tileY;
    int m_orientation;
    float m_prices[5]; // -1.0f = default price (compared as raw bits)
    int m_beerType; // -1 = default
    float m_beerStrength; // forwarded to SetBeerStrength
    int m_age; // days, Building::m_BuildingAge
    uint8_t m_broken;
};

// Argument of CreateGuest (by pointer).
struct GuestCreateParams {
    char m_key[0x40]; // GuestDef key
    float m_pos[3]; // world coordinates
};

// Argument of the staff factories CreateServiceStaff/Lifeguard/Mechanic/Cleaner/SecurityGuard/Rep (by pointer).
// CreateBuilder reads the same record plus the BuilderNumber (BuilderCreateParams).
struct StaffCreateParams {
    char m_key[0x40]; // StaffDef key (staff key lookup)
    float m_pos[3]; // world coordinates, passed to the Teleport slot
    int m_homeId; // the SetHome slot's argument (building id, 0 in the world script's records)
};

// Argument of CreateBuilder (by pointer): StaffCreateParams plus the BuilderNumber (flat, so the struct stays standard-layout).
struct BuilderCreateParams {
    char m_key[0x40]; // StaffDef key (staff key lookup)
    float m_pos[3]; // world coordinates: the Teleport slot's arguments and the builder's m_HomePoint
    int m_homeId; // the SetHome slot's argument (0 in the world script's records)
    int m_builderNumber; // SetBuilderNumber
};

// Argument of the six performer factories CreateWeightLifter/Singer/Aerobic/Cabaret/Catwalk/WetTShirt (by pointer).
// There is no key: the object key is a literal of the class (k_ObjectKey<Class>).
struct PerformerCreateParams {
    int m_variant; // SetVariant chooses the sprite and first frame for the act or outfit
    float m_pos[3]; // world coordinates, passed to the Teleport slot
    int m_buildingId; // AttachToBuilding assigns the building whose events the performer joins
};

// Argument of CreateShark (by pointer).
struct SharkCreateParams {
    char m_key[0x40]; // passed to the object manager CreateObject
    int m_initParam; // SetInitParam
    int m_bounds[4]; // SetBounds: four words copied to the shark
    float m_pos[3]; // world coordinates, passed to the Teleport slot
};

// Argument of CreateScenery (by pointer): two 32-byte strings.
struct SceneryCreateParams {
    char m_key[0x20];
    char m_name[0x20];
    uint32_t m_tile[2];
    uint8_t m_setDirection;
    uint8_t m_direction;
    int32_t m_ownerBuildId; // -2 preserves the constructor's owner
};

// Argument of CreateTable (by pointer).
struct TableCreateParams {
    char m_key[0x40];
    float m_pos[3];
    int32_t m_buildingId;
    int32_t m_orientation;
};

// Argument of CreateVehicle (by pointer).
struct VehicleCreateParams {
    char m_key[0x40];
    float m_pos[3];
    int32_t m_homeId;
    int32_t m_direction;
};

} // namespace obl::object
