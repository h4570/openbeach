#include "bot_build_tree.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace obl::bot {
namespace {

constexpr int32_t kFastestSpeed = 2;
// A placement shows up in the view within a few ticks (the place command posts a message); until then the type counts as placed, and a
// rejected one is tried again after it.
constexpr uint32_t kPendingTicks = 100;

class BuildTreeBot final : public Bot {
public:
    void Think(const WorldView& view, const Queries& queries, std::vector<command::Command>& out) override
    {
        if (view.speedLevel != kFastestSpeed && !m_speedSent) {
            out.push_back(command::SetGameSpeed{kFastestSpeed});
            m_speedSent = true;
        }
        // One placement per think: the next fit check then sees it in the live grid, so the bot needs no footprint geometry of its own.
        for (const BuildingType& type : view.buildingTypes) {
            if (type.total > 0 || !type.allowed || type.lockedByTree) continue;
            const auto pending = m_placedAt.find(type.key);
            if (pending != m_placedAt.end() && view.tick - pending->second < kPendingTicks) continue;
            // The place mode needs cash left over after the price.
            if (view.cash <= static_cast<float>(type.cost)) continue;
            const auto spot = FindSpot(view, queries, type);
            if (!spot) continue;
            out.push_back(command::PlaceBuilding{type.key, spot->first, spot->second, 0});
            m_placedAt[type.key] = view.tick;
            return;
        }
    }

private:
    // The anchor is the mean tile of the buildings standing (the resort's centre), the map's centre without any.
    static std::pair<int32_t, int32_t> Anchor(const WorldView& view)
    {
        if (view.buildings.empty()) return {view.mapWidth / 2, view.mapHeight / 2};
        int64_t x = 0, y = 0;
        for (const Building& building : view.buildings) x += building.tileX, y += building.tileY;
        const auto count = static_cast<int64_t>(view.buildings.size());
        return {static_cast<int32_t>(x / count), static_cast<int32_t>(y / count)};
    }

    // Square rings of growing distance around the anchor, each ring row by row: the first origin where the footprint fits.
    static std::optional<std::pair<int32_t, int32_t>> FindSpot(const WorldView& view, const Queries& queries, const BuildingType& type)
    {
        const auto [ax, ay] = Anchor(view);
        const int32_t limit = std::max(view.mapWidth, view.mapHeight);
        for (int32_t d = 0; d < limit; ++d) {
            for (int32_t y = ay - d; y <= ay + d; ++y) {
                // Inner rows of the ring hold only its two side tiles.
                const int32_t step = y == ay - d || y == ay + d ? 1 : std::max(1, 2 * d);
                for (int32_t x = ax - d; x <= ax + d; x += step) {
                    if (x < 0 || y < 0 || x >= view.mapWidth || y >= view.mapHeight) continue;
                    if (queries.BuildingFits(type.key, x, y, 0)) return std::pair{x, y};
                }
            }
        }
        return std::nullopt;
    }

    bool m_speedSent = false;
    std::map<std::string, uint32_t> m_placedAt;
};

} // namespace

std::unique_ptr<Bot> MakeBuildTreeBot(const BotPlan&)
{
    return std::make_unique<BuildTreeBot>();
}

} // namespace obl::bot
