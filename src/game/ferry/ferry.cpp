#include "game/ferry/ferry.h"
#include "game/ferry/ferry_lifecycle.h"
#include "game/world/world_queries.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"
#include "game/world/world_params.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "core/call_counter.h"
#include <cstring>
#include <limits>
#include <vector>

namespace obl::ferry {
namespace {

using ferry::Arrival;
using ferry::Boat;
using ferry::Manager;

struct GameEnv {
    float Hour() const { return game::GetWorld()->m_gameDayTime; }
    uint32_t Day() const { return game::GetWorld()->m_gameDay; }
    int GuestCount() const { return game::GetWorldParams()->m_numGuests; }
    int Gender(int type) const { return game::GetWorldParams()->m_guests[type].m_Gender; }
    int GuestId(const char* key) const { return world::world_params::GetGuestId(game::GetWorldParams(), key); }
    int Rating() const { return game::GetWorld()->ResortRating(); }
    int Rand() const { return game::Rand(); }
    void Intensities(float& male, float& female) const
    {
        const auto buildings = object::object_manager::GetListByClass(game::GetObjectManager(), 2);
        const auto count = static_cast<uint32_t>(buildings.size());
        for (uint32_t i = 0; i < count; ++i) {
            auto* b = static_cast<object::Building*>(buildings[i]);
            male = static_cast<float>(male + object::building_api::GetAverageMaleIntensity(b));
            female = static_cast<float>(female + object::building_api::GetAverageFemaleIntensity(b));
        }
    }
};

enum Method { Add, Pop, Reset, Next, Today, Leaving, Methods };
test::CallCounter g_calls[] = {test::CallCounter("ferry.manager-add-boat-event"), test::CallCounter("ferry.manager-pop-due-boat"),
        test::CallCounter("ferry.manager-reset-cursor"), test::CallCounter("ferry.manager-get-next-boat-time"),
        test::CallCounter("ferry.manager-get-boat-time-today"), test::CallCounter("ferry.manager-record-guest-leaving")};

} // namespace

namespace ferry_facade {

void AddBoatEvent(Manager* manager, const Boat* input)
{
    GameEnv env;
    g_calls[Add].Count();
    ferry::AddBoatEvent(*manager, env, *input);
}
bool PopDueBoat(Manager* manager, Arrival* out)
{
    GameEnv env;
    g_calls[Pop].Count();
    return ferry::PopDueBoat(*manager, env, *out);
}
void ResetCursor(Manager* manager)
{
    g_calls[Reset].Count();
    ferry::ResetCursor(*manager);
}
double GetNextBoatTime(Manager* manager, int today, uint32_t* day)
{
    GameEnv env;
    g_calls[Next].Count();
    return ferry::GetNextBoatTime(*manager, env.Day(), env.Hour(), today, *day);
}
double GetBoatTimeToday(Manager* manager, int index)
{
    GameEnv env;
    g_calls[Today].Count();
    return ferry::GetBoatTimeToday(*manager, env.Day(), index);
}
void RecordGuestLeaving(Manager* manager, object::Guest* guest)
{
    g_calls[Leaving].Count();
    ferry::RecordGuestLeaving(*manager, guest->m_definition->m_id, guest->m_happiness, guest->m_leavingEarly != 0);
}

} // namespace ferry_facade

namespace {

// Synthetic queues through every method (`call-drive ferry.manager-pop-due-boat N`): ordered, performance and NaN-timed
// boats, all hours of six days and leavers of every guest type.
void Drive(int rounds)
{
    if (!game::GetWorld() || !game::GetWorldParams()) {
        OBL_LOG("autotest: FAILED ferry driver needs gameplay");
        return;
    }
    GameEnv env;
    auto& world = *game::GetWorld();
    auto oldHour = world.m_gameDayTime;
    auto oldDay = world.m_gameDay;
    for (int round = 0; round < rounds; ++round) {
        Manager m;
        uint32_t day = 0xabcdef;
        ferry_facade::GetNextBoatTime(&m, 0, &day);
        world.m_gameDay = round % 4;
        world.m_gameDayTime = 10;
        for (int i = 0; i < 7; ++i) {
            Boat boat{};
            boat.GuestInfo = (round + i) % 4;
            boat.NumGuests = 3 + i;
            boat.Time = static_cast<float>((i * 7) % 24);
            boat.Day = (round + i) % 5;
            boat.TimeFlags = i % 3;
            if (boat.GuestInfo == 2) {
                for (int t = 0; t < env.GuestCount(); ++t) {
                    std::strcpy(boat.GuestKeyword[t], game::GetWorldParams()->m_guests[t].m_key.c_str());
                    boat.GuestType[t] = t % 2;
                    boat.GuestKeywordNumbers[t] = 3 + t;
                }
            }
            ferry_facade::AddBoatEvent(&m, &boat);
        }
        Boat performanceBoat{};
        performanceBoat.TimeFlags = 3;
        performanceBoat.GuestInfo = round % 4;
        performanceBoat.NumGuests = 10;
        ferry_facade::AddBoatEvent(&m, &performanceBoat);
        Boat unorderedBoat{};
        unorderedBoat.TimeFlags = 1;
        unorderedBoat.GuestInfo = 1;
        unorderedBoat.NumGuests = 3;
        unorderedBoat.Time = std::numeric_limits<float>::quiet_NaN();
        ferry_facade::AddBoatEvent(&m, &unorderedBoat);
        world.m_gameDayTime = std::numeric_limits<float>::quiet_NaN();
        Arrival unorderedArrival{};
        ferry_facade::PopDueBoat(&m, &unorderedArrival);
        for (int d = 0; d < 6; ++d) {
            world.m_gameDay = d;
            ferry_facade::ResetCursor(&m);
            for (int h = 0; h < 24; ++h) {
                world.m_gameDayTime = static_cast<float>(h);
                for (int today : {0, 1, 256}) ferry_facade::GetNextBoatTime(&m, today, &day);
                for (int index = -1; index < 9; ++index) ferry_facade::GetBoatTimeToday(&m, index);
                Arrival arrival;
                std::memset(&arrival, 0xa5, sizeof(arrival));
                ferry_facade::PopDueBoat(&m, &arrival);
            }
        }
        // Leavers of every guest type, happy and early, with values that exercise the halving (a negative, a tiny and a large happiness).
        std::vector<uint8_t> memory(sizeof(object::Guest), 0);
        auto* guest = reinterpret_cast<object::Guest*>(memory.data());
        static const float kHappiness[] = {0.0f, 37.5f, -12.25f, 1e-30f, 1e9f, 100.0f};
        for (int i = 0; i < 40; ++i) {
            guest->m_definition = &game::GetWorldParams()->m_guests[i % env.GuestCount()];
            guest->m_happiness = kHappiness[(i + round) % 6];
            guest->m_leavingEarly = (i / 3) % 2;
            ferry_facade::RecordGuestLeaving(&m, guest);
        }
    }
    world.m_gameDayTime = oldHour;
    world.m_gameDay = oldDay;
}

test::CallCounter g_createCalls("ferry.manager-construct"), g_destroyCalls("ferry.manager-destruct");
} // namespace

Manager* ferry_facade::Create()
{
    g_createCalls.Count();
    return new Manager();
}

void ferry_facade::Destroy(Manager* manager)
{
    g_destroyCalls.Count();
    delete manager;
}

bool RegisterFerryTestDriver()
{
    g_calls[Pop].SetDriver(&Drive);
    return true;
}
} // namespace obl::ferry
