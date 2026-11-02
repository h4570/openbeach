#include "chaos.h"

#include "game/game/command_recorder.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <limits>
#include <system_error>

namespace obl::chaos {
namespace {

using command::Command;

// Relative weights of the command kinds (Kind order). Placement and staff commands dominate because they create and destroy the
// objects every other command aims at.
constexpr std::array<uint32_t, static_cast<size_t>(Kind::Count)> kWeights = {
        16, // PlaceBuilding
        7, // PlaceScenery
        7, // PaintSurface
        6, // Dozer
        6, // Demolish
        8, // HireStaff
        4, // DismissStaff
        5, // StaffWage
        4, // StaffShift
        5, // SendStaff
        5, // BuildingOpen
        5, // CallService
        6, // Price
        4, // BeerOrShow
        7, // ObjectControl
        2, // Pause
        3, // Speed
        2, // BreakObject
};

constexpr std::array<std::string_view, static_cast<size_t>(Kind::Count)> kKindNames = {"place-building", "place-scenery", "paint-surface",
        "dozer", "demolish", "hire-staff", "dismiss-staff", "staff-wage", "staff-shift", "send-staff", "building-open", "call-service",
        "price", "beer-or-show", "object-control", "pause", "speed", "break-object"};

// The game's "no object" (it appears in the id lists) and other ids no live object should resolve to.
constexpr std::array<uint32_t, 4> kJunkIds = {0, 0xffffffffu, 0x7fffffffu, 123456};
// Wages and values outside what a slider produces: the executor must take or refuse them without breaking the world.
constexpr std::array<int32_t, 6> kOddInts = {
        0, -10, 1000, 100000, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min()};
constexpr std::array<float, 6> kOddPrices = {0.0f, -1.0f, 0.01f, 100.0f, 10000.0f, 1.0e7f};
// A key no table has.
constexpr std::string_view kNoSuchKey = "NOSUCHKEY";

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::pair<std::string_view, std::string_view> SplitWord(std::string_view line)
{
    const size_t space = line.find_first_of(" \t");
    if (space == std::string_view::npos) return {line, {}};
    return {line.substr(0, space), Trim(line.substr(space + 1))};
}

template<class T> bool ParseNumber(std::string_view text, T& out)
{
    if (text.empty()) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

} // namespace

uint64_t Rng::Next()
{
    uint64_t z = (m_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

uint32_t Rng::Below(uint32_t count)
{
    return count == 0 ? 0 : static_cast<uint32_t>(Next() % count);
}

int32_t Rng::Between(int32_t low, int32_t high)
{
    const auto span = static_cast<uint32_t>(static_cast<int64_t>(high) - low + 1);
    return static_cast<int32_t>(low + static_cast<int64_t>(Below(span)));
}

bool Rng::Chance(uint32_t percent)
{
    return Below(100) < percent;
}

std::string_view KindName(Kind kind)
{
    return kind < Kind::Count ? kKindNames[static_cast<size_t>(kind)] : "none";
}

Kind ChaosBot::DrawKind()
{
    uint32_t total = 0;
    for (const uint32_t weight : kWeights) total += weight;
    uint32_t pick = m_rng.Below(total);
    for (size_t i = 0; i < kWeights.size(); ++i) {
        if (pick < kWeights[i]) return static_cast<Kind>(i);
        pick -= kWeights[i];
    }
    return Kind::PlaceBuilding;
}

uint32_t ChaosBot::AnyObject(const bot::WorldView& view)
{
    const uint32_t roll = m_rng.Below(100);
    if (roll < 45 && !view.buildings.empty()) return view.buildings[m_rng.Below(static_cast<uint32_t>(view.buildings.size()))].id;
    if (roll < 70) return AnyStaff(view);
    if (roll < 90 && !m_seen.empty()) {
        auto it = m_seen.begin();
        std::advance(it, m_rng.Below(static_cast<uint32_t>(m_seen.size())));
        return *it;
    }
    return m_rng.Chance(50) ? kJunkIds[m_rng.Below(static_cast<uint32_t>(kJunkIds.size()))] : m_rng.Below(5000);
}

uint32_t ChaosBot::AnyBuilding(const bot::WorldView& view)
{
    if (view.buildings.empty() || m_rng.Chance(15)) return AnyObject(view);
    return view.buildings[m_rng.Below(static_cast<uint32_t>(view.buildings.size()))].id;
}

uint32_t ChaosBot::AnyStaff(const bot::WorldView& view)
{
    std::vector<uint32_t> staff;
    for (const auto& ids : view.staffIds) staff.insert(staff.end(), ids.begin(), ids.end());
    if (staff.empty() || m_rng.Chance(10)) {
        if (!m_seen.empty() && m_rng.Chance(50)) {
            auto it = m_seen.begin();
            std::advance(it, m_rng.Below(static_cast<uint32_t>(m_seen.size())));
            return *it;
        }
        return kJunkIds[m_rng.Below(static_cast<uint32_t>(kJunkIds.size()))];
    }
    return staff[m_rng.Below(static_cast<uint32_t>(staff.size()))];
}

void ChaosBot::Tile(const bot::WorldView& view, int32_t& x, int32_t& y)
{
    const int32_t width = std::max(view.mapWidth, 1);
    const int32_t height = std::max(view.mapHeight, 1);
    const uint32_t roll = m_rng.Below(100);
    if (roll < 40 && !view.buildings.empty()) {
        // Next to, on and under the standing buildings: where paths, scenery and demolition meet.
        const bot::Building& near = view.buildings[m_rng.Below(static_cast<uint32_t>(view.buildings.size()))];
        x = near.tileX + m_rng.Between(-6, 6);
        y = near.tileY + m_rng.Between(-6, 6);
    } else if (roll < 85) {
        x = m_rng.Between(0, width - 1);
        y = m_rng.Between(0, height - 1);
    } else if (roll < 95) {
        // The map edges and one tile past them.
        constexpr std::array<int32_t, 4> kEdge = {0, 1, -1, -2};
        const int32_t edgeX = kEdge[m_rng.Below(static_cast<uint32_t>(kEdge.size()))];
        const int32_t edgeY = kEdge[m_rng.Below(static_cast<uint32_t>(kEdge.size()))];
        x = edgeX >= 0 ? edgeX : width + 1 + edgeX;
        y = edgeY >= 0 ? edgeY : height + 1 + edgeY;
        if (m_rng.Chance(50))
            x = m_rng.Between(0, width - 1);
        else
            y = m_rng.Between(0, height - 1);
    } else {
        x = m_rng.Between(-1000, 1000);
        y = m_rng.Between(-1000, 1000);
    }
}

std::string ChaosBot::Key(const std::vector<std::string>& keys)
{
    if (keys.empty() || m_rng.Chance(5)) return std::string(kNoSuchKey);
    return keys[m_rng.Below(static_cast<uint32_t>(keys.size()))];
}

int32_t ChaosBot::Direction()
{
    // 0-3 are the supported quarter turns; 4-7 trapped buildings in construction before the executor refused them.
    constexpr std::array<int32_t, 6> kBad = {4, 5, 6, 7, -1, 100};
    return m_rng.Chance(85) ? m_rng.Between(0, 3) : kBad[m_rng.Below(static_cast<uint32_t>(kBad.size()))];
}

float ChaosBot::Price()
{
    if (m_rng.Chance(70)) return static_cast<float>(m_rng.Between(0, 40)) * 0.5f;
    return kOddPrices[m_rng.Below(static_cast<uint32_t>(kOddPrices.size()))];
}

command::Command ChaosBot::Draw(Kind kind, const bot::WorldView& view, const bot::Queries& queries)
{
    int32_t x = 0, y = 0;
    switch (kind) {
    case Kind::PlaceBuilding: {
        std::string key;
        if (!view.buildingTypes.empty() && m_rng.Chance(90))
            key = view.buildingTypes[m_rng.Below(static_cast<uint32_t>(view.buildingTypes.size()))].key;
        else
            key = Key(view.sceneryKeys);
        if (m_rng.Chance(40)) {
            // A placement the fit check accepts, so the world fills up and the other commands get targets (cash and tree aside).
            for (int attempt = 0; attempt < 16; ++attempt) {
                Tile(view, x, y);
                const int32_t direction = m_rng.Between(0, 3);
                if (queries.BuildingFits(key, x, y, direction)) return command::PlaceBuilding{key, x, y, direction};
            }
        }
        Tile(view, x, y);
        return command::PlaceBuilding{key, x, y, Direction()};
    }
    case Kind::PlaceScenery: {
        Tile(view, x, y);
        const std::string key = m_rng.Chance(88) ? Key(view.sceneryKeys) : Key(view.surfaceKeys);
        return command::PlaceScenery{key, x, y, Direction()};
    }
    case Kind::PaintSurface: {
        const std::string key = m_rng.Chance(90) ? Key(view.surfaceKeys) : Key(view.sceneryKeys);
        Tile(view, x, y);
        return command::PaintSurface{key, x, y};
    }
    case Kind::Dozer: Tile(view, x, y); return command::Dozer{x, y};
    case Kind::Demolish: return command::Demolish{AnyBuilding(view)};
    case Kind::HireStaff: {
        std::string profession =
                m_rng.Chance(92) ? std::string(bot::kProfessions[m_rng.Below(static_cast<uint32_t>(bot::kProfessions.size()))]) : "Janitor";
        return command::HireStaff{std::move(profession)};
    }
    case Kind::DismissStaff: return command::DismissStaff{AnyStaff(view)};
    case Kind::StaffWage: {
        const uint32_t staff = AnyStaff(view);
        const int32_t wage = m_rng.Chance(65) ? m_rng.Between(0, 60) : kOddInts[m_rng.Below(static_cast<uint32_t>(kOddInts.size()))];
        if (m_rng.Chance(70)) return command::SetStaffWage{staff, wage};
        return command::CopyStaffWage{staff, wage};
    }
    case Kind::StaffShift: {
        constexpr std::array<int32_t, 3> kBadHours = {-1, 24, 99};
        auto hour = [&] {
            return m_rng.Chance(90) ? m_rng.Between(0, 23) : kBadHours[m_rng.Below(static_cast<uint32_t>(kBadHours.size()))];
        };
        const uint32_t staff = AnyStaff(view);
        const int32_t start = hour();
        return command::SetStaffShift{staff, start, hour()};
    }
    case Kind::SendStaff: {
        const uint32_t staff = AnyStaff(view);
        if (m_rng.Chance(50)) return command::SendStaffTo{staff, AnyObject(view)};
        // World coordinates: about 64 units per tile across the map, a few points off it.
        const int32_t extent = std::max(view.mapWidth, view.mapHeight) * 64;
        return command::SendStaff{
                staff, static_cast<float>(m_rng.Between(-200, extent)), static_cast<float>(m_rng.Between(-200, extent)), 0.0f};
    }
    case Kind::BuildingOpen: return command::SetBuildingOpen{AnyBuilding(view), m_rng.Chance(50)};
    case Kind::CallService:
        if (m_rng.Chance(50)) return command::CallRepair{AnyBuilding(view)};
        return command::CallCleaner{AnyBuilding(view)};
    case Kind::Price: {
        const uint32_t building = AnyBuilding(view);
        const int32_t slot = m_rng.Chance(85) ? m_rng.Between(0, 3) : m_rng.Between(-2, 50);
        if (m_rng.Chance(75)) return command::SetPrice{building, slot, Price()};
        return command::SetEventPrice{building, slot, Price()};
    }
    case Kind::BeerOrShow: {
        const uint32_t building = AnyBuilding(view);
        if (m_rng.Chance(50)) return command::SetBeer{building, Key(view.beerKeys)};
        return command::SetShow{building, m_rng.Between(-1, 3), Key(view.showKeys)};
    }
    case Kind::ObjectControl: {
        const uint32_t object = m_rng.Chance(50) ? AnyBuilding(view) : AnyStaff(view);
        const int32_t word = m_rng.Chance(70) ? m_rng.Between(0, 3) : kOddInts[m_rng.Below(static_cast<uint32_t>(kOddInts.size()))];
        const float value = m_rng.Chance(70) ? static_cast<float>(m_rng.Between(0, 4)) * 0.25f
                                             : kOddPrices[m_rng.Below(static_cast<uint32_t>(kOddPrices.size()))];
        switch (m_rng.Below(13)) {
        case 0: return command::SetRepairInterval{object, word};
        case 1: return command::SetBeerStrength{object, value};
        case 2: return command::SetBeerPrice{object, value};
        case 3: return command::SetBuildingOpenPolicy{object, word};
        case 4: return command::SetBeachOpenPolicy{object, word};
        case 5: return command::SetCleanerToilets{object, word};
        case 6: return command::SetCleanerBins{object, word};
        case 7: return command::SetCleanerLitter{object, word};
        case 8: return command::SetMechanicRepair{object, word};
        case 9: return command::SetMechanicVandals{object, word};
        case 10: return command::SetMechanicWater{object, word};
        case 11: return command::SetRepGuestTime{object, value};
        default: return command::SetSecurityHarshness{object, value};
        }
    }
    case Kind::Pause: return command::SetPaused{!view.paused};
    case Kind::Speed: return command::SetGameSpeed{m_rng.Chance(90) ? m_rng.Between(0, 2) : (m_rng.Chance(50) ? 3 : -1)};
    case Kind::BreakObject:
    case Kind::Count: break;
    }
    return command::BreakObject{AnyObject(view)};
}

void ChaosBot::Think(const bot::WorldView& view, const bot::Queries& queries, std::vector<command::Command>& out)
{
    for (const bot::Building& building : view.buildings) m_seen.insert(building.id);
    for (const auto& ids : view.staffIds) m_seen.insert(ids.begin(), ids.end());
    // Pauses stay short (a paused world only counts ticks), sometimes through the pause box's resume, which also clears a forced pause.
    if (view.paused && m_rng.Chance(60)) {
        if (m_rng.Chance(30))
            out.push_back(command::ResumeGame{});
        else
            out.push_back(command::SetPaused{false});
    }
    const uint32_t count = 1 + (m_rng.Chance(25) ? m_rng.Below(3) : 0);
    for (uint32_t i = 0; i < count; ++i) {
        Kind kind;
        if (m_burstLeft > 0) {
            --m_burstLeft;
            kind = m_burstKind;
        } else {
            kind = DrawKind();
            // Bursts: hiring, demolishing or pricing several times in a row, as a player clicking fast.
            if (m_rng.Chance(3)) {
                m_burstKind = kind;
                m_burstLeft = static_cast<uint32_t>(m_rng.Between(3, 8));
            }
        }
        out.push_back(Draw(kind, view, queries));
    }
}

namespace {

const bot::Building* FindBuilding(const bot::WorldView& view, uint32_t id)
{
    for (const auto& building : view.buildings)
        if (building.id == id) return &building;
    return nullptr;
}

std::string Hour(float hour)
{
    char text[16];
    std::snprintf(text, sizeof text, "%.2f", hour);
    return text;
}

bool IsAccommodation(const bot::Building& building)
{
    return building.key == "hotel" || building.key == "accomblk";
}

} // namespace

bool IsTrickyFamily(std::string_view family)
{
    return family == "scenery-sale" || family == "accommodation-asleep" || family == "accommodation-away" || family == "construction";
}

std::unique_ptr<TrickyProbe> MakeTrickyProbe(std::string_view family)
{
    if (family == "scenery-sale") return std::make_unique<TrickyBot>();
    if (family == "accommodation-asleep") return std::make_unique<TrickyAccommodation>(false);
    if (family == "accommodation-away") return std::make_unique<TrickyAccommodation>(true);
    if (family == "construction") return std::make_unique<TrickyConstruction>();
    return nullptr;
}

void TrickyAccommodation::Observe(const bot::WorldView& view, const char* stage)
{
    std::string line = std::string("tricky-accommodation stage=") + stage + " day=" + std::to_string(view.day) +
            " hour=" + Hour(view.hour) + " guests=" + std::to_string(view.guests);
    for (const auto& building : view.buildings) {
        if (!IsAccommodation(building)) continue;
        const char* state = building.underConstruction ? "building"
                : building.beingDemolished             ? "demolishing"
                : building.closed                      ? "closed"
                                                       : "open";
        line += " " + building.key + "#" + std::to_string(building.id) + "=" + state + "/" + std::to_string(building.guests);
    }
    observations.push_back(line);
}

void TrickyAccommodation::Think(const bot::WorldView& view, const bot::Queries&, std::vector<command::Command>& out)
{
    // Guests are out from the morning to the evening and in bed from about midnight (their m_BedTime/m_WakeTime); every step is
    // observed again an hour later, when the guests have reacted.
    const int32_t day = m_day;
    if (m_phase == 0) {
        m_day = view.day;
        for (const auto& building : view.buildings) {
            if (building.key == "hotel" && !m_hotel) m_hotel = building.id;
            if (building.key == "accomblk") m_blocks = true;
        }
        if (!m_hotel || !m_blocks) {
            observations.push_back("tricky-accommodation skipped=no-hotel-and-accommodation-block");
            m_phase = 99;
            return;
        }
        out.push_back(command::SetGameSpeed{2});
        Observe(view, "start");
        m_phase = 1;
        return;
    }
    const auto step = [&](int32_t stepDay, float hour, const char* stage, auto&& act) {
        if (!At(view, stepDay, hour)) return;
        act();
        Observe(view, stage);
        ++m_phase;
    };
    // The block with the most residents now: the residents are the trap.
    const auto fullest = [&]() -> std::optional<uint32_t> {
        const bot::Building* best = nullptr;
        for (const auto& building : view.buildings)
            if (building.key == "accomblk" && !building.underConstruction && !building.beingDemolished &&
                    (!best || building.guests > best->guests))
                best = &building;
        return best ? std::optional<uint32_t>(best->id) : std::nullopt;
    };
    const auto demolish = [&](std::optional<uint32_t> block, int requests) {
        if (!block) observations.push_back("tricky-accommodation no-block-left");
        for (int i = 0; block && i < requests; ++i) out.push_back(command::Demolish{*block});
    };
    switch (m_phase) {
    // A repeated request in the same tick, as in the scenery-sale probe.
    case 1:
        if (m_awayFirst)
            step(day, 10.0f, "block-demolished-away", [&] { demolish(fullest(), 2); });
        else
            step(day + 1, 1.0f, "block-demolished-asleep", [&] { demolish(fullest(), 2); });
        break;
    case 2: step(m_awayFirst ? day : day + 1, m_awayFirst ? 11.0f : 2.0f, "+1h", [] {}); break;
    case 3:
        if (m_awayFirst)
            step(day + 1, 1.0f, "block-demolished-asleep", [&] { demolish(fullest(), 1); });
        else
            step(day + 1, 11.0f, "block-demolished-away", [&] { demolish(fullest(), 1); });
        break;
    case 4: step(day + 1, m_awayFirst ? 2.0f : 12.0f, "+1h", [] {}); break;
    case 5: step(day + 2, 1.0f, "hotel-closed-asleep", [&] { out.push_back(command::SetBuildingOpen{*m_hotel, false}); }); break;
    case 6: step(day + 2, 2.0f, "hotel-closed-asleep+1h", [] {}); break;
    case 7: step(day + 2, 6.0f, "hotel-reopened-morning", [&] { out.push_back(command::SetBuildingOpen{*m_hotel, true}); }); break;
    case 8: step(day + 2, 10.0f, "hotel-closed-away", [&] { out.push_back(command::SetBuildingOpen{*m_hotel, false}); }); break;
    case 9: step(day + 2, 11.0f, "hotel-closed-away+1h", [] {}); break;
    case 10: step(day + 2, 14.0f, "hotel-reopened", [&] { out.push_back(command::SetBuildingOpen{*m_hotel, true}); }); break;
    case 11: step(day + 3, 8.0f, "next-morning", [] {}); break;
    default: break;
    }
}

bool TrickyConstruction::Place(const bot::WorldView& view, const bot::Queries& queries, const bot::BuildingType& type, int32_t direction,
        std::vector<command::Command>& out, int32_t trap)
{
    for (int32_t y = 0; y < view.mapHeight; ++y)
        for (int32_t x = 0; x < view.mapWidth; ++x)
            if (queries.BuildingFits(type.key, x, y, direction)) {
                if (trap >= 0) {
                    // Rotations 4-7 would trap buildings in construction; the place mode refuses them.
                    command::PlaceBuilding rotated{type.key, x, y, trap};
                    out.push_back(rotated);
                    m_placed.push_back({rotated, view.tick});
                }
                command::PlaceBuilding place{type.key, x, y, direction};
                out.push_back(place);
                m_placed.push_back({place, view.tick});
                return true;
            }
    return false;
}

void TrickyConstruction::Match(const bot::WorldView& view)
{
    for (const auto& building : view.buildings) {
        if (!m_known.insert(building.id).second) continue;
        // The first placement of the key from the newest think: an earlier rejected one stays without a building, and of two in one
        // tick the first is accepted.
        Placed* match = nullptr;
        for (Placed& placed : m_placed)
            if (placed.id == 0 && placed.place.key == building.key && placed.place.direction <= 3 && (!match || placed.tick > match->tick))
                match = &placed;
        if (match) match->id = building.id;
    }
}

void TrickyConstruction::Think(const bot::WorldView& view, const bot::Queries& queries, std::vector<command::Command>& out)
{
    const auto report = [&](const char* stage, const Placed& placed) {
        observations.push_back(std::string("tricky-construction stage=") + stage + " tick=" + std::to_string(view.tick) + " " +
                command::Format(command::Command{placed.place}) + " id=" + std::to_string(placed.id));
    };
    if (m_phase == 0) {
        for (const auto& building : view.buildings) m_known.insert(building.id);
        // One affordable unlocked type per direction 0-3, each a different type so MAXINSTANCES does not interfere, each first tried
        // at the same origin with direction + 4.
        float cash = view.cash;
        size_t next = 0;
        for (int32_t direction = 0; direction <= 3; ++direction)
            for (; next < view.buildingTypes.size(); ++next) {
                const bot::BuildingType& type = view.buildingTypes[next];
                if (!type.allowed || type.lockedByTree || static_cast<float>(type.cost) > cash || type.maxInstances < 2) continue;
                if (!Place(view, queries, type, direction, out, direction + 4)) continue;
                cash -= static_cast<float>(type.cost);
                ++next;
                break;
            }
        if (m_placed.empty()) {
            observations.push_back("tricky-construction skipped=no-fitting-building");
            m_phase = 9;
            return;
        }
        // The same footprint twice in one tick.
        out.push_back(m_placed[1].place);
        m_placed.push_back({m_placed[1].place, view.tick});
        // A type whose predecessor is one of these placements: locked until the predecessor finishes.
        for (const auto& type : view.buildingTypes)
            for (const Placed& placed : m_placed)
                if (m_locked.empty() && type.tree == placed.place.key && type.lockedByTree) m_locked = type.key;
        out.push_back(command::SetGameSpeed{2});
        m_placedAt = view.tick;
        m_phase = 1;
    } else if (m_phase == 1) {
        Match(view);
        for (const Placed& placed : m_placed) report(placed.id ? "placed" : "rejected", placed);
        // The hints: "a placement under construction does not unlock the next", so the locked type must stay refused.
        if (const bot::BuildingType* type = m_locked.empty() ? nullptr : view.FindType(m_locked); type && type->lockedByTree) {
            const size_t before = m_placed.size();
            for (int32_t direction = 0; direction < 4 && m_placed.size() == before; ++direction)
                Place(view, queries, *type, direction, out);
            if (m_placed.size() == before) observations.push_back("tricky-construction locked=" + m_locked + " no-fitting-tile");
        }
        // The last accepted placement is demolished during construction; its footprint is placed again on the next think.
        m_phase = 3;
        for (auto it = m_placed.rbegin(); it != m_placed.rend(); ++it)
            if (it->id) {
                out.push_back(command::Demolish{it->id});
                report("demolish-under-construction", *it);
                m_again = it->place;
                m_phase = 2;
                break;
            }
    } else if (m_phase == 2) {
        Match(view);
        out.push_back(m_again);
        m_placed.push_back({m_again, view.tick});
        m_phase = 3;
    } else if (m_phase == 3) {
        Match(view);
        for (const Placed& placed : m_placed) report(placed.id ? "placed" : "rejected", placed);
        m_phase = 4;
    } else if (m_phase == 4) {
        Match(view);
        bool pending = false;
        for (Placed& placed : m_placed) {
            const bot::Building* building = placed.id ? FindBuilding(view, placed.id) : nullptr;
            if (!building || placed.finished) continue;
            if (!building->underConstruction && !building->beingDemolished) {
                placed.finished = true;
                report("finished", placed);
            } else if (building->underConstruction)
                pending = true;
        }
        if (!pending) {
            observations.push_back("tricky-construction stage=all-finished tick=" + std::to_string(view.tick));
            m_phase = 5;
        } else if (view.tick >= m_placedAt + kDeadline) {
            for (const Placed& placed : m_placed) {
                const bot::Building* building = placed.id ? FindBuilding(view, placed.id) : nullptr;
                if (building && building->underConstruction) report("stuck", placed);
            }
            m_phase = 5;
        }
    } else if (m_phase == 5) {
        // The finished predecessor unlocks the type: the same placement is accepted now.
        const bot::BuildingType* type = m_locked.empty() ? nullptr : view.FindType(m_locked);
        if (type)
            observations.push_back("tricky-construction unlock " + m_locked + " tree=" + type->tree +
                    " allowed=" + std::to_string(type->allowed) + " locked=" + std::to_string(type->lockedByTree) +
                    " cost=" + std::to_string(type->cost) + " cash=" + std::to_string(view.cash));
        m_phase = type && Place(view, queries, *type, 0, out) ? 6 : 7;
    } else if (m_phase == 6) {
        Match(view);
        report(m_placed.back().id ? "unlocked-placed" : "unlocked-rejected", m_placed.back());
        m_phase = 7;
    }
}

std::optional<ChaosRun> ReadChaosRun(std::string_view text, std::string* error)
{
    int lineNumber = 0;
    auto fail = [&](const std::string& why) -> std::optional<ChaosRun> {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return std::nullopt;
    };
    ChaosRun run;
    bool magic = false;
    std::set<std::string, std::less<>> seen;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t newline = text.find('\n', pos);
        const size_t stop = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = Trim(text.substr(pos, stop - pos));
        pos = stop + 1;
        ++lineNumber;
        if (line.empty() || line.front() == '#') continue;
        const auto [word, rest] = SplitWord(line);
        if (!magic) {
            int32_t version = 0;
            if (word != "OBL-chaos" || !ParseNumber(rest, version)) return fail("expected 'OBL-chaos <version>'");
            if (version != 1) return fail("unsupported chaos format version " + std::string(rest));
            magic = true;
            continue;
        }
        if (!seen.insert(std::string(word)).second) return fail("duplicate '" + std::string(word) + "'");
        if (word == "start") {
            const auto [kind, name] = SplitWord(rest);
            if (kind != "scenario" || !command::ParseStartScenario(name)) return fail("expected 'start scenario s<n>|sandbox<n>'");
            run.start = name;
        } else if (word == "seed") {
            if (!ParseNumber(rest, run.seed)) return fail("bad seed '" + std::string(rest) + "'");
        } else if (word == "fixed-dt") {
            const auto dt = command::ParseFloat(rest);
            if (!dt || !(*dt > 0.0f)) return fail("fixed-dt needs a positive number");
            run.fixedDt = *dt;
        } else if (word == "max-ticks") {
            if (!ParseNumber(rest, run.maxTicks) || run.maxTicks == 0) return fail("max-ticks needs a positive count");
        } else if (word == "save-at") {
            uint32_t tick = 0;
            if (!ParseNumber(rest, tick)) return fail("save-at needs a tick");
            run.saveAt = tick;
        } else if (word == "checkpoint-every") {
            if (!ParseNumber(rest, run.checkpointEvery) || run.checkpointEvery == 0) return fail("checkpoint-every needs a positive count");
        } else if (word == "check-rejected") {
            if (!ParseNumber(rest, run.checkRejected)) return fail("check-rejected needs a count");
        } else if (word == "mode") {
            const auto [mode, family] = SplitWord(rest);
            if (mode == "random" && family.empty())
                run.tricky.clear();
            else if (mode == "tricky" && (family.empty() || IsTrickyFamily(family)))
                run.tricky = family.empty() ? "scenery-sale" : std::string(family);
            else
                return fail("mode needs random or tricky [scenery-sale|accommodation-asleep|accommodation-away|construction]");
        } else if (word == "commands") {
            if (rest.empty()) return fail("commands needs a replay file");
            run.commands = rest;
        } else {
            return fail("unrecognized key '" + std::string(word) + "'");
        }
    }
    if (!magic || run.start.empty() || run.maxTicks == 0) {
        if (error) *error = !magic ? "empty chaos run file" : "start and max-ticks are required";
        return std::nullopt;
    }
    if (run.saveAt && *run.saveAt >= run.maxTicks) {
        if (error) *error = "save-at must be below max-ticks";
        return std::nullopt;
    }
    if (!run.tricky.empty() && !run.commands.empty()) return fail("tricky mode cannot use commands");
    return run;
}

uint32_t RandStateAfter(uint32_t seed, uint64_t calls)
{
    // state' = a * state + c (mod 2^32), composed by squaring: (a1, c1) then (a2, c2) is (a2 * a1, a2 * c1 + c2).
    uint32_t multiplier = 214013u, increment = 2531011u;
    uint32_t state = seed;
    for (; calls != 0; calls >>= 1) {
        if (calls & 1) state = multiplier * state + increment;
        increment = multiplier * increment + increment;
        multiplier *= multiplier;
    }
    return state;
}

} // namespace obl::chaos
