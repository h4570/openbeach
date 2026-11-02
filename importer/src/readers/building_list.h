#pragma once

#include "openbeach/importer/byte_source.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obl::convert {

struct BlCommodityFamily {
    std::string key, name;
};

struct BlCommodity {
    std::string key, name;
    int family = 0;
};

struct BlOpeningHours {
    std::string key;
    float open = 0, close = 0;
};

struct BlDesire {
    std::string key, name;
    std::vector<int> families;
};

struct BlStaff {
    std::string key, name;
    bool female = false;
    int wage = 0;
    std::string sprite, script, menu;
    int recommendedWage = 0;
    float scale = 0, expectationGrowth = 0, initialHappiness = 0;
    int startHour = 0, endHour = 0;
};

struct BlEvent {
    std::string key;
    float cost = 0, price = 0;
    std::array<float, 2> intensity{}; // men, women
};

struct BlBeer {
    std::string key, brand, cocktail;
    float baseCost = 0, strengthCost = 0, alcoholStrength = 0, price = 0;
    int effect = 0; // 0 fight, 1 flirt, 2 vandalise, 3 happy, 4 sad
};

struct BlFine {
    std::string key;
    float cost = 0;
};

struct BlVehicle {
    std::string key, name, sprite;
    int width = 0, height = 0, capacity = 0;
    float maxSpeed = 0;
    std::string towed;
    float force = 0, mass = 0;
    int moveType = 0; // 1 points, 2 boundary
    std::string animSet;
    std::vector<int> staff;
    std::array<float, 2> creationPoint{};
};

struct BlChair {
    std::array<float, 2> pos{};
    int dir = 0; // 0 N .. 7 NW
    std::vector<int> walkPoints;
    std::optional<std::array<float, 2>> servePoint;
};

struct BlTable {
    std::string key, name, tableSprite, chairSprite;
    int width = 0, height = 0;
    std::vector<std::array<float, 2>> walkPoints;
    std::vector<BlChair> chairs;
    std::string sitDownAnim, getUpAnim;
    std::vector<std::string> satDownAnims;
    bool serveTable = false, hasWaiter = false;
    int offsetX = 0, offsetY = 0;
    std::string waiterSprite, waiterWalkAnim, waiterServeAnim, callWaiterAnim, waitWaiterAnim;
};

struct BlScenery {
    std::string key, name, sprite, anim, menu;
    int width = 0, height = 0, cost = 0, capacity = 0, icon = 0, category = 0, pathFlag = 0;
    uint32_t buildOn = 0; // LAND 1, BEACH 2, SHALLOW_WATER 4, DEEP_SEA 8, PATH 0x80
    float rating = 0;
    bool collide = false, clean = false, repair = false, burn = false, userRemove = false, vegetation = false, water = false;
};

struct BlBuildingCommodity {
    int commodity = 0;
    float intensityMen = 0, intensityWomen = 0, dailyIntensityFactor = 0, price = 0, dailyPriceFactor = 0, cost = 0;
};

struct BlAnimSprite {
    std::string sprite;
    std::array<std::array<int, 2>, 4> points{};
    bool flag = false;
};

struct BlVehiclePlace {
    int dir = 0, x = 0, y = 0;
};

struct BlTablePlace {
    int table = 0;
    float x = 0, y = 0;
};

struct BlBuilding {
    std::string key, name, sprite, tree, menu, construct, maleFile, femaleFile;
    bool customConstruct = false;
    int icon = 0, category = 0, cost = 0, refund = 0, width = 0, height = 0, guestCapacity = 0, buildTime = 0, demolishTime = 0;
    int openingHours = 0, maxInstances = 0, attractor = 0, maxQueue = 0, maxStaff = 0, power = 0, failPercent = 0, failAge = 0;
    int maxBuilders = 0;
    float avFailTime = 0;
    std::array<int16_t, 2> serve{}, staffOffset{}, entrance{};
    uint32_t buildOn = 0; // LAND 1, BEACH 2, SHALLOW_WATER 4, DEEP_SEA 8
    std::vector<BlBuildingCommodity> commodities;
    std::vector<int> staff, vehicles, events, basePlates;
    std::vector<BlVehiclePlace> vehiclePlaces;
    std::vector<BlTablePlace> tables;
    std::vector<BlAnimSprite> anims;
    std::vector<std::array<float, 2>> walkPoints;
};

struct BuildingList {
    std::vector<BlCommodityFamily> families;
    std::vector<BlCommodity> commodities;
    std::vector<BlOpeningHours> openingHours;
    std::vector<BlDesire> desires;
    std::vector<BlStaff> staff;
    std::vector<BlEvent> events;
    std::vector<BlBeer> beers;
    std::vector<BlFine> fines;
    std::vector<BlVehicle> vehicles;
    std::vector<BlTable> tables;
    std::vector<BlScenery> scenery;
    std::vector<BlBuilding> buildings;
};

// Parses the decoded text (bytes XOR 0xFF undone). False with a message naming the line's keyword on anything the current game
// would not load the same way: odd quotes, an unrecognized keyword or value, a missing reference, a list past its capacity, a
// field line of a kind the catalog does not hold (guests, name pools).
bool ParseBuildingList(std::string_view text, BuildingList& list, std::string& error);

// Reads Data/Script/BuildingList.ble of an installation and parses it.
bool ReadBuildingList(const ByteSource& source, BuildingList& list, std::string& error);

} // namespace obl::convert
