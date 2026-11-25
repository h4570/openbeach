#pragma once

// A building's `behavior` (docs/architecture/data-formats/game-data.md): our enum for every special case the engine
// decides. The importer derives it from the source key (importer/src/readers/building_behavior_keys.h) and writes its name into
// catalog/buildings/<key>.json; the catalog loader stores it in BuildingDef::m_Behavior, and the engine sites read it
// from the building's definition (`building.m_definition->m_Behavior`) instead of comparing names. One value per key
// the engine names, so later predicates (power-exempt, bar, staff home) are functions of the value, not extra fields.
// Names are written as JSON and listed in data/schemas/building.schema.json (a unit test keeps them equal).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace obl {

enum class BuildingBehavior {
    None,
    Hotel, // HOTEL: accommodation income, power-exempt, the guests' first lodging (guest_placement, vehicle messages)
    AccomBlock, // ACCOMBLK: accommodation income, power-exempt, lodging after the hotels
    BeachHut, // BEACHHUT: enter report (building_lifecycle)
    MainPool, // MAINPOOL: enter report (building_lifecycle)
    LifeguardPost, // BAYWATCH: swimming area at startup, lifeguard home and browser
    MechanicHome, // HANDYMAN: spared under a power shortage, power-exempt, mechanic home and browser
    BuilderHome, // BUILDERYARD: power-exempt, builder home and browser
    CleanerHome, // CLEANERSHACK: cleaner home and browser
    RepOffice, // REP: rep home and browser
    SecurityPost, // SECURITY_POST: security guard home and browser
    Stage, // STAGE: creates the stage events at startup
    ShipStop, // WHARF: boards queued guests, the ship arrival point
    BusStop, // BUSSTOP: boards queued guests, the arrival point without a wharf
    ComplaintsHut, // COMPLAINTHUT: boards queued guests, power-exempt, guest complaints
    IceStall, // ICESTALL: food that water contamination does not spoil
    FastFood, // FASTFOOD: as ICESTALL. The engine once also named "BARBEQUE", which is no key (BARBECUE is), so barbecue is None.
    WaterCleaner, // WATERCLN: reduces water pollution, built tick
    Toilet, // LOO: cleaning calls, flies, staff feedback, drawing
    JetskiHire, // NJJETSKIHIRE: engine idle sound
    SpeedboatHire, // NJSPDBOATHIRE: engine idle sound
    EquipmentHire, // EQPTHIRE: 20 toys at construction
    Bar, // BEACHBAR, POOLBAR, BAR1, BAR2: counted as bars by world scripts
    CocktailBar, // COCKTAIL: a bar that sells the beer's cocktail name
    DjClubFootprint, // CLUB1: the DJ stands at a footprint point, turned with the building
    DjClubOffset, // CLUB2: the DJ stands at a per-rotation offset from the building
    FirstAid, // FIRSTAID: guest first aid lookup
    PowerGenerator, // POWERGEN: power table, power events
    SouvenirShop, // SOUVSHOP: souvenir commodity provider
    GeneralStore, // GENSTORE: built tick
    Jetty, // every key containing "JETTY" (strstr): built without builders, not a rep/guard target
};

struct BuildingBehaviorName {
    BuildingBehavior behavior;
    const char* name; // the JSON value
};

inline constexpr std::array<BuildingBehaviorName, 31> kBuildingBehaviorNames = {{
        {BuildingBehavior::None, "none"},
        {BuildingBehavior::Hotel, "hotel"},
        {BuildingBehavior::AccomBlock, "accom_block"},
        {BuildingBehavior::BeachHut, "beach_hut"},
        {BuildingBehavior::MainPool, "main_pool"},
        {BuildingBehavior::LifeguardPost, "lifeguard_post"},
        {BuildingBehavior::MechanicHome, "mechanic_home"},
        {BuildingBehavior::BuilderHome, "builder_home"},
        {BuildingBehavior::CleanerHome, "cleaner_home"},
        {BuildingBehavior::RepOffice, "rep_office"},
        {BuildingBehavior::SecurityPost, "security_post"},
        {BuildingBehavior::Stage, "stage"},
        {BuildingBehavior::ShipStop, "ship_stop"},
        {BuildingBehavior::BusStop, "bus_stop"},
        {BuildingBehavior::ComplaintsHut, "complaints_hut"},
        {BuildingBehavior::IceStall, "ice_stall"},
        {BuildingBehavior::FastFood, "fast_food"},
        {BuildingBehavior::WaterCleaner, "water_cleaner"},
        {BuildingBehavior::Toilet, "toilet"},
        {BuildingBehavior::JetskiHire, "jetski_hire"},
        {BuildingBehavior::SpeedboatHire, "speedboat_hire"},
        {BuildingBehavior::EquipmentHire, "equipment_hire"},
        {BuildingBehavior::Bar, "bar"},
        {BuildingBehavior::CocktailBar, "cocktail_bar"},
        {BuildingBehavior::DjClubFootprint, "dj_club_footprint"},
        {BuildingBehavior::DjClubOffset, "dj_club_offset"},
        {BuildingBehavior::FirstAid, "first_aid"},
        {BuildingBehavior::PowerGenerator, "power_generator"},
        {BuildingBehavior::SouvenirShop, "souvenir_shop"},
        {BuildingBehavior::GeneralStore, "general_store"},
        {BuildingBehavior::Jetty, "jetty"},
}};

inline const char* BuildingBehaviorName(BuildingBehavior behavior)
{
    for (const auto& entry : kBuildingBehaviorNames)
        if (entry.behavior == behavior) return entry.name;
    return "none";
}

// The inverse of BuildingBehaviorName (the catalog loader reads the JSON value). False for a name that is no value.
inline bool BuildingBehaviorFromName(std::string_view name, BuildingBehavior& out)
{
    for (const auto& entry : kBuildingBehaviorNames) {
        if (name == entry.name) {
            out = entry.behavior;
            return true;
        }
    }
    return false;
}

// Guest lodging role of a building: hotels are filled first, then accommodation blocks.
enum class AccommodationKind : uint8_t {
    None,
    Hotel,
    Block,
};

inline const char* AccommodationKindName(AccommodationKind kind)
{
    switch (kind) {
    case AccommodationKind::Hotel: return "hotel";
    case AccommodationKind::Block: return "block";
    default: return "none";
    }
}

inline bool AccommodationKindFromName(std::string_view name, AccommodationKind& out)
{
    if (name == "none") {
        out = AccommodationKind::None;
        return true;
    }
    if (name == "hotel") {
        out = AccommodationKind::Hotel;
        return true;
    }
    if (name == "block") {
        out = AccommodationKind::Block;
        return true;
    }
    return false;
}

// Combinable building traits (docs/modding/reference/buildings.md). `behavior` populates the defaults via
// DefaultBuildingTraits; explicit trait fields in catalog/buildings/<key>.json override individual entries.
struct BuildingTraits {
    bool powerExempt = false; // spared from power-shortage shedding (world_power.h)
    bool boardsGuests = false; // queues guests in place on MT_WAIT_FOR_BOAT and boards them on MT_GUEST_SERVED; arrival stop
    bool contaminationSafe = false; // food/drink served here is not spoiled by water contamination
    bool countsAsBar = false; // counted as a bar by world scripts
    bool ownsSwimmingArea = false; // claims a beach/sea swimming area on completion and handles beach-open commands
    bool toilet = false; // cleaning job, flies/smoke when dirty, cleaner targeting and feedback
    bool waterCleaner = false; // reduces water contamination illness while open
    bool powerGenerator = false; // listed in the power generators UI table
    bool instantBuild = false; // built without builders
    bool patrolExempt = false; // excluded from rep and security guard patrols
    int initialRentalStock = 0; // starting stock of snorkels, lilos and surfboards (equipment_hire = 20)
    AccommodationKind accommodation = AccommodationKind::None; // guest lodging tier and accommodation income
    std::string idleSound; // looping sound played while open (and with a free vehicle when the building has vehicles)
    std::string objectScript; // optional scripts/object/<stem>.obs run while the building stands
};

inline BuildingTraits DefaultBuildingTraits(BuildingBehavior behavior)
{
    BuildingTraits t;
    switch (behavior) {
    case BuildingBehavior::Hotel:
        t.powerExempt = true;
        t.accommodation = AccommodationKind::Hotel;
        break;
    case BuildingBehavior::AccomBlock:
        t.powerExempt = true;
        t.accommodation = AccommodationKind::Block;
        break;
    case BuildingBehavior::LifeguardPost: t.ownsSwimmingArea = true; break;
    case BuildingBehavior::MechanicHome:
    case BuildingBehavior::BuilderHome: t.powerExempt = true; break;
    case BuildingBehavior::ShipStop:
    case BuildingBehavior::BusStop: t.boardsGuests = true; break;
    case BuildingBehavior::ComplaintsHut:
        t.powerExempt = true;
        t.boardsGuests = true;
        break;
    case BuildingBehavior::IceStall:
    case BuildingBehavior::FastFood: t.contaminationSafe = true; break;
    case BuildingBehavior::WaterCleaner: t.waterCleaner = true; break;
    case BuildingBehavior::Toilet: t.toilet = true; break;
    case BuildingBehavior::JetskiHire:
    case BuildingBehavior::SpeedboatHire: t.idleSound = "newjetskiidle"; break;
    case BuildingBehavior::EquipmentHire: t.initialRentalStock = 20; break;
    case BuildingBehavior::Bar:
    case BuildingBehavior::CocktailBar: t.countsAsBar = true; break;
    case BuildingBehavior::PowerGenerator: t.powerGenerator = true; break;
    case BuildingBehavior::Jetty:
        t.instantBuild = true;
        t.patrolExempt = true;
        break;
    default: break;
    }
    return t;
}

} // namespace obl
