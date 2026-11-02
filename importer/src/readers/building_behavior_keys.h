#pragma once

// Which engine behavior (game/tables/building_behavior.h) a building of the user's copy has, by its key: the catalog
// writer stores the value, so the game never compares keys itself.

#include "game/tables/building_behavior.h"

#include <cstring>

namespace obl {

// The behavior of a building key of the user's copy, compared as the engine compares it (exact, case-sensitive; JETTY by substring).
inline BuildingBehavior BehaviorFromKey(const char* key)
{
    struct Entry {
        const char* key;
        BuildingBehavior behavior;
    };
    static constexpr Entry kKeys[] = {{"HOTEL", BuildingBehavior::Hotel}, {"ACCOMBLK", BuildingBehavior::AccomBlock},
            {"BEACHHUT", BuildingBehavior::BeachHut}, {"MAINPOOL", BuildingBehavior::MainPool},
            {"BAYWATCH", BuildingBehavior::LifeguardPost}, {"HANDYMAN", BuildingBehavior::MechanicHome},
            {"BUILDERYARD", BuildingBehavior::BuilderHome}, {"CLEANERSHACK", BuildingBehavior::CleanerHome},
            {"REP", BuildingBehavior::RepOffice}, {"SECURITY_POST", BuildingBehavior::SecurityPost}, {"STAGE", BuildingBehavior::Stage},
            {"WHARF", BuildingBehavior::ShipStop}, {"BUSSTOP", BuildingBehavior::BusStop},
            {"COMPLAINTHUT", BuildingBehavior::ComplaintsHut}, {"ICESTALL", BuildingBehavior::IceStall},
            {"FASTFOOD", BuildingBehavior::FastFood}, {"WATERCLN", BuildingBehavior::WaterCleaner}, {"LOO", BuildingBehavior::Toilet},
            {"NJJETSKIHIRE", BuildingBehavior::JetskiHire}, {"NJSPDBOATHIRE", BuildingBehavior::SpeedboatHire},
            {"EQPTHIRE", BuildingBehavior::EquipmentHire}, {"BEACHBAR", BuildingBehavior::Bar}, {"POOLBAR", BuildingBehavior::Bar},
            {"BAR1", BuildingBehavior::Bar}, {"BAR2", BuildingBehavior::Bar}, {"COCKTAIL", BuildingBehavior::CocktailBar},
            {"CLUB1", BuildingBehavior::DjClubFootprint}, {"CLUB2", BuildingBehavior::DjClubOffset},
            {"FIRSTAID", BuildingBehavior::FirstAid}, {"POWERGEN", BuildingBehavior::PowerGenerator},
            {"SOUVSHOP", BuildingBehavior::SouvenirShop}, {"GENSTORE", BuildingBehavior::GeneralStore}};
    for (const Entry& entry : kKeys)
        if (std::strcmp(key, entry.key) == 0) return entry.behavior;
    if (std::strstr(key, "JETTY") != nullptr) return BuildingBehavior::Jetty;
    return BuildingBehavior::None;
}

} // namespace obl
