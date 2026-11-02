#include "game/world/world_state.h"
#include "unit_test.h"

#include "importer/src/readers/building_behavior_keys.h"
#include "game/objects/building_lifecycle.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace obl;

namespace {

// A building with a sprite (one animation per name) and an Env that records the calls in order.
struct Site {
    std::unique_ptr<tables::BuildingDef> def = std::make_unique<tables::BuildingDef>();
    std::unique_ptr<object::Building> building = std::make_unique<object::Building>();
    std::vector<std::string> calls;
    std::vector<messages::Message> posted;
    LifecycleWorld world;

    // The building's own sprite: animations "shadow" (0), "idle" (1), "lightmap" (2); the parts have their own.
    std::vector<std::unique_ptr<ui::SpriteAnim>> anims;
    std::vector<ui::SpriteAnim*> animList;
    std::vector<std::string> names;
    ui::Sprite ownSprite{};
    ui::SpriteInstance own{};
    std::vector<std::unique_ptr<ui::SpriteInstance>> parts;
    std::vector<std::unique_ptr<ui::Sprite>> partSprites;
    std::vector<std::vector<ui::SpriteAnim*>> partAnimLists;
    std::vector<std::unique_ptr<ui::SpriteAnim>> partAnims;

    // answers
    int usableBuilders = 1;
    int keepers = 1;
    int area = 4;
    bool areaOpen = true;
    float ballX = 11.0f, ballY = 22.0f;
    LoungerTile hutTile{60, 45};
    uint32_t hutFlags = 2;
    int hutRetries = 0;
    int hutValidAt = 0;
    int hutDraws = 0;
    std::vector<LoungerTile> loungers;
    std::vector<int> neighbours;
    std::vector<LoungerTile> flagReads;
    int queueMarker = 0;
    int eventsMarker = 0;

    // The engine dispatches on the definition's behavior, and the building's name is the same key (Prop's constructor).
    void SetKey(const char* key)
    {
        def->m_key = key;
        def->m_Behavior = obl::BehaviorFromKey(key);
        std::strcpy(building->m_Name, key);
    }

    Site()
    {
        building->m_definition = def.get();
        building->m_id = 7;
        building->m_state = 1;
        SetKey("SOUVSHOP");
        names = {"shadow", "idle", "lightmap"};
        for (const std::string& name : names) {
            anims.push_back(std::make_unique<ui::SpriteAnim>());
            std::memset(anims.back().get(), 0, sizeof(ui::SpriteAnim));
            anims.back()->m_name = const_cast<char*>(name.c_str());
            anims.back()->m_directions = 8;
            animList.push_back(anims.back().get());
        }
        ownSprite.m_animCount = 3;
        ownSprite.m_anims = animList.data();
        own.m_sprite = &ownSprite;
        building->m_sprite = &own;
    }

    // A part (ANIMSPRITE) whose first animation has `directions0` directions and the playing one `directions`.
    void AddPart(int32_t directions0, int32_t directions, int32_t playing = 1)
    {
        partAnims.push_back(std::make_unique<ui::SpriteAnim>());
        partAnims.back()->m_directions = static_cast<uint8_t>(directions0);
        partAnims.push_back(std::make_unique<ui::SpriteAnim>());
        partAnims.back()->m_directions = static_cast<uint8_t>(directions);
        partAnimLists.push_back({partAnims[partAnims.size() - 2].get(), partAnims.back().get()});
        partSprites.push_back(std::make_unique<ui::Sprite>());
        partSprites.back()->m_animCount = 2;
        partSprites.back()->m_anims = partAnimLists.back().data();
        parts.push_back(std::make_unique<ui::SpriteInstance>());
        parts.back()->m_sprite = partSprites.back().get();
        parts.back()->m_animIndex = playing;
        building->m_animSprites[parts.size() - 1] = parts.back().get();
        def->m_NumAnimSprites = static_cast<int>(parts.size());
    }

    void Note(const char* call) { calls.push_back(call); }
    bool Called(const std::string& call) const
    {
        for (const std::string& c : calls)
            if (c == call) return true;
        return false;
    }
    size_t Index(const std::string& call) const
    {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i] == call) return i;
        return calls.size();
    }

    // OnEnterState
    LoungerTile BuildingTile() { return hutTile; }
    LoungerTile LoungerNeighbour(LoungerTile tile, int direction)
    {
        neighbours.push_back(direction);
        if (direction == 0) ++hutRetries;
        return {tile.x + static_cast<uint32_t>(direction + 1), tile.y};
    }
    uint32_t LoungerFlags(LoungerTile tile)
    {
        flagReads.push_back(tile);
        return hutRetries > hutValidAt ? hutFlags : 0;
    }
    void PlaceLounger(LoungerTile tile)
    {
        Note("lounger");
        loungers.push_back(tile);
    }
    void NotifyNoBuilders() { Note("no builders"); }
    int CountUsableBuilders()
    {
        Note("count builders");
        return usableBuilders;
    }
    void UpdateOpenState() { Note("open state"); }
    void StartBuild() { Note("start build"); }
    void StartDemolish() { Note("start demolish"); }
    void FinishDemolish() { Note("finish demolish"); }
    int CountSwimmingAreaKeepers(int32_t)
    {
        Note("count keepers");
        return keepers;
    }
    void OpenSwimmingArea(int32_t) { Note("open area"); }
    void PoolBallPosition(float& x, float& y)
    {
        Note("ball");
        x = ballX;
        y = ballY;
    }
    void BuildingSoundStart() { Note("sound start"); }
    // OnExitState
    int FindSwimmingArea()
    {
        Note("find area");
        return area;
    }
    bool IsSwimmingAreaOpen(int32_t)
    {
        Note("area open?");
        return areaOpen;
    }
    void SpawnInitialStaff() { Note("staff"); }
    void SpawnVehicles() { Note("vehicles"); }
    void SpawnTables() { Note("tables"); }
    void CloseDialog() { Note("dialog"); }
    void Reselect() { Note("reselect"); }
    void FinishBuild() { Note("finish build"); }
    void* CreateQueue()
    {
        Note("queue");
        return &queueMarker;
    }
    void HideStaff() { Note("hide"); }
    void ClearQueueForClosing() { Note("clear queue"); }
    void PostClosing() { Note("closing"); }
    object::BuildingEvents* CreateEvents()
    {
        Note("events");
        return reinterpret_cast<object::BuildingEvents*>(&eventsMarker);
    }
    void RequestInspection() { Note("inspection"); }
    // ReceiveMessage
    void Post(const messages::Message& message)
    {
        Note("post");
        posted.push_back(message);
    }
    void NotifyRepaired(bool noPower) { Note(noPower ? "repaired (no power)" : "repaired"); }
    // MT_STAFF_CREATED: the StaffDef ids that count as women, and the objects hidden
    std::vector<int32_t> femaleDefinitions;
    std::vector<int32_t> hidden;
    bool IsFemaleStaff(int32_t definition)
    {
        Note("staff def");
        for (int32_t id : femaleDefinitions)
            if (id == definition) return true;
        return false;
    }
    void HideServiceStaff(int32_t id)
    {
        Note("hide staff");
        hidden.push_back(id);
    }
    // MT_VEHICLE_CREATED, MT_GUEST_LEFT_VEHICLE, MT_WAIT_FOR_BOAT: what went to the vehicles, tables and the queue
    std::vector<int32_t> routeStops;
    std::vector<std::pair<int32_t, int32_t>> seated; // table, guest
    std::vector<std::pair<int32_t, bool>> enqueued; // guest, moveGuest
    std::vector<int32_t> enqueuedSlots;
    void AddRouteStop(int32_t vehicle)
    {
        Note("route stop");
        routeStops.push_back(vehicle);
    }
    int32_t acceptingTable = -1; // the table whose SeatGuest succeeds
    bool SeatGuest(int32_t table, int32_t guest)
    {
        Note("seat");
        seated.emplace_back(table, guest);
        return table == acceptingTable;
    }
    void Enqueue(int32_t guest, bool moveGuest, int32_t slot)
    {
        Note("enqueue");
        enqueued.emplace_back(guest, moveGuest);
        enqueuedSlots.push_back(slot);
    }
    // MT_LOOK_AROUND, MT_COMMOD_QUALITY, MT_GUEST_SERVED: the objects around, the keys of the goods, the queue's guests (front first) and the delayed sends
    std::vector<std::unique_ptr<object::Vehicle>> objects; // the biggest object type any test needs
    std::vector<object::Base*> nearby;
    int32_t nearRadius = -1;
    std::vector<std::pair<messages::Message, float>> delayedPosts;
    std::vector<std::string> commodityKeys; // indexed by the commodity id; the definition's slots hold the ids
    float eventPrice = 4.5f;
    std::vector<std::pair<uint32_t, int32_t>> queue; // guest, slot
    bool dequeueFails = false;
    int32_t boarded = -1;
    int32_t missingQueueSender = -1;
    object::Base* AddObject(int32_t id, object::ObjectType type)
    {
        objects.push_back(std::make_unique<object::Vehicle>());
        objects.back()->m_id = static_cast<uint32_t>(id);
        objects.back()->m_type = type;
        return objects.back().get();
    }
    uint32_t ObjectsNear(int32_t radius, object::Base* const*& list)
    {
        Note("near");
        nearRadius = radius;
        list = nearby.data();
        return static_cast<uint32_t>(nearby.size());
    }
    void PostDelayed(const messages::Message& message, float delay)
    {
        Note("post delayed");
        delayedPosts.emplace_back(message, delay);
    }
    const char* CommodityName(int32_t id)
    {
        Note("commodity name");
        return commodityKeys[static_cast<size_t>(id)].c_str();
    }
    float EventPrice()
    {
        Note("event price");
        return eventPrice;
    }
    void MissingQueue(int32_t sender)
    {
        Note("missing queue");
        missingQueueSender = sender;
    }
    uint32_t QueueSize()
    {
        Note("queue size");
        return static_cast<uint32_t>(queue.size());
    }
    bool Dequeue(uint32_t& guest, int32_t& slot)
    {
        Note("dequeue");
        if (dequeueFails || queue.empty()) return false;
        guest = queue.front().first;
        slot = queue.front().second;
        queue.erase(queue.begin());
        return true;
    }
    void BoardQueuedGuests(int32_t sender)
    {
        Note("board queued");
        boarded = sender;
    }
    object::Base* GetObject(int32_t id)
    {
        Note("get object");
        for (auto& object : objects)
            if (static_cast<int32_t>(object->m_id) == id) return object.get();
        return nullptr;
    }

    // MT_IM_READY: the world's books, the key tables, the water cleaners, the beer table and the game's random numbers
    std::unique_ptr<layout::World> gameWorld = std::make_unique<layout::World>();
    layout::Player player{};
    float eventCost = 0.75f;
    std::vector<int32_t> familyOfCommodity; // indexed by the commodity id
    std::vector<object::Base*> cleaners;
    std::vector<std::pair<int32_t, float>> foodIll; // guest, amount
    std::vector<int32_t> eventGuests;
    tables::BeerDef beer{};
    int32_t beerCounter = 0;
    uint32_t rating = 50;
    std::vector<int> rands; // what rand() gives, in order
    size_t randsUsed = 0;
    layout::World& GameWorld()
    {
        Note("world");
        gameWorld->m_player.reset(&player);
        return *gameWorld;
    }
    float EventCost()
    {
        Note("event cost");
        return eventCost;
    }
    int32_t FamilyOf(int32_t commodity)
    {
        Note("family of");
        return familyOfCommodity[static_cast<size_t>(commodity)];
    }
    int32_t FamilyId(const char* key)
    {
        Note("family id");
        const std::string name = key;
        return name == "MEALS" ? 1 : name == "SNACKS" ? 2 : name == "ALCOHOL" ? 3 : name == "SOFTDRNK" ? 4 : -1;
    }
    int32_t CommodityId(const char* key)
    {
        Note("commodity id");
        for (size_t i = 0; i < commodityKeys.size(); ++i)
            if (commodityKeys[i] == key) return static_cast<int32_t>(i);
        return -1;
    }
    object::Base* const* FindBuildings(BuildingBehavior behavior, uint32_t& count)
    {
        Note("find objects");
        OBL_CHECK(behavior == BuildingBehavior::WaterCleaner);
        count = static_cast<uint32_t>(cleaners.size());
        return cleaners.data();
    }
    void AddFoodIll(int32_t guest, float amount)
    {
        Note("food ill");
        foodIll.emplace_back(guest, amount);
    }
    void AddEventGuest(int32_t guest)
    {
        Note("event guest");
        eventGuests.push_back(guest);
    }
    const tables::BeerDef& Beer(int32_t)
    {
        Note("beer");
        return beer;
    }
    int32_t& BeerCounter() { return beerCounter; }
    uint32_t ResortRating()
    {
        Note("rating");
        return rating;
    }
    int Rand()
    {
        Note("rand");
        ++hutDraws;
        return rands.empty() ? 0 : rands[randsUsed++];
    }

    EnterReport Enter(uint32_t state) { return EnterState(*this, *building, state, world); }
    ExitReport Exit() { return ExitState(*this, *building, world); }
};

} // namespace

// ---- OnEnterState --------------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_lifecycle.find_anim_index")
{
    Site s;
    OBL_CHECK_EQ(FindAnimIndex(s.ownSprite, "shadow"), 0);
    OBL_CHECK_EQ(FindAnimIndex(s.ownSprite, "lightmap"), 2);
    OBL_CHECK_EQ(FindAnimIndex(s.ownSprite, "pick"), -1);
    OBL_CHECK_EQ(FindAnimIndex(s.ownSprite, "Shadow"), -1); // case-sensitive
}

OBL_TEST_CASE("building_lifecycle.state0_plain_building_starts_its_sound")
{
    Site s;
    s.building->m_soundPlaying = 0;
    const EnterReport report = s.Enter(0);
    OBL_CHECK(report.working);
    OBL_CHECK(!report.pool);
    OBL_CHECK(!report.beachHut);
    const std::vector<std::string> expected = {"sound start"};
    OBL_CHECK(s.calls == expected);
    OBL_CHECK_EQ(int{s.building->m_soundPlaying}, 1);
}

OBL_TEST_CASE("building_lifecycle.state0_beach_hut_posts_ten_loungers_before_sound")
{
    Site s;
    s.SetKey("BEACHHUT");
    s.building->m_soundPlaying = 0;
    const EnterReport report = s.Enter(0);
    OBL_CHECK(report.beachHut);
    OBL_CHECK(report.working);
    OBL_CHECK_EQ(s.loungers.size(), size_t{10});
    OBL_CHECK_EQ(s.hutDraws, 20);
    OBL_CHECK_EQ(s.neighbours.size(), size_t{80});
    OBL_CHECK_EQ(s.flagReads.size(), size_t{90});
    OBL_CHECK_EQ(s.loungers[0].x, 50u);
    OBL_CHECK_EQ(s.loungers[0].y, 35u);
    OBL_CHECK_EQ(s.calls.back(), std::string("sound start"));
    OBL_CHECK_EQ(int{s.building->m_soundPlaying}, 1);
}

OBL_TEST_CASE("building_lifecycle.lounger_offsets_cover_every_crt_draw")
{
    for (int draw = 0; draw <= 32767; ++draw)
        OBL_CHECK_EQ(LoungerOffset(draw), static_cast<int32_t>(uint64_t{32769} * draw * 20 / (uint64_t{1} << 30)) - 10);
    OBL_CHECK_EQ(LoungerOffset(32767), 9);
}

OBL_TEST_CASE("building_lifecycle.beach_hut_retries_and_discards_the_last_candidate")
{
    for (int validAt : {99, 100, 2000}) {
        Site s;
        s.SetKey("BEACHHUT");
        s.hutValidAt = validAt;
        s.Enter(0);
        OBL_CHECK_EQ(s.loungers.size(), validAt == 99 ? size_t{10} : validAt == 100 ? size_t{9} : size_t{0});
        OBL_CHECK_EQ(s.hutDraws, validAt == 99 ? 218 : validAt == 100 ? 220 : 2020);
        OBL_CHECK_EQ(s.calls.back(), std::string("sound start"));
    }
}

OBL_TEST_CASE("building_lifecycle.beach_hut_checks_all_neighbours_and_each_forbidden_bit")
{
    struct Blocked : Site {
        uint32_t blockedX = 0, blockedFlags = 0;
        uint32_t LoungerFlags(LoungerTile tile) { return tile.x == blockedX ? blockedFlags : 2; }
    };
    for (int cell = 0; cell < 9; ++cell) {
        for (uint32_t flags : {0u, 0x12u, 0x8002u}) {
            Blocked s;
            s.blockedX = 50 + cell;
            s.blockedFlags = flags;
            s.SetKey("BEACHHUT");
            EnterState(s, *s.building, 0, s.world);
            OBL_CHECK(s.loungers.empty());
            OBL_CHECK_EQ(s.hutDraws, 2020);
            OBL_CHECK_EQ(s.neighbours.size(), size_t{8080});
        }
    }
}

OBL_TEST_CASE("building_lifecycle.beach_hut_offsets_keep_unsigned_coordinates")
{
    Site s;
    s.hutTile = {0, 0};
    s.SetKey("BEACHHUT");
    s.Enter(0);
    OBL_CHECK_EQ(s.loungers[0].x, 0xfffffff6u);
    OBL_CHECK_EQ(s.loungers[0].y, 0xfffffff6u);
    const int order[] = {0, 2, 4, 6, 1, 3, 5, 7};
    for (int i = 0; i < 8; ++i) {
        OBL_CHECK_EQ(s.neighbours[i], i);
        OBL_CHECK_EQ(s.flagReads[i + 1].x, s.loungers[0].x + order[i] + 1);
    }
}

OBL_TEST_CASE("building_lifecycle.state0_pool_sets_the_ball_before_the_sound")
{
    Site s;
    s.SetKey("MAINPOOL");
    s.building->m_BallPosition[2] = 5.0f;
    const EnterReport report = s.Enter(0);
    OBL_CHECK(report.pool);
    OBL_CHECK_EQ(s.building->m_BallPosition[0], 11.0f);
    OBL_CHECK_EQ(s.building->m_BallPosition[1], 22.0f);
    OBL_CHECK_EQ(s.building->m_BallPosition[2], 0.0f);
    const std::vector<std::string> expected = {"ball", "sound start"};
    OBL_CHECK(s.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.state1_finds_the_animations_and_starts_the_build")
{
    Site s;
    s.usableBuilders = 3;
    s.world.gameTime = 12.5f;
    const EnterReport report = s.Enter(1);
    OBL_CHECK(report.startup);
    OBL_CHECK(!report.noBuilders);
    OBL_CHECK_EQ(s.building->m_shadowOffset, 0);
    OBL_CHECK_EQ(s.building->m_pickOffset, -1);
    OBL_CHECK_EQ(s.building->m_lightMapOffset, 2);
    OBL_CHECK_EQ(s.building->m_buildingAnimTime, 12.5f);
    const std::vector<std::string> expected = {"count builders", "start build"};
    OBL_CHECK(s.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.state1_without_builders_tells_the_player")
{
    Site s;
    s.usableBuilders = 0;
    const EnterReport report = s.Enter(1);
    OBL_CHECK(report.noBuilders);
    const std::vector<std::string> expected = {"count builders", "no builders", "start build"};
    OBL_CHECK(s.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.state1_in_a_world_being_built_opens_by_the_hours")
{
    Site s;
    s.world.startup = true;
    s.usableBuilders = 0; // not even asked
    const EnterReport report = s.Enter(1);
    OBL_CHECK(!report.noBuilders);
    const std::vector<std::string> expected = {"open state", "start build"};
    OBL_CHECK(s.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.state1_parts_follow_the_building_direction")
{
    Site s;
    s.own.m_direction = 5;
    s.AddPart(4, 4); // 5 % 4 = 1
    s.AddPart(8, 3); // 5 % 8 = 5, cut to 3 - 1 = 2
    s.AddPart(2, 8, 0); // playing the first animation (2 directions): 5 % 2 = 1 < 2
    s.AddPart(0, 8); // no direction at all: remainder 0
    s.Enter(1);
    OBL_CHECK_EQ(s.parts[0]->m_direction, 1);
    OBL_CHECK_EQ(s.parts[1]->m_direction, 2);
    OBL_CHECK_EQ(s.parts[2]->m_direction, 1);
    OBL_CHECK_EQ(s.parts[3]->m_direction, 0);
}

OBL_TEST_CASE("building_lifecycle.state1_negative_direction_keeps_the_sign_of_the_remainder")
{
    Site s;
    s.own.m_direction = -3;
    s.AddPart(4, 4);
    s.Enter(1);
    OBL_CHECK_EQ(s.parts[0]->m_direction, -3); // signed remainder: -3 % 4 = -3, which is below the limit
}

OBL_TEST_CASE("building_lifecycle.state2_starts_the_demolition")
{
    Site s;
    s.building->m_builtByUser = 1;
    const EnterReport report = s.Enter(2);
    OBL_CHECK(report.demolish);
    OBL_CHECK_EQ(int{s.building->m_builtByUser}, 0);
    const std::vector<std::string> expected = {"start demolish"};
    OBL_CHECK(s.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.state3_gives_back_a_swimming_area_only_when_it_is_the_last")
{
    Site s;
    s.building->m_InfluenceArea = 4;
    s.keepers = 2;
    EnterReport report = s.Enter(3);
    OBL_CHECK(report.demolished);
    OBL_CHECK(!report.areaOpened);
    std::vector<std::string> expected = {"finish demolish", "count keepers"};
    OBL_CHECK(s.calls == expected);

    Site last;
    last.building->m_InfluenceArea = 4;
    last.keepers = 1;
    report = last.Enter(3);
    OBL_CHECK(report.areaOpened);
    expected = {"finish demolish", "count keepers", "open area"};
    OBL_CHECK(last.calls == expected);

    Site none;
    none.building->m_InfluenceArea = -1;
    report = none.Enter(3);
    OBL_CHECK(!report.areaOpened);
    expected = {"finish demolish"};
    OBL_CHECK(none.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.other_states_do_nothing")
{
    for (uint32_t state : {4u, 5u, 99u, 0xffffffffu}) {
        Site s;
        s.Enter(state);
        OBL_CHECK(s.calls.empty());
    }
}

// ---- OnExitState ---------------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_lifecycle.exit_only_acts_when_leaving_the_building_site")
{
    for (int32_t state : {0, 2, 3, 4, -1}) {
        Site s;
        s.building->m_state = state;
        s.world.powerUse = 10.0f;
        const ExitReport report = s.Exit();
        OBL_CHECK(!report.startupDone);
        OBL_CHECK(s.calls.empty());
        OBL_CHECK(s.building->m_queue == nullptr);
    }
}

OBL_TEST_CASE("building_lifecycle.exit_brings_up_a_plain_building")
{
    Site s;
    s.building->m_builtByUser = 0;
    const ExitReport report = s.Exit();
    OBL_CHECK(report.startupDone);
    const std::vector<std::string> expected = {"staff", "vehicles", "tables", "queue"};
    OBL_CHECK(s.calls == expected);
    OBL_CHECK(s.building->m_queue == &s.queueMarker);
    OBL_CHECK_EQ(int{s.building->m_builtByUser}, 1);
    OBL_CHECK_EQ(int{s.building->m_closedForPower}, 0);
    OBL_CHECK(s.building->m_events == nullptr);
}

OBL_TEST_CASE("building_lifecycle.exit_reselects_a_selected_building_and_finishes_a_world_being_built")
{
    Site s;
    s.building->m_SelectLevel = 1;
    s.world.startup = true;
    s.building->m_builtByUser = 0;
    s.def->m_FailPercent = 40;
    const ExitReport report = s.Exit();
    OBL_CHECK(report.reselected);
    OBL_CHECK(report.finished);
    const std::vector<std::string> expected = {"staff", "vehicles", "tables", "dialog", "reselect", "finish build", "queue"};
    OBL_CHECK(s.calls == expected);
    OBL_CHECK_EQ(int{s.building->m_builtByUser}, 0); // not built by the player: no flag, no inspection
    OBL_CHECK(!report.inspection);
}

OBL_TEST_CASE("building_lifecycle.exit_a_selection_level_other_than_one_is_ignored")
{
    for (int level : {0, 2}) {
        Site s;
        s.building->m_SelectLevel = level;
        s.Exit();
        OBL_CHECK(!s.Called("dialog"));
    }
}

OBL_TEST_CASE("building_lifecycle.exit_player_built_building_with_failures_asks_for_an_inspection")
{
    Site s;
    s.def->m_FailPercent = 5;
    const ExitReport report = s.Exit();
    OBL_CHECK(report.inspection);
    OBL_CHECK_EQ(s.calls.back(), std::string("inspection"));
    s.def->m_FailPercent = 0;
    Site other;
    OBL_CHECK(!other.Exit().inspection);
}

OBL_TEST_CASE("building_lifecycle.exit_lifeguard_station_takes_its_area_and_shows_the_flag")
{
    Site s;
    s.SetKey("BAYWATCH");
    s.AddPart(8, 8, 0);
    s.building->m_InfluenceArea = -1;
    s.areaOpen = false;
    s.parts[0]->m_frameIndex = 9;
    ExitReport report = s.Exit();
    OBL_CHECK(report.swimmingArea);
    OBL_CHECK_EQ(s.building->m_InfluenceArea, 4);
    OBL_CHECK_EQ(s.parts[0]->m_animIndex, 1);
    OBL_CHECK_EQ(s.parts[0]->m_frameIndex, 0);
    OBL_CHECK_EQ(s.calls[0], std::string("find area"));
    OBL_CHECK_EQ(s.calls[1], std::string("area open?"));

    // An open area: animation 0; the frame is only reset when the animation changes.
    Site open;
    open.SetKey("BAYWATCH");
    open.AddPart(8, 8, 0);
    open.parts[0]->m_frameIndex = 9;
    open.Exit();
    OBL_CHECK_EQ(open.parts[0]->m_animIndex, 0);
    OBL_CHECK_EQ(open.parts[0]->m_frameIndex, 9);
}

OBL_TEST_CASE("building_lifecycle.exit_stage_gets_its_show_schedule")
{
    Site s;
    s.SetKey("STAGE");
    const ExitReport report = s.Exit();
    OBL_CHECK(report.stage);
    OBL_CHECK(s.building->m_events == reinterpret_cast<object::BuildingEvents*>(&s.eventsMarker));
    OBL_CHECK(s.Index("queue") < s.Index("events"));
}

OBL_TEST_CASE("building_lifecycle.exit_power_is_checked_after_the_queue_exists")
{
    Site s;
    s.def->m_Power = 5;
    s.world.powerUse = 20.0f;
    s.world.currentPower = 10.0f;
    s.world.maxPower = 100.0f;
    const ExitReport report = s.Exit();
    OBL_CHECK(report.shed);
    OBL_CHECK_EQ(int{s.building->m_closedForPower}, 1);
    OBL_CHECK_EQ(s.building->m_OpenStatus, 1u);
    OBL_CHECK(s.Index("queue") < s.Index("hide"));
    OBL_CHECK(s.Index("hide") < s.Index("clear queue"));
    OBL_CHECK(s.Index("clear queue") < s.Index("closing"));
}

OBL_TEST_CASE("building_lifecycle.exit_power_cases")
{
    // No shortage: nothing is touched, not even a flag that was set.
    {
        Site s;
        s.def->m_Power = 5;
        s.world.powerUse = 10.0f;
        s.world.currentPower = 10.0f; // equal is not a shortage
        s.building->m_closedForPower = 1;
        s.building->m_OpenStatus = 0;
        OBL_CHECK(!s.Exit().shed);
        OBL_CHECK_EQ(int{s.building->m_closedForPower}, 1);
    }
    // A shortage, but the building draws no power (or makes it): it only clears the flag.
    for (int power : {0, -3}) {
        Site s;
        s.def->m_Power = power;
        s.world.powerUse = 20.0f;
        s.world.currentPower = 10.0f;
        s.building->m_closedForPower = 1;
        const ExitReport report = s.Exit();
        OBL_CHECK(!report.shed);
        OBL_CHECK_EQ(int{s.building->m_closedForPower}, 0);
        OBL_CHECK(!s.Called("hide"));
    }
    // The handyman's hut is spared while there is capacity left, whatever it draws.
    {
        Site s;
        s.SetKey("HANDYMAN");
        s.def->m_Power = 5;
        s.world.powerUse = 20.0f;
        s.world.currentPower = 10.0f;
        s.world.maxPower = 50.0f;
        OBL_CHECK(!s.Exit().shed);
    }
    // ... but not when the world is at its maximum.
    {
        Site s;
        s.SetKey("HANDYMAN");
        s.def->m_Power = 5;
        s.world.powerUse = 60.0f;
        s.world.currentPower = 50.0f;
        s.world.maxPower = 50.0f;
        OBL_CHECK(s.Exit().shed);
    }
}

// ---- MT_REPAIR -----------------------------------------------------------------------------------------------------------------------------

namespace {

messages::Message From(int32_t sender)
{
    messages::Message message{};
    message.sender = sender;
    message.receiver = 7;
    message.type = messages::MT_REPAIR;
    return message;
}

} // namespace

OBL_TEST_CASE("building_lifecycle.repair_steps_the_wear_down_and_asks_for_more")
{
    struct Step {
        uint32_t from, to;
    };
    for (Step step : {Step{3, 2}, Step{2, 1}, Step{7, 2}}) {
        Site s;
        s.building->m_RepairState = step.from;
        const RepairReport report = RepairMessage(s, *s.building, From(42), s.world);
        OBL_CHECK(report.more);
        OBL_CHECK(!report.finished);
        OBL_CHECK_EQ(s.building->m_RepairState, step.to);
        OBL_CHECK_EQ(s.posted.size(), size_t{1});
        OBL_CHECK_EQ(s.posted[0].sender, 7);
        OBL_CHECK_EQ(s.posted[0].receiver, 42);
        OBL_CHECK_EQ(s.posted[0].type, int{messages::MT_MORE_REPAIRS});
    }
}

OBL_TEST_CASE("building_lifecycle.repair_more_leaves_the_claims_alone")
{
    Site s;
    s.building->m_RepairState = 3;
    s.building->m_repairing = 1;
    s.building->m_calledForRepair = 1;
    s.building->m_wasBroken = 1;
    s.world.gameDay = 9;
    RepairMessage(s, *s.building, From(42), s.world);
    OBL_CHECK_EQ(int{s.building->m_repairing}, 1);
    OBL_CHECK_EQ(int{s.building->m_calledForRepair}, 1);
    OBL_CHECK_EQ(int{s.building->m_wasBroken}, 1);
    OBL_CHECK_EQ(s.building->m_LastRepairDay, 0);
    OBL_CHECK(!s.Called("repaired"));
}

OBL_TEST_CASE("building_lifecycle.repair_last_step_finishes_and_tells_the_player")
{
    Site s;
    s.building->m_RepairState = 1;
    s.building->m_repairing = 1;
    s.building->m_calledRepairing = 1;
    s.building->m_urgentRepairCalled = 1;
    s.building->m_calledForRepair = 1;
    s.building->m_calledForInspection = 1;
    s.building->m_wasBroken = 1;
    s.world.gameDay = 9;
    s.world.gameDayTime = 14.5f;
    const RepairReport report = RepairMessage(s, *s.building, From(42), s.world);
    OBL_CHECK(report.finished);
    OBL_CHECK(report.notified);
    OBL_CHECK(!report.noPower);
    OBL_CHECK_EQ(s.building->m_RepairState, 0u);
    OBL_CHECK_EQ(s.posted[0].type, int{messages::MT_REPAIRS_FINISHED});
    OBL_CHECK_EQ(s.posted[0].receiver, 42);
    OBL_CHECK_EQ(int{s.building->m_repairing}, 0);
    OBL_CHECK_EQ(int{s.building->m_calledRepairing}, 0);
    OBL_CHECK_EQ(int{s.building->m_urgentRepairCalled}, 0);
    OBL_CHECK_EQ(int{s.building->m_calledForRepair}, 0);
    OBL_CHECK_EQ(int{s.building->m_calledForInspection}, 0);
    OBL_CHECK_EQ(int{s.building->m_wasBroken}, 0);
    OBL_CHECK_EQ(s.building->m_LastRepairDay, 9);
    OBL_CHECK_EQ(s.building->m_lastRepairDayTime, 14.5f);
    const std::vector<std::string> expected = {"post", "repaired"};
    OBL_CHECK(s.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.repair_inspection_of_a_working_building_is_silent")
{
    Site s;
    s.building->m_RepairState = 0; // an inspection visit: nothing to take down
    s.building->m_wasBroken = 0;
    s.building->m_calledForInspection = 1;
    const RepairReport report = RepairMessage(s, *s.building, From(42), s.world);
    OBL_CHECK(report.finished);
    OBL_CHECK(!report.notified);
    OBL_CHECK_EQ(int{s.building->m_calledForInspection}, 0);
    const std::vector<std::string> expected = {"post"};
    OBL_CHECK(s.calls == expected);
}

OBL_TEST_CASE("building_lifecycle.repair_of_a_building_shut_for_power_uses_the_other_notice")
{
    Site s;
    s.building->m_RepairState = 1;
    s.building->m_wasBroken = 1;
    s.building->m_closedForPower = 1;
    const RepairReport report = RepairMessage(s, *s.building, From(42), s.world);
    OBL_CHECK(report.noPower);
    OBL_CHECK(s.Called("repaired (no power)"));
}

// ---- MT_STAFF_CREATED, MT_NEW_DAY, MT_RE_POLL_FLAG_STATE, MT_TABLE_CREATED, MT_LOUNGER_CREATED -----------------------------------------------

OBL_TEST_CASE("building_lifecycle.staff_created_serving_staff_is_counted_by_the_definition_of_its_slot")
{
    Site s;
    s.def->m_Staff[0] = 5;
    s.def->m_Staff[1] = 6;
    s.def->m_Staff[2] = 5;
    s.femaleDefinitions = {6};
    s.building->m_numMalestaff = 1; // slot 0 is taken: the new member takes slot 1, which the definition lists as a woman
    s.building->m_staff_busy[1] = 1;
    messages::Message message = From(30);
    message.type = messages::MT_STAFF_CREATED;
    const StaffCreatedReport report = StaffCreated(s, *s.building, message);
    OBL_CHECK(report.serving);
    OBL_CHECK(report.female);
    OBL_CHECK(!report.hidden);
    OBL_CHECK_EQ(s.building->m_staff[1], 30);
    OBL_CHECK_EQ(s.building->m_staff_busy[1], 0);
    OBL_CHECK_EQ(s.building->m_numMalestaff, 1);
    OBL_CHECK_EQ(s.building->m_numFemalestaff, 1);
    // the next one takes slot 2 (male + female), which is a man's
    message.sender = 31;
    OBL_CHECK(!StaffCreated(s, *s.building, message).female);
    OBL_CHECK_EQ(s.building->m_staff[2], 31);
    OBL_CHECK_EQ(s.building->m_numMalestaff, 2);
    OBL_CHECK_EQ(s.building->m_numFemalestaff, 1);
    OBL_CHECK(s.hidden.empty()); // the building is working
}

OBL_TEST_CASE("building_lifecycle.staff_created_other_staff_goes_to_the_non_serving_list")
{
    Site s;
    s.building->m_NumNonServestaff = 2;
    messages::Message message = From(33);
    message.type = messages::MT_STAFF_CREATED;
    message.data = 1;
    const StaffCreatedReport report = StaffCreated(s, *s.building, message);
    OBL_CHECK(!report.serving);
    OBL_CHECK_EQ(s.building->m_NonServeStaff[2], 33);
    OBL_CHECK_EQ(s.building->m_NumNonServestaff, 3);
    OBL_CHECK_EQ(s.building->m_numMalestaff + s.building->m_numFemalestaff, 0);
    OBL_CHECK(!s.Called("staff def")); // no definition is looked up for them
}

OBL_TEST_CASE("building_lifecycle.staff_created_in_a_closed_building_hides_the_new_member")
{
    for (int32_t data : {0, 1}) {
        Site s;
        s.building->m_OpenStatus = 1;
        messages::Message message = From(34);
        message.type = messages::MT_STAFF_CREATED;
        message.data = data;
        OBL_CHECK(StaffCreated(s, *s.building, message).hidden);
        OBL_CHECK_EQ(s.hidden.size(), size_t{1});
        OBL_CHECK_EQ(s.hidden[0], 34);
    }
    // an open building (also a status other than 1) leaves the member visible
    Site open;
    open.building->m_OpenStatus = 2;
    messages::Message message = From(35);
    message.type = messages::MT_STAFF_CREATED;
    OBL_CHECK(!StaffCreated(open, *open.building, message).hidden);
}

OBL_TEST_CASE("building_lifecycle.new_day_only_closes_the_books_of_a_working_building")
{
    for (int32_t state : {1, 2, 3, 4}) {
        Site s;
        s.building->m_state = static_cast<uint32_t>(state);
        s.building->m_profitToday = 9.0f;
        s.building->m_BuildingAge = 5;
        OBL_CHECK(!NewDay(*s.building));
        OBL_CHECK_EQ(s.building->m_BuildingAge, 5);
        OBL_CHECK_EQ(s.building->m_profitToday, 9.0f);
    }
}

OBL_TEST_CASE("building_lifecycle.new_day_spreads_the_first_record_over_the_table_as_the_game_does")
{
    Site s;
    s.building->m_state = 0;
    for (int i = 0; i < 64; ++i) s.building->m_profitRecord[i] = static_cast<float>(100 + i);
    s.building->m_profitToday = 8.0f;
    s.building->m_incomeToday = 20.0f;
    s.building->m_costToday = 12.0f;
    s.building->m_NumVisitsToday = 6;
    s.building->m_VisitsSinceCleaned = 6;
    s.building->m_NumTimesCleanedToday = 2;
    s.building->m_BuildingAge = 3;
    OBL_CHECK(NewDay(*s.building));
    OBL_CHECK_EQ(s.building->m_profitRecord[0], 8.0f);
    for (int i = 1; i < 64; ++i)
        OBL_CHECK_EQ(s.building->m_profitRecord[i], 100.0f); // the forward copy repeats the old entry 0 (a bug of the game, kept)
    OBL_CHECK_EQ(s.building->m_BuildingAge, 4);
    OBL_CHECK_EQ(s.building->m_averageProfit, (8.0f + 3 * 100.0f) / 4.0f);
    OBL_CHECK_EQ(s.building->m_profitToday, 0.0f);
    OBL_CHECK_EQ(s.building->m_incomeToday, 0.0f);
    OBL_CHECK_EQ(s.building->m_costToday, 0.0f);
    OBL_CHECK_EQ(s.building->m_NumVisitsToday, 0);
    OBL_CHECK_EQ(s.building->m_NumTimesCleanedToday, 0);
    OBL_CHECK_EQ(s.building->m_VisitsSinceCleaned, 6); // not a daily counter
}

OBL_TEST_CASE("building_lifecycle.new_day_average_sums_at_most_64_records_but_divides_by_the_age")
{
    Site s;
    s.building->m_state = 0;
    for (int i = 0; i < 64; ++i) s.building->m_profitRecord[i] = 2.0f;
    s.building->m_BuildingAge = 99; // 100 days old
    NewDay(*s.building);
    OBL_CHECK_EQ(s.building->m_BuildingAge, 100);
    OBL_CHECK_EQ(s.building->m_averageProfit, 126.0f / 100.0f); // today's 0 and 63 copies of the old entry 0
}

OBL_TEST_CASE("building_lifecycle.new_day_scales_the_commodities_by_their_daily_factors")
{
    Site s;
    s.building->m_state = 0;
    s.def->m_NumCommodities = 2;
    s.def->m_CommodityC[0] = 0.5f;
    s.def->m_CommodityC[1] = 2.0f;
    s.def->m_CommodityE[0] = 0.25f;
    s.def->m_CommodityE[1] = 3.0f;
    for (int i = 0; i < 5; ++i) {
        s.building->m_Commod_Intensity[i][0] = 8.0f;
        s.building->m_Commod_Intensity[i][1] = 4.0f;
        s.building->m_Commod_PriceFactor[i] = 1.0f;
        s.def->m_CommodityC[i] = i < 2 ? s.def->m_CommodityC[i] : 100.0f; // a slot beyond the definition's count is not touched
    }
    NewDay(*s.building);
    OBL_CHECK_EQ(s.building->m_Commod_Intensity[0][0], 4.0f);
    OBL_CHECK_EQ(s.building->m_Commod_Intensity[0][1], 2.0f);
    OBL_CHECK_EQ(s.building->m_Commod_PriceFactor[0], 0.25f);
    OBL_CHECK_EQ(s.building->m_Commod_Intensity[1][0], 16.0f);
    OBL_CHECK_EQ(s.building->m_Commod_Intensity[1][1], 8.0f);
    OBL_CHECK_EQ(s.building->m_Commod_PriceFactor[1], 3.0f);
    OBL_CHECK_EQ(s.building->m_Commod_Intensity[2][0], 8.0f);
    OBL_CHECK_EQ(s.building->m_Commod_PriceFactor[2], 1.0f);
}

OBL_TEST_CASE("building_lifecycle.re_poll_flag_state_follows_the_swimming_area")
{
    Site s;
    s.AddPart(8, 8, 0);
    s.building->m_InfluenceArea = 3;
    s.parts[0]->m_frameIndex = 7;
    s.areaOpen = true;
    OBL_CHECK(!RePollFlagState(s, *s.building)); // already showing "open": nothing changes, the frame keeps running
    OBL_CHECK_EQ(s.parts[0]->m_frameIndex, 7);
    s.areaOpen = false;
    OBL_CHECK(RePollFlagState(s, *s.building));
    OBL_CHECK_EQ(s.parts[0]->m_animIndex, 1);
    OBL_CHECK_EQ(s.parts[0]->m_frameIndex, 0);
    s.parts[0]->m_frameIndex = 5;
    OBL_CHECK(!RePollFlagState(s, *s.building));
    OBL_CHECK_EQ(s.parts[0]->m_frameIndex, 5);
    s.areaOpen = true;
    OBL_CHECK(RePollFlagState(s, *s.building));
    OBL_CHECK_EQ(s.parts[0]->m_animIndex, 0);
}

OBL_TEST_CASE("building_lifecycle.re_poll_flag_state_without_an_area_does_not_look")
{
    Site s;
    s.AddPart(8, 8, 0);
    s.building->m_InfluenceArea = -1;
    OBL_CHECK(!RePollFlagState(s, *s.building));
    OBL_CHECK(s.calls.empty());
}

OBL_TEST_CASE("building_lifecycle.table_and_lounger_created_are_appended")
{
    Site s;
    messages::Message message = From(50);
    message.type = messages::MT_TABLE_CREATED;
    TableCreated(*s.building, message);
    message.sender = 51;
    TableCreated(*s.building, message);
    message.type = messages::MT_LOUNGER_CREATED;
    message.sender = 60;
    LoungerCreated(*s.building, message);
    OBL_CHECK_EQ(s.building->m_numTables, 2u);
    OBL_CHECK_EQ(s.building->m_Tables[0], 50);
    OBL_CHECK_EQ(s.building->m_Tables[1], 51);
    OBL_CHECK_EQ(s.building->m_numLoungers, 1u);
    OBL_CHECK_EQ(s.building->m_loungers[0], 60);
}

// ---- the vehicles: VEHICLE_CREATED, GUEST_LEFT_VEHICLE, BOARD_VEHICLE, WAIT_FOR_BOAT -----------------------------------------------------------

OBL_TEST_CASE("building_lifecycle.vehicle_created_is_listed_free_and_gets_its_route_stop")
{
    Site s;
    messages::Message message = From(90);
    message.type = messages::MT_VEHICLE_CREATED;
    s.building->m_vehicleInUse[0] = 1;
    s.building->m_vehicleInUse[1] = 1;
    VehicleCreated(s, *s.building, message);
    message.sender = 91;
    VehicleCreated(s, *s.building, message);
    OBL_CHECK_EQ(s.building->m_NumVehicles, 2);
    OBL_CHECK_EQ(s.building->m_Vehicles[0], 90);
    OBL_CHECK_EQ(s.building->m_Vehicles[1], 91);
    OBL_CHECK_EQ(int{s.building->m_vehicleInUse[0]}, 0);
    OBL_CHECK_EQ(int{s.building->m_vehicleInUse[1]}, 0);
    OBL_CHECK_EQ(s.routeStops.size(), size_t{2});
    OBL_CHECK_EQ(s.routeStops[1], 91);
    OBL_CHECK(s.posted.empty());
}

OBL_TEST_CASE("building_lifecycle.guest_left_without_tables_sends_the_guest_on")
{
    Site s;
    s.building->m_NumVehicles = 3;
    s.building->m_Vehicles[0] = 90;
    s.building->m_Vehicles[1] = 91;
    s.building->m_Vehicles[2] = 91; // a repeated id: only the first counts
    messages::Message message = From(55);
    message.type = messages::MT_GUEST_LEFT_VEHICLE;
    message.data = 91;
    const GuestLeftReport report = VehicleLeft(s, *s.building, message);
    OBL_CHECK(report.matched);
    OBL_CHECK(!report.seated);
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    OBL_CHECK_EQ(s.posted[0].sender, 7);
    OBL_CHECK_EQ(s.posted[0].receiver, 55);
    OBL_CHECK_EQ(s.posted[0].type, int{messages::MT_STATECHANGE});
    OBL_CHECK_EQ(s.posted[0].flags, 0);
    OBL_CHECK_EQ(s.posted[0].data, 0x1c);
    OBL_CHECK(s.seated.empty());
}

OBL_TEST_CASE("building_lifecycle.guest_left_with_tables_seats_the_guest_at_the_table_of_the_vehicle")
{
    Site s;
    s.building->m_NumVehicles = 2;
    s.building->m_Vehicles[0] = 90;
    s.building->m_Vehicles[1] = 91;
    s.building->m_numTables = 2;
    s.building->m_Tables[0] = 70;
    s.building->m_Tables[1] = 71;
    messages::Message message = From(55);
    message.type = messages::MT_GUEST_LEFT_VEHICLE;
    message.data = 91;
    const GuestLeftReport report = VehicleLeft(s, *s.building, message);
    OBL_CHECK(report.matched);
    OBL_CHECK(report.seated);
    OBL_CHECK_EQ(s.seated.size(), size_t{1});
    OBL_CHECK_EQ(s.seated[0].first, 71);
    OBL_CHECK_EQ(s.seated[0].second, 55);
    OBL_CHECK(s.posted.empty());
}

OBL_TEST_CASE("building_lifecycle.guest_left_from_an_unrecognized_vehicle_or_without_vehicles_does_nothing")
{
    Site s;
    messages::Message message = From(55);
    message.type = messages::MT_GUEST_LEFT_VEHICLE;
    message.data = 91;
    OBL_CHECK(!VehicleLeft(s, *s.building, message).matched);
    s.building->m_NumVehicles = 1;
    s.building->m_Vehicles[0] = 90;
    OBL_CHECK(!VehicleLeft(s, *s.building, message).matched);
    OBL_CHECK(s.calls.empty());
}

OBL_TEST_CASE("building_lifecycle.board_vehicle_tells_the_guest_and_the_vehicle_of_the_same_index")
{
    Site s;
    s.building->m_NumVehicles = 2;
    s.building->m_Vehicles[0] = 90;
    s.building->m_Vehicles[1] = 91;
    s.building->m_numTables = 3;
    s.building->m_Tables[0] = 70;
    s.building->m_Tables[1] = 71;
    s.building->m_Tables[2] = 70; // the loop goes on after a match
    messages::Message message = From(70);
    message.type = messages::MT_BOARD_VEHICLE;
    message.data = 55;
    OBL_CHECK_EQ(BoardVehicle(s, *s.building, message), 2);
    OBL_CHECK_EQ(s.posted.size(), size_t{4});
    OBL_CHECK_EQ(s.posted[0].receiver, 55);
    OBL_CHECK_EQ(s.posted[0].type, int{messages::MT_STATECHANGE});
    OBL_CHECK_EQ(s.posted[0].data, 0xf);
    OBL_CHECK_EQ(s.posted[1].sender, 7);
    OBL_CHECK_EQ(s.posted[1].receiver, 90);
    OBL_CHECK_EQ(s.posted[1].type, int{messages::MT_SERVE_GUEST});
    OBL_CHECK_EQ(s.posted[1].data, 55);
    OBL_CHECK_EQ(s.posted[2].receiver, 55);
    // the table of index 2 has no vehicle (only two are listed): the slot is whatever it holds, here empty
    OBL_CHECK_EQ(s.posted[3].receiver, 0);
    OBL_CHECK_EQ(s.posted[3].type, int{messages::MT_SERVE_GUEST});
}

OBL_TEST_CASE("building_lifecycle.board_vehicle_beyond_the_vehicle_slots_reads_what_follows_them")
{
    Site s;
    s.building->m_numTables = 7;
    for (int i = 0; i < 6; ++i) s.building->m_Tables[i] = 100 + i;
    s.building->m_Tables[6] = 70;
    s.building->m_vehicleInUse[0] = 5;
    messages::Message message = From(70);
    message.type = messages::MT_BOARD_VEHICLE;
    message.data = 55;
    OBL_CHECK_EQ(BoardVehicle(s, *s.building, message), 1);
    OBL_CHECK_EQ(s.posted.size(), size_t{2});
    OBL_CHECK_EQ(s.posted[1].receiver, 5);
}

OBL_TEST_CASE("building_lifecycle.board_vehicle_from_an_unrecognized_table_does_nothing")
{
    Site s;
    s.building->m_numTables = 1;
    s.building->m_Tables[0] = 70;
    messages::Message message = From(71);
    message.type = messages::MT_BOARD_VEHICLE;
    OBL_CHECK_EQ(BoardVehicle(s, *s.building, message), 0);
    s.building->m_numTables = 0;
    message.sender = 70;
    OBL_CHECK_EQ(BoardVehicle(s, *s.building, message), 0);
    OBL_CHECK(s.posted.empty());
}

OBL_TEST_CASE("building_lifecycle.wait_for_boat_queues_the_guest_and_walks_it_except_at_the_stops")
{
    struct Case {
        const char* key;
        const char* name;
        bool moves;
    };
    for (Case c : {Case{"NJJETSKIHIRE", "NJJETSKIHIRE", true}, Case{"WHARF", "WHARF", false}, Case{"BUSSTOP", "BUSSTOP", false},
                 Case{"COMPLAINTHUT", "COMPLAINTHUT", false}, Case{"WHARF2", "WHARF2", true}}) {
        Site s;
        s.SetKey(c.key);
        messages::Message message = From(55);
        message.type = messages::MT_WAIT_FOR_BOAT;
        OBL_CHECK_EQ(WaitForBoat(s, *s.building, message), c.moves);
        OBL_CHECK_EQ(s.enqueued.size(), size_t{1});
        OBL_CHECK_EQ(s.enqueued[0].first, 55);
        OBL_CHECK_EQ(s.enqueued[0].second, c.moves);
        OBL_CHECK(s.posted.empty());
    }
}

// ---- group 3: guests and goods -----------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_lifecycle.look_around_below_the_limit_does_nothing")
{
    Site s;
    s.building->m_VisitsSinceCleaned = 0x19; // the limit itself does not count (unsigned compare)
    s.nearby.push_back(s.AddObject(60, object::ObjectType::Guest));
    const LookAroundReport report = LookAround(s, *s.building);
    OBL_CHECK(!report.looked);
    OBL_CHECK(s.calls.empty());
    s.building->m_VisitsSinceCleaned = -1; // a negative count is a huge unsigned one
    OBL_CHECK(LookAround(s, *s.building).looked);
}

OBL_TEST_CASE("building_lifecycle.look_around_shows_the_state_to_the_guests_and_wakes_itself_up")
{
    Site s;
    s.building->m_VisitsSinceCleaned = 26;
    s.nearby = {s.AddObject(60, object::ObjectType::Guest), s.AddObject(61, object::ObjectType::Vehicle),
            s.AddObject(62, object::ObjectType::Guest), s.AddObject(63, object::ObjectType::Building)};
    const LookAroundReport report = LookAround(s, *s.building);
    OBL_CHECK(report.looked);
    OBL_CHECK_EQ(report.guestsNotified, uint32_t{2});
    OBL_CHECK_EQ(s.nearRadius, 2);
    OBL_CHECK_EQ(s.posted.size(), size_t{2});
    for (size_t i = 0; i < 2; ++i) {
        OBL_CHECK_EQ(s.posted[i].sender, 7);
        OBL_CHECK_EQ(s.posted[i].receiver, i == 0 ? 60 : 62);
        OBL_CHECK_EQ(s.posted[i].type, int(messages::MT_SPOT_OPINION));
        OBL_CHECK_EQ(s.posted[i].data, 0);
    }
    OBL_CHECK_EQ(s.delayedPosts.size(), size_t{1});
    OBL_CHECK_EQ(s.delayedPosts[0].first.sender, 7);
    OBL_CHECK_EQ(s.delayedPosts[0].first.receiver, 7);
    OBL_CHECK_EQ(s.delayedPosts[0].first.type, int(messages::MT_LOOK_AROUND));
    OBL_CHECK_EQ(s.delayedPosts[0].second, 2.0f);
    OBL_CHECK(s.Index("post delayed") > s.Index("post")); // the wake-up goes after the spot opinions
}

OBL_TEST_CASE("building_lifecycle.look_around_with_nobody_near_only_wakes_itself_up")
{
    Site s;
    s.building->m_VisitsSinceCleaned = 100;
    const LookAroundReport report = LookAround(s, *s.building);
    OBL_CHECK(report.looked);
    OBL_CHECK_EQ(report.guestsNotified, uint32_t{0});
    OBL_CHECK(s.posted.empty());
    OBL_CHECK_EQ(s.delayedPosts.size(), size_t{1});
}

namespace {
// A shop with five slots whose goods are named by keys; the queue has a script of guests waiting for slots.
void GoodsShop(Site& s)
{
    s.commodityKeys = {"BEER", "SNORKEL", "LILOS", "SURFBOARD", "LIVEVENT"};
    for (int i = 0; i < 5; ++i) s.def->m_Commodity[i] = i;
    s.building->m_queue = &s.queueMarker;
    for (int i = 0; i < 5; ++i) s.building->m_Commod_Price[i] = 1.0f + static_cast<float>(i);
}
} // namespace

OBL_TEST_CASE("building_lifecycle.commod_quality_counts_the_rental_goods_that_come_back")
{
    struct Case {
        int slot;
        RentalGood good;
    };
    for (Case c : {Case{1, RentalGood::Snorkel}, Case{2, RentalGood::Lilo}, Case{3, RentalGood::Board}, Case{0, RentalGood::None},
                 Case{4, RentalGood::None}}) {
        Site s;
        GoodsShop(s);
        s.building->m_CurCapacity = 3;
        messages::Message message = From(55);
        message.type = messages::MT_COMMOD_QUALITY;
        message.data = c.slot;
        const CommodQualityReport report = CommodQuality(s, *s.building, message);
        OBL_CHECK(report.rental == c.good);
        OBL_CHECK_EQ(s.building->m_NumSnorkels, c.good == RentalGood::Snorkel ? 1 : 0);
        OBL_CHECK_EQ(s.building->m_NumLilos, c.good == RentalGood::Lilo ? 1 : 0);
        OBL_CHECK_EQ(s.building->m_NumBoards, c.good == RentalGood::Board ? 1 : 0);
        OBL_CHECK_EQ(s.building->m_CurCapacity, 2);
        OBL_CHECK(!report.next);
        OBL_CHECK(s.posted.empty());
    }
}

OBL_TEST_CASE("building_lifecycle.commod_quality_gives_the_place_to_the_first_guest_of_the_queue")
{
    Site s;
    GoodsShop(s);
    s.building->m_CurCapacity = 3;
    s.queue = {{90, 0}, {91, 2}};
    messages::Message message = From(55);
    message.type = messages::MT_COMMOD_QUALITY;
    message.data = 0;
    const CommodQualityReport report = CommodQuality(s, *s.building, message);
    OBL_CHECK(report.next);
    OBL_CHECK_EQ(s.building->m_CurCapacity, 3); // one place freed, one taken
    OBL_CHECK_EQ(s.queue.size(), size_t{1});
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    OBL_CHECK_EQ(s.posted[0].sender, 7);
    OBL_CHECK_EQ(s.posted[0].receiver, 90);
    OBL_CHECK_EQ(s.posted[0].type, int(messages::MT_IM_READY));
    OBL_CHECK_EQ(s.posted[0].data, 7); // the building itself serves
    OBL_CHECK_EQ(s.posted[0].floatData, 1.0f); // m_Commod_Price[slot 0]
    OBL_CHECK(!s.Called("event price"));
}

OBL_TEST_CASE("building_lifecycle.commod_quality_prices_a_live_event_by_the_schedule")
{
    Site s;
    GoodsShop(s);
    s.queue = {{90, 4}};
    messages::Message message = From(55);
    message.type = messages::MT_COMMOD_QUALITY;
    message.data = 0;
    OBL_CHECK(CommodQuality(s, *s.building, message).next);
    OBL_CHECK_EQ(s.posted[0].floatData, 4.5f);
    OBL_CHECK(s.Called("event price"));
}

OBL_TEST_CASE("building_lifecycle.commod_quality_without_a_queue_is_the_games_error")
{
    Site s;
    GoodsShop(s);
    s.building->m_queue = nullptr;
    s.queue = {{90, 0}};
    messages::Message message = From(55);
    message.type = messages::MT_COMMOD_QUALITY;
    message.data = 1;
    const CommodQualityReport report = CommodQuality(s, *s.building, message);
    OBL_CHECK(report.noQueue);
    OBL_CHECK_EQ(s.missingQueueSender, 55);
    OBL_CHECK_EQ(s.building->m_NumSnorkels, 1); // the stock was counted before the queue was looked at
    OBL_CHECK(!s.Called("queue size"));
    OBL_CHECK(s.posted.empty());
}

OBL_TEST_CASE("building_lifecycle.guest_served_at_a_stop_hands_over_to_the_queue")
{
    struct Case {
        const char* key;
        const char* name;
        bool boards;
    };
    for (Case c : {Case{"WHARF", "WHARF", true}, Case{"BUSSTOP", "BUSSTOP", true}, Case{"COMPLAINTHUT", "COMPLAINTHUT", true},
                 Case{"NJJETSKIHIRE", "NJJETSKIHIRE", false}}) {
        Site s;
        GoodsShop(s);
        s.SetKey(c.key);
        messages::Message message = From(77);
        message.type = messages::MT_GUEST_SERVED;
        const GuestServedReport report = GuestServed(s, *s.building, message);
        OBL_CHECK_EQ(report.boarded, c.boards);
        OBL_CHECK_EQ(s.boarded, c.boards ? 77 : -1);
        OBL_CHECK(!c.boards || !s.Called("queue size"));
    }
}

OBL_TEST_CASE("building_lifecycle.guest_served_with_nobody_waiting_frees_the_vehicle_or_the_staff_member")
{
    {
        Site s;
        GoodsShop(s);
        s.building->m_NumVehicles = 3;
        s.building->m_Vehicles[0] = 11;
        s.building->m_Vehicles[1] = 77;
        s.building->m_Vehicles[2] = 13;
        s.building->m_vehicleInUse[0] = s.building->m_vehicleInUse[1] = s.building->m_vehicleInUse[2] = 1;
        s.building->m_numMalestaff = 1;
        s.building->m_staff[0] = 77;
        s.building->m_staff_busy[0] = 1;
        messages::Message message = From(77);
        message.type = messages::MT_GUEST_SERVED;
        const GuestServedReport report = GuestServed(s, *s.building, message);
        OBL_CHECK(report.idle);
        OBL_CHECK_EQ(report.freedVehicle, 1);
        OBL_CHECK_EQ(report.freedStaff, 0); // a building with vehicles looks only at them
        OBL_CHECK_EQ(int(s.building->m_vehicleInUse[0]), 1);
        OBL_CHECK_EQ(int(s.building->m_vehicleInUse[1]), 0);
        OBL_CHECK_EQ(int(s.building->m_vehicleInUse[2]), 1);
        OBL_CHECK_EQ(int(s.building->m_staff_busy[0]), 1);
    }
    {
        Site s;
        GoodsShop(s);
        s.building->m_numMalestaff = 1;
        s.building->m_numFemalestaff = 2;
        s.building->m_staff[0] = 5;
        s.building->m_staff[1] = 77;
        s.building->m_staff[2] = 6;
        s.building->m_staff_busy[0] = s.building->m_staff_busy[1] = s.building->m_staff_busy[2] = 1;
        messages::Message message = From(77);
        message.type = messages::MT_GUEST_SERVED;
        const GuestServedReport report = GuestServed(s, *s.building, message);
        OBL_CHECK(report.idle);
        OBL_CHECK_EQ(report.freedStaff, 1);
        OBL_CHECK_EQ(int(s.building->m_staff_busy[0]), 1);
        OBL_CHECK_EQ(int(s.building->m_staff_busy[1]), 0);
        OBL_CHECK_EQ(int(s.building->m_staff_busy[2]), 1);
        OBL_CHECK(!s.Called("get object"));
    }
    {
        Site s; // neither vehicles nor staff: nothing to free
        GoodsShop(s);
        messages::Message message = From(77);
        message.type = messages::MT_GUEST_SERVED;
        const GuestServedReport report = GuestServed(s, *s.building, message);
        OBL_CHECK(report.idle);
        OBL_CHECK_EQ(report.freedVehicle + report.freedStaff, 0);
    }
}

OBL_TEST_CASE("building_lifecycle.guest_served_by_a_vehicle_takes_as_many_guests_as_it_holds_at_once")
{
    Site s;
    GoodsShop(s);
    auto* vehicle = static_cast<object::Vehicle*>(s.AddObject(77, object::ObjectType::Vehicle));
    tables::VehicleDef definition{};
    definition.m_Capacity = 2;
    vehicle->m_definition = &definition;
    s.queue = {{90, 0}, {91, 2}, {92, 3}};
    messages::Message message = From(77);
    message.type = messages::MT_GUEST_SERVED;
    const GuestServedReport report = GuestServed(s, *s.building, message);
    OBL_CHECK_EQ(report.ready, uint32_t{2});
    OBL_CHECK_EQ(report.readyDelayed, uint32_t{0});
    OBL_CHECK_EQ(s.queue.size(), size_t{1});
    OBL_CHECK(s.delayedPosts.empty());
    OBL_CHECK_EQ(s.posted.size(), size_t{2});
    OBL_CHECK_EQ(s.posted[0].receiver, 90);
    OBL_CHECK_EQ(s.posted[1].receiver, 91);
    for (const messages::Message& m : s.posted) {
        OBL_CHECK_EQ(m.sender, 7);
        OBL_CHECK_EQ(m.type, int(messages::MT_IM_READY));
        OBL_CHECK_EQ(m.data, 77); // the vehicle carries them
    }
    OBL_CHECK_EQ(s.posted[0].floatData, 1.0f);
    OBL_CHECK_EQ(s.posted[1].floatData, 3.0f);
}

OBL_TEST_CASE("building_lifecycle.guest_served_by_staff_takes_one_guest_and_tells_it_three_seconds_later")
{
    Site s;
    GoodsShop(s);
    s.AddObject(77, object::ObjectType::ServiceStaff);
    s.queue = {{90, 4}, {91, 2}};
    messages::Message message = From(77);
    message.type = messages::MT_GUEST_SERVED;
    const GuestServedReport report = GuestServed(s, *s.building, message);
    OBL_CHECK_EQ(report.ready, uint32_t{1});
    OBL_CHECK_EQ(report.readyDelayed, uint32_t{1});
    OBL_CHECK(s.posted.empty());
    OBL_CHECK_EQ(s.delayedPosts.size(), size_t{1});
    OBL_CHECK_EQ(s.delayedPosts[0].second, 3.0f);
    OBL_CHECK_EQ(s.delayedPosts[0].first.receiver, 90);
    OBL_CHECK_EQ(s.delayedPosts[0].first.data, 77);
    OBL_CHECK_EQ(s.delayedPosts[0].first.floatData, 4.5f); // LIVEVENT
    OBL_CHECK_EQ(s.queue.size(), size_t{1});
}

OBL_TEST_CASE("building_lifecycle.guest_served_by_a_vehicle_without_capacity_or_a_failing_dequeue_sends_nothing")
{
    {
        Site s;
        GoodsShop(s);
        auto* vehicle = static_cast<object::Vehicle*>(s.AddObject(77, object::ObjectType::Vehicle));
        tables::VehicleDef definition{};
        vehicle->m_definition = &definition; // capacity 0
        s.queue = {{90, 0}};
        messages::Message message = From(77);
        message.type = messages::MT_GUEST_SERVED;
        OBL_CHECK_EQ(GuestServed(s, *s.building, message).ready, uint32_t{0});
        OBL_CHECK(!s.Called("dequeue"));
        OBL_CHECK_EQ(s.queue.size(), size_t{1});
    }
    {
        Site s;
        GoodsShop(s);
        auto* vehicle = static_cast<object::Vehicle*>(s.AddObject(77, object::ObjectType::Vehicle));
        tables::VehicleDef definition{};
        definition.m_Capacity = 3;
        vehicle->m_definition = &definition;
        s.queue = {{90, 0}};
        s.dequeueFails = true;
        messages::Message message = From(77);
        message.type = messages::MT_GUEST_SERVED;
        OBL_CHECK_EQ(GuestServed(s, *s.building, message).ready, uint32_t{0});
        size_t dequeues = 0;
        for (const std::string& call : s.calls)
            if (call == "dequeue") ++dequeues;
        OBL_CHECK_EQ(dequeues, size_t{3}); // every place is tried
        OBL_CHECK(s.posted.empty());
    }
}

// ---- MT_IM_READY ---------------------------------------------------------------------------------------------------------------------------

namespace {

messages::Message Ready(int32_t guest, int32_t slot, int32_t server)
{
    messages::Message message{};
    message.sender = guest;
    message.receiver = 7;
    message.type = messages::MT_IM_READY;
    message.flags = slot;
    message.data = server;
    return message;
}

// GoodsShop plus the numbers the books need: prices 1..5, costs 0.5, 1.5, ..., a fresh till, no beer chosen, no water pollution.
void ReadyShop(Site& s)
{
    GoodsShop(s);
    s.familyOfCommodity = {0, 0, 0, 0, 0};
    for (int i = 0; i < 5; ++i) s.building->m_Commod_Cost[i] = 0.5f + static_cast<float>(i);
    s.building->m_BeerType = -1;
    s.building->m_BeerStrength = 1.5f;
    s.beer.m_AlcStr = 2.0f;
    s.beer.m_Effect = 3;
    s.player.m_cash = 1000.0f;
    s.GameWorld();
}

constexpr int32_t kGuest = 55;
constexpr int32_t kStaffMember = 99;
constexpr int32_t kAlcohol = 3; // Site::FamilyId

} // namespace

OBL_TEST_CASE("building_lifecycle.ready_books_the_sale_in_the_building_the_world_and_the_cash")
{
    Site s;
    ReadyShop(s);
    s.def->m_Category = 1;
    s.building->m_incomeToday = 10.0f;
    s.building->m_costToday = 4.0f;
    layout::World& w = s.GameWorld();
    w.m_incomeToday = 100.0f;
    w.m_retailIncomeToday = 20.0f;
    w.m_costsToday = 50.0f;
    w.m_stockCostToday = 30.0f;
    const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 2, kStaffMember)); // slot 2: price 3, cost 2.5
    OBL_CHECK(!report.live);
    OBL_CHECK_EQ(s.building->m_incomeToday, 13.0f);
    OBL_CHECK_EQ(s.building->m_costToday, 6.5f);
    OBL_CHECK_EQ(w.m_incomeToday, 103.0f);
    OBL_CHECK_EQ(w.m_retailIncomeToday, 23.0f);
    OBL_CHECK_EQ(w.m_costsToday, 52.5f);
    OBL_CHECK_EQ(w.m_stockCostToday, 32.5f);
    OBL_CHECK_EQ(s.player.m_cash, 1000.5f); // cash + price - cost
}

OBL_TEST_CASE("building_lifecycle.ready_sorts_the_income_by_the_category_of_the_building")
{
    struct Case {
        int category;
        const char* name;
        int where;
    }; // where: 0 none, 1 accommodation, 2 attractions, 3 retail
    for (Case c : {Case{0, "HOTEL", 1}, Case{0, "ACCOMBLK", 1}, Case{0, "SOUVSHOP", 3}, Case{1, "SOUVSHOP", 3}, Case{2, "SOUVSHOP", 2},
                 Case{3, "SOUVSHOP", 0}}) {
        Site s;
        ReadyShop(s);
        s.def->m_Category = c.category;
        s.SetKey(c.name);
        ImReady(s, *s.building, Ready(kGuest, 0, kStaffMember)); // price 1
        const layout::World& w = *s.gameWorld;
        OBL_CHECK_EQ(w.m_incomeToday, 1.0f);
        OBL_CHECK_EQ(w.m_accomIncomeToday, c.where == 1 ? 1.0f : 0.0f);
        OBL_CHECK_EQ(w.m_attractIncomeToday, c.where == 2 ? 1.0f : 0.0f);
        OBL_CHECK_EQ(w.m_retailIncomeToday, c.where == 3 ? 1.0f : 0.0f);
    }
}

OBL_TEST_CASE("building_lifecycle.ready_takes_price_and_cost_of_a_live_event_from_the_show")
{
    Site s;
    ReadyShop(s);
    const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 4, kStaffMember));
    OBL_CHECK(report.live);
    OBL_CHECK_EQ(s.building->m_incomeToday, 4.5f);
    OBL_CHECK_EQ(s.building->m_costToday, 0.75f);
    OBL_CHECK_EQ(s.gameWorld->m_stockCostToday, 0.75f);
    OBL_CHECK_EQ(s.player.m_cash, 1003.75f);
}

OBL_TEST_CASE("building_lifecycle.ready_polluted_water_makes_the_guest_ill_unless_a_cleaner_works")
{
    auto cleaner = std::make_unique<object::Building>();
    struct Case {
        float contamination;
        uint32_t open;
        uint32_t repair;
        bool hasCleaner;
        float expected;
    };
    const float repaired = static_cast<float>((1.0 - 2.0 * static_cast<double>(0.33f)) * 0.5);
    for (Case c : {Case{0.5f, 0, 3, false, 0.5f}, // nobody cleans
                 Case{0.5f, 0, 3, true, 0.5f}, // an open cleaner at repair state 3 stops nothing
                 Case{0.5f, 0, 1, true, repaired}, Case{0.5f, 1, 1, true, 0.5f}, // a closed one is as good as none
                 Case{0.0f, 0, 3, false, -1.0f}}) { // clean water: no check at all
        Site s;
        ReadyShop(s);
        s.familyOfCommodity[0] = kAlcohol;
        s.gameWorld->m_waterContam = c.contamination;
        cleaner->m_OpenStatus = c.open;
        cleaner->m_RepairState = c.repair;
        if (c.hasCleaner) s.cleaners = {cleaner.get()};
        const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 0, kStaffMember));
        OBL_CHECK_EQ(report.foodIll, c.expected >= 0.0f);
        if (c.expected >= 0.0f) {
            OBL_CHECK_EQ(s.foodIll.size(), size_t{1});
            OBL_CHECK_EQ(s.foodIll[0].first, kGuest);
            OBL_CHECK_EQ(s.foodIll[0].second, c.expected);
        } else {
            OBL_CHECK(s.foodIll.empty());
            OBL_CHECK(!s.Called("find objects"));
        }
    }
}

OBL_TEST_CASE("building_lifecycle.ready_pollution_spares_other_families_and_the_stalls_that_wash_their_own")
{
    struct Case {
        int family;
        const char* name;
        bool ill;
    };
    for (Case c : {Case{1, "SOUVSHOP", true}, Case{2, "SOUVSHOP", true}, Case{3, "SOUVSHOP", true}, Case{4, "SOUVSHOP", true},
                 Case{0, "SOUVSHOP", false}, Case{3, "ICESTALL", false}, Case{3, "FASTFOOD", false}, Case{3, "BARBECUE", true}}) {
        Site s;
        ReadyShop(s);
        s.familyOfCommodity[0] = c.family;
        s.gameWorld->m_waterContam = 0.25f;
        s.SetKey(c.name);
        OBL_CHECK_EQ(ImReady(s, *s.building, Ready(kGuest, 0, kStaffMember)).foodIll, c.ill);
    }
}

OBL_TEST_CASE("building_lifecycle.ready_self_served_with_tables_seats_the_guest_at_the_first_table_that_takes_it")
{
    Site s;
    ReadyShop(s);
    s.building->m_numTables = 3;
    s.building->m_Tables[0] = 30;
    s.building->m_Tables[1] = 31;
    s.building->m_Tables[2] = 32;
    s.acceptingTable = 31;
    const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 0, 7));
    OBL_CHECK(report.route == ReadyRoute::SelfTables);
    OBL_CHECK_EQ(s.seated.size(), size_t{2});
    OBL_CHECK_EQ(s.seated[1].first, 31);
    OBL_CHECK_EQ(s.seated[1].second, kGuest);
    OBL_CHECK(s.posted.empty());
    Site none;
    ReadyShop(none);
    none.building->m_numTables = 3;
    ImReady(none, *none.building, Ready(kGuest, 0, 7));
    OBL_CHECK_EQ(none.seated.size(), size_t{3}); // every table is tried
}

OBL_TEST_CASE("building_lifecycle.ready_self_served_with_loungers_claims_the_first_free_one")
{
    Site s;
    ReadyShop(s);
    s.building->m_numLoungers = 3;
    s.building->m_loungers[0] = 40;
    s.building->m_loungers[1] = 41;
    s.building->m_loungers[2] = 42;
    s.AddObject(40, object::ObjectType::Scenery)->m_state = 1; // taken
    s.AddObject(41, object::ObjectType::Scenery);
    s.AddObject(42, object::ObjectType::Scenery);
    const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 0, 7));
    OBL_CHECK(report.route == ReadyRoute::SelfLoungers);
    OBL_CHECK_EQ(s.posted.size(), size_t{3});
    OBL_CHECK_EQ(s.posted[0].sender, 7); // served
    OBL_CHECK_EQ(s.posted[0].receiver, kGuest);
    OBL_CHECK_EQ(s.posted[0].type, int(messages::MT_SERVED_COMMOD));
    OBL_CHECK_EQ(s.posted[1].sender, kGuest); // the guest lies down: its own state change to the lounger
    OBL_CHECK_EQ(s.posted[1].receiver, 41);
    OBL_CHECK_EQ(s.posted[1].type, int(messages::MT_STATECHANGE));
    OBL_CHECK_EQ(s.posted[1].data, 4);
    OBL_CHECK_EQ(s.posted[2].sender, 7); // and walks there
    OBL_CHECK_EQ(s.posted[2].receiver, kGuest);
    OBL_CHECK_EQ(s.posted[2].type, int(messages::MT_PAVEMENT2OBJECT));
    OBL_CHECK_EQ(s.posted[2].data, 41);
    OBL_CHECK(s.delayedPosts.empty());
    Site taken;
    ReadyShop(taken);
    taken.building->m_numLoungers = 1;
    taken.building->m_loungers[0] = 40;
    taken.AddObject(40, object::ObjectType::Scenery)->m_state = 1;
    ImReady(taken, *taken.building, Ready(kGuest, 0, 7));
    OBL_CHECK(taken.posted.empty()); // all taken: nothing happens
}

OBL_TEST_CASE("building_lifecycle.ready_self_served_goods_reach_the_guest_at_once_or_four_seconds_later")
{
    struct Case {
        int slot;
        const char* key;
        ReadyRoute route;
        bool now;
        bool audience;
    };
    for (Case c : {Case{4, "LIVEVENT", ReadyRoute::SelfEvent, true, true}, Case{1, "SWIMMING", ReadyRoute::SelfNow, true, false},
                 Case{2, "FUNSLIDE", ReadyRoute::SelfNow, true, false}, Case{0, "BEER", ReadyRoute::SelfDelayed, false, false}}) {
        Site s;
        ReadyShop(s);
        s.commodityKeys[static_cast<size_t>(c.slot)] = c.key;
        const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, c.slot, 7));
        OBL_CHECK(report.route == c.route);
        OBL_CHECK_EQ(s.eventGuests.size(), c.audience ? size_t{1} : size_t{0});
        OBL_CHECK_EQ(s.posted.size(), c.now ? size_t{1} : size_t{0});
        OBL_CHECK_EQ(s.delayedPosts.size(), c.now ? size_t{0} : size_t{1});
        const messages::Message& message = c.now ? s.posted[0] : s.delayedPosts[0].first;
        OBL_CHECK_EQ(message.sender, 7);
        OBL_CHECK_EQ(message.receiver, kGuest);
        OBL_CHECK_EQ(message.type, int(messages::MT_SERVED_COMMOD));
        OBL_CHECK_EQ(message.flags, 0);
        OBL_CHECK_EQ(message.data, 0);
        if (!c.now) OBL_CHECK_EQ(s.delayedPosts[0].second, 4.0f);
    }
}

OBL_TEST_CASE("building_lifecycle.ready_self_served_alcohol_is_served_four_seconds_later_with_the_beers_effect_and_strength")
{
    Site s;
    ReadyShop(s);
    s.familyOfCommodity[0] = kAlcohol;
    const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 0, 7));
    OBL_CHECK(report.route == ReadyRoute::SelfBeer);
    OBL_CHECK(!report.beerDrawn);
    OBL_CHECK_EQ(s.beerCounter, 1);
    OBL_CHECK(s.posted.empty());
    OBL_CHECK_EQ(s.delayedPosts.size(), size_t{1});
    const messages::Message& message = s.delayedPosts[0].first;
    OBL_CHECK_EQ(message.sender, 7);
    OBL_CHECK_EQ(message.receiver, kGuest);
    OBL_CHECK_EQ(message.type, int(messages::MT_SERVED_COMMOD));
    OBL_CHECK_EQ(message.flags, 3); // BeerDef::m_Effect
    OBL_CHECK_EQ(message.data, 7);
    OBL_CHECK_EQ(message.floatData, 3.0f); // ALCSTR 2 * strength 1.5
    OBL_CHECK_EQ(s.delayedPosts[0].second, 4.0f);
    OBL_CHECK(!s.Called("rand"));
}

OBL_TEST_CASE("building_lifecycle.ready_every_twentieth_beer_draws_its_effect_by_the_rating")
{
    struct Case {
        uint32_t rating;
        int rand;
        int effect;
    };
    for (Case c : {Case{50, 0, 1}, Case{50, 7, 3}, Case{0, 4, 1}, Case{51, 0, 0}, Case{51, 1, 2}, Case{51, 5, 4}, Case{200, 3, 0}}) {
        Site s;
        ReadyShop(s);
        s.familyOfCommodity[0] = kAlcohol;
        s.beerCounter = 19;
        s.rating = c.rating;
        s.rands = {c.rand};
        const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 0, 7));
        OBL_CHECK(report.beerDrawn);
        OBL_CHECK_EQ(s.randsUsed, size_t{1});
        OBL_CHECK_EQ(s.beerCounter, 0);
        OBL_CHECK_EQ(s.delayedPosts[0].first.flags, c.effect);
        OBL_CHECK_EQ(s.delayedPosts[0].first.floatData, 3.0f);
    }
}

OBL_TEST_CASE("building_lifecycle.ready_served_by_somebody_else_at_tables_seats_the_guest_at_the_table_of_that_vehicle")
{
    for (int32_t server : {61, 62}) {
        Site s;
        ReadyShop(s);
        s.building->m_numTables = 2;
        s.building->m_Tables[0] = 30;
        s.building->m_Tables[1] = 31;
        s.building->m_NumVehicles = 2;
        s.building->m_Vehicles[0] = 60;
        s.building->m_Vehicles[1] = 61;
        const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 0, server));
        OBL_CHECK(report.route == ReadyRoute::OtherTables);
        OBL_CHECK_EQ(s.seated.size(), server == 61 ? size_t{1} : size_t{0});
        if (server == 61) {
            OBL_CHECK_EQ(s.seated[0].first, 31); // the second vehicle, the second table
            OBL_CHECK_EQ(s.seated[0].second, kGuest);
        }
        OBL_CHECK(s.posted.empty());
    }
}

OBL_TEST_CASE("building_lifecycle.ready_served_by_somebody_else_tells_the_server_to_serve_the_guest")
{
    Site s;
    ReadyShop(s);
    const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 0, kStaffMember));
    OBL_CHECK(report.route == ReadyRoute::OtherPlain);
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    OBL_CHECK_EQ(s.posted[0].sender, 7);
    OBL_CHECK_EQ(s.posted[0].receiver, kStaffMember);
    OBL_CHECK_EQ(s.posted[0].type, int(messages::MT_SERVE_GUEST));
    OBL_CHECK_EQ(s.posted[0].flags, 0);
    OBL_CHECK_EQ(s.posted[0].data, kGuest);
    OBL_CHECK_EQ(s.posted[0].floatData, 0.0f);
    OBL_CHECK_EQ(s.beerCounter, 0);
}

OBL_TEST_CASE("building_lifecycle.ready_served_by_somebody_else_with_a_beer_chosen_passes_the_effect_and_strength_on")
{
    Site s;
    ReadyShop(s);
    s.building->m_BeerType = 2;
    const ImReadyReport report = ImReady(s, *s.building, Ready(kGuest, 3, kStaffMember)); // any good, not only alcohol
    OBL_CHECK(report.route == ReadyRoute::OtherBeer);
    OBL_CHECK_EQ(s.beerCounter, 1);
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    OBL_CHECK_EQ(s.posted[0].receiver, kStaffMember);
    OBL_CHECK_EQ(s.posted[0].type, int(messages::MT_SERVE_GUEST));
    OBL_CHECK_EQ(s.posted[0].flags, 3);
    OBL_CHECK_EQ(s.posted[0].data, kGuest);
    OBL_CHECK_EQ(s.posted[0].floatData, 3.0f);
    OBL_CHECK(s.delayedPosts.empty());
}

// ---- MT_REQUEST_COMMOD --------------------------------------------------------------------------------------------------------------------

namespace {
messages::Message Request(int32_t slot)
{
    messages::Message message = From(kGuest);
    message.type = messages::MT_REQUEST_COMMOD;
    message.data = slot;
    message.flags = 4; // the slot is data, not flags as in IM_READY
    message.floatData = 99.0f;
    return message;
}

void CheckRequestReply(const Site& s, bool available, float price = 0.0f)
{
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    const messages::Message& reply = s.posted[0];
    OBL_CHECK_EQ(reply.sender, 7);
    OBL_CHECK_EQ(reply.receiver, kGuest);
    OBL_CHECK_EQ(reply.type, available ? int(messages::MT_COMMOD_PRICE) : int(messages::MT_NO_STOCK));
    OBL_CHECK_EQ(reply.flags, 0);
    OBL_CHECK_EQ(reply.data, 0);
    OBL_CHECK_EQ(reply.floatData, price);
    OBL_CHECK(s.delayedPosts.empty());
}
} // namespace

OBL_TEST_CASE("building_lifecycle.request_reserves_each_rental_before_quoting_its_price")
{
    struct Case {
        int slot;
        RentalGood good;
        int32_t object::Building::* stock;
    };
    for (Case c : {Case{1, RentalGood::Snorkel, &object::Building::m_NumSnorkels}, Case{2, RentalGood::Lilo, &object::Building::m_NumLilos},
                 Case{3, RentalGood::Board, &object::Building::m_NumBoards}}) {
        for (int initial : {2, 1, 0, -1}) {
            Site s;
            GoodsShop(s);
            s.commodityKeys[0] = s.commodityKeys[c.slot];
            s.commodityKeys[c.slot] = "OTHER";
            s.def->m_Commodity[c.slot] = 0; // the commodity id differs from the slot whose price is quoted
            s.building->m_NumSnorkels = 10;
            s.building->m_NumLilos = 20;
            s.building->m_NumBoards = 30;
            s.building.get()->*c.stock = initial;
            const RequestCommodReport report = RequestCommod(s, *s.building, Request(c.slot));
            OBL_CHECK(report.rental == c.good);
            OBL_CHECK_EQ(report.available, initial != 0);
            OBL_CHECK(!report.lounger);
            OBL_CHECK(!report.live);
            OBL_CHECK_EQ(s.building.get()->*c.stock, initial == 0 ? 0 : initial - 1);
            OBL_CHECK_EQ(s.building->m_NumSnorkels, c.slot == 1 ? (initial == 0 ? 0 : initial - 1) : 10);
            OBL_CHECK_EQ(s.building->m_NumLilos, c.slot == 2 ? (initial == 0 ? 0 : initial - 1) : 20);
            OBL_CHECK_EQ(s.building->m_NumBoards, c.slot == 3 ? (initial == 0 ? 0 : initial - 1) : 30);
            CheckRequestReply(s, initial != 0, initial == 0 ? 0.0f : s.building->m_Commod_Price[c.slot]);
            OBL_CHECK(!s.Called("event price"));
            OBL_CHECK(!s.Called("queue size"));
            OBL_CHECK(!s.Called("dequeue"));
        }
    }
}

OBL_TEST_CASE("building_lifecycle.request_counts_visits_even_when_stock_is_empty_and_litter_controls_dirty_visits")
{
    for (int litter : {0, 1, 7}) {
        Site s;
        GoodsShop(s);
        s.gameWorld->m_allowLitter = static_cast<uint8_t>(litter);
        s.building->m_NumVisitsToday = 12;
        s.building->m_VisitsSinceCleaned = 25;
        OBL_CHECK(!RequestCommod(s, *s.building, Request(1)).available);
        OBL_CHECK_EQ(s.building->m_NumVisitsToday, 13);
        OBL_CHECK_EQ(s.building->m_VisitsSinceCleaned, litter != 0 ? 26 : 25);
        // A second request also counts, whether it is a refusal or a price quote.
        OBL_CHECK(RequestCommod(s, *s.building, Request(0)).available);
        OBL_CHECK_EQ(s.building->m_NumVisitsToday, 14);
        OBL_CHECK_EQ(s.building->m_VisitsSinceCleaned, litter != 0 ? 27 : 25);
    }
}

OBL_TEST_CASE("building_lifecycle.request_lounger_checks_every_object_without_claiming_one")
{
    for (bool free : {false, true}) {
        Site s;
        GoodsShop(s);
        s.commodityKeys[0] = "SUNLNGER";
        s.building->m_numLoungers = 3;
        for (int i = 0; i < 3; ++i) {
            s.building->m_loungers[i] = 70 + i;
            s.AddObject(70 + i, object::ObjectType::Scenery)->m_state = i == 0 && free ? 0 : 4;
        }
        const RequestCommodReport report = RequestCommod(s, *s.building, Request(0));
        OBL_CHECK(report.lounger);
        OBL_CHECK(report.rental == RentalGood::None);
        OBL_CHECK_EQ(report.available, free);
        CheckRequestReply(s, free, free ? 1.0f : 0.0f);
        int lookups = 0;
        for (const std::string& call : s.calls)
            if (call == "get object") ++lookups;
        OBL_CHECK_EQ(lookups, 3); // no early exit when the first one is free
        OBL_CHECK_EQ(s.objects[0]->m_state, free ? 0 : 4);
        OBL_CHECK_EQ(s.objects[1]->m_state, 4);
        OBL_CHECK_EQ(s.objects[2]->m_state, 4);
        OBL_CHECK_EQ(s.building->m_CurCapacity, 0);
    }
}

OBL_TEST_CASE("building_lifecycle.request_lounger_with_zero_or_negative_count_has_no_stock")
{
    for (int count : {0, -1}) {
        Site s;
        GoodsShop(s);
        s.commodityKeys[0] = "SUNLNGER";
        s.building->m_numLoungers = static_cast<uint32_t>(count);
        OBL_CHECK(!RequestCommod(s, *s.building, Request(0)).available);
        CheckRequestReply(s, false);
        OBL_CHECK(!s.Called("get object"));
    }
}

OBL_TEST_CASE("building_lifecycle.request_live_event_quotes_the_current_show_without_booking_or_adding_a_guest")
{
    Site s;
    GoodsShop(s);
    s.eventPrice = 17.25f;
    const RequestCommodReport report = RequestCommod(s, *s.building, Request(4));
    OBL_CHECK(report.available);
    OBL_CHECK(report.live);
    CheckRequestReply(s, true, 17.25f);
    OBL_CHECK(s.Called("event price"));
    OBL_CHECK_EQ(s.building->m_CurCapacity, 0);
    OBL_CHECK_EQ(s.building->m_incomeToday, 0.0f);
    OBL_CHECK(s.eventGuests.empty());
}

OBL_TEST_CASE("building_lifecycle.request_plain_goods_quote_the_requested_slot_without_serving")
{
    for (const char* key : {"BEER", "SWIMMING", "FUNSLIDE", "SNORKEL2", "SUNLNGER2", "LIVEVENT2"}) {
        Site s;
        GoodsShop(s);
        s.commodityKeys[0] = key;
        const RequestCommodReport report = RequestCommod(s, *s.building, Request(0));
        OBL_CHECK(report.available);
        OBL_CHECK(report.rental == RentalGood::None);
        OBL_CHECK(!report.live);
        OBL_CHECK(!report.lounger);
        CheckRequestReply(s, true, 1.0f);
        OBL_CHECK_EQ(s.beerCounter, 0);
        OBL_CHECK_EQ(s.building->m_CurCapacity, 0);
        OBL_CHECK_EQ(s.building->m_incomeToday, 0.0f);
        OBL_CHECK(!s.Called("event price"));
        OBL_CHECK(!s.Called("get object"));
    }
}

// ---- group 5: PRICE_OK, PRICE_NOT_OK, BUILDERS_LEFT_BASEPLATE -----------------------------------------------------------------------------

namespace {
messages::Message PriceAnswer(int type, int32_t flags, int32_t data)
{
    messages::Message message = From(90);
    message.type = type;
    message.flags = flags;
    message.data = data;
    return message;
}

void CheckReadyTo(const Site& s, size_t index, int32_t guest, int32_t server, float price = 0.0f)
{
    const messages::Message& m = s.posted[index];
    OBL_CHECK_EQ(m.sender, 7);
    OBL_CHECK_EQ(m.receiver, guest);
    OBL_CHECK_EQ(m.type, int(messages::MT_IM_READY));
    OBL_CHECK_EQ(m.flags, 0);
    OBL_CHECK_EQ(m.data, server);
    OBL_CHECK_EQ(m.floatData, price);
}
} // namespace

OBL_TEST_CASE("building_lifecycle.price_ok_gives_the_guest_to_the_first_free_vehicle")
{
    Site s;
    GoodsShop(s);
    s.building->m_NumVehicles = 3;
    s.building->m_Vehicles[0] = 11;
    s.building->m_Vehicles[1] = 12;
    s.building->m_Vehicles[2] = 13;
    s.building->m_vehicleInUse[0] = 1;
    s.building->m_numMalestaff = 1; // a building with vehicles does not look at its staff
    s.building->m_staff[0] = 5;
    const PriceOkReport report = PriceOk(s, *s.building, PriceAnswer(messages::MT_PRICE_OK, 0, 2));
    OBL_CHECK(report.route == PriceOkRoute::Vehicle);
    OBL_CHECK_EQ(int(s.building->m_vehicleInUse[1]), 1);
    OBL_CHECK_EQ(int(s.building->m_vehicleInUse[2]), 0);
    OBL_CHECK_EQ(int(s.building->m_staff_busy[0]), 0);
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    CheckReadyTo(s, 0, 90, 12);
    OBL_CHECK(s.enqueued.empty());
}

OBL_TEST_CASE("building_lifecycle.price_ok_with_every_vehicle_busy_queues_the_guest_with_its_slot")
{
    Site s;
    GoodsShop(s);
    s.building->m_NumVehicles = 2;
    s.building->m_vehicleInUse[0] = s.building->m_vehicleInUse[1] = 1;
    const PriceOkReport report = PriceOk(s, *s.building, PriceAnswer(messages::MT_PRICE_OK, 9, 3));
    OBL_CHECK(report.route == PriceOkRoute::Queued);
    OBL_CHECK(s.posted.empty());
    OBL_CHECK_EQ(s.enqueued.size(), size_t{1});
    OBL_CHECK_EQ(s.enqueued[0].first, 90);
    OBL_CHECK(s.enqueued[0].second);
    OBL_CHECK_EQ(s.enqueuedSlots[0], 3); // data, not flags
}

OBL_TEST_CASE("building_lifecycle.price_ok_gives_the_guest_to_the_first_free_staff_member")
{
    Site s;
    GoodsShop(s);
    s.building->m_numMalestaff = 1;
    s.building->m_numFemalestaff = 2;
    s.building->m_staff[0] = 5;
    s.building->m_staff[1] = 6;
    s.building->m_staff[2] = 8;
    s.building->m_staff_busy[0] = s.building->m_staff_busy[1] = 1;
    OBL_CHECK(PriceOk(s, *s.building, PriceAnswer(messages::MT_PRICE_OK, 0, 1)).route == PriceOkRoute::Staff);
    OBL_CHECK_EQ(int(s.building->m_staff_busy[2]), 1);
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    CheckReadyTo(s, 0, 90, 8);
    // all of them busy: the queue
    Site t;
    GoodsShop(t);
    t.building->m_numFemalestaff = 1;
    t.building->m_staff_busy[0] = 1;
    OBL_CHECK(PriceOk(t, *t.building, PriceAnswer(messages::MT_PRICE_OK, 0, 4)).route == PriceOkRoute::Queued);
    OBL_CHECK_EQ(t.enqueuedSlots[0], 4);
    OBL_CHECK(t.posted.empty());
}

OBL_TEST_CASE("building_lifecycle.price_ok_in_a_building_that_serves_itself_counts_the_guest_in_while_there_is_room")
{
    struct Case {
        uint32_t guests;
        uint32_t capacity;
        int32_t lilos, snorkels, boards;
        uint32_t loungers;
        bool served;
    };
    for (Case c : {Case{0, 3, 0, 0, 0, 0, true}, // room
                 Case{3, 3, 0, 0, 0, 0, false}, // full, no rental stock
                 Case{3, 3, 1, 0, 0, 0, true}, // full, but a rental stock is not empty
                 Case{4, 3, 0, 1, 0, 0, true}, Case{3, 3, 0, 0, 1, 0, true},
                 Case{1, 3, 0, 0, 0, 2, true}, // room and fewer guests than loungers
                 Case{2, 3, 0, 0, 0, 2, false}, // room, but as many guests as loungers
                 Case{2, 3, 5, 0, 0, 2, false}}) { // the loungers limit holds even with stock
        Site s;
        GoodsShop(s);
        s.def->m_GuestCapacity = static_cast<int32_t>(c.capacity);
        s.building->m_CurCapacity = static_cast<int32_t>(c.guests);
        s.building->m_NumLilos = c.lilos;
        s.building->m_NumSnorkels = c.snorkels;
        s.building->m_NumBoards = c.boards;
        s.building->m_numLoungers = c.loungers;
        const PriceOkReport report = PriceOk(s, *s.building, PriceAnswer(messages::MT_PRICE_OK, 0, 2));
        OBL_CHECK(report.route == (c.served ? PriceOkRoute::SelfServed : PriceOkRoute::Queued));
        OBL_CHECK_EQ(s.building->m_CurCapacity, static_cast<int32_t>(c.guests + (c.served ? 1 : 0)));
        OBL_CHECK_EQ(s.posted.size(), c.served ? size_t{1} : size_t{0});
        OBL_CHECK_EQ(s.enqueued.size(), c.served ? size_t{0} : size_t{1});
        if (c.served) CheckReadyTo(s, 0, 90, 7); // the building serves
    }
}

OBL_TEST_CASE("building_lifecycle.price_ok_compares_the_places_as_unsigned_numbers")
{
    Site s;
    GoodsShop(s);
    s.def->m_GuestCapacity = 3;
    s.building->m_CurCapacity = -1; // 0xffffffff: more than any limit
    OBL_CHECK(PriceOk(s, *s.building, PriceAnswer(messages::MT_PRICE_OK, 0, 0)).route == PriceOkRoute::Queued);
}

OBL_TEST_CASE("building_lifecycle.price_not_ok_gives_a_rental_good_back_even_without_a_server")
{
    struct Case {
        int slot;
        RentalGood good;
    };
    for (Case c : {Case{1, RentalGood::Snorkel}, Case{2, RentalGood::Lilo}, Case{3, RentalGood::Board}, Case{0, RentalGood::None},
                 Case{4, RentalGood::None}}) {
        Site s;
        GoodsShop(s);
        s.building->m_CurCapacity = 2;
        s.queue = {{91, 0}};
        const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, c.slot, 0));
        OBL_CHECK(report.rental == c.good);
        OBL_CHECK_EQ(s.building->m_NumSnorkels, c.good == RentalGood::Snorkel ? 1 : 0);
        OBL_CHECK_EQ(s.building->m_NumLilos, c.good == RentalGood::Lilo ? 1 : 0);
        OBL_CHECK_EQ(s.building->m_NumBoards, c.good == RentalGood::Board ? 1 : 0);
        OBL_CHECK(!report.released);
        OBL_CHECK_EQ(s.building->m_CurCapacity, 2); // no server: nothing else happens
        OBL_CHECK(!s.Called("queue size"));
        OBL_CHECK(s.posted.empty());
    }
}

OBL_TEST_CASE("building_lifecycle.price_not_ok_with_nobody_waiting_frees_the_place_and_the_server")
{
    {
        Site s;
        GoodsShop(s);
        s.building->m_CurCapacity = 2;
        s.building->m_NumVehicles = 3;
        s.building->m_Vehicles[0] = 11;
        s.building->m_Vehicles[1] = 77;
        s.building->m_Vehicles[2] = 77;
        s.building->m_vehicleInUse[0] = s.building->m_vehicleInUse[1] = s.building->m_vehicleInUse[2] = 1;
        s.building->m_numMalestaff = 1;
        s.building->m_staff[0] = 77;
        s.building->m_staff_busy[0] = 1;
        const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 0, 77));
        OBL_CHECK(report.queueEmpty);
        OBL_CHECK_EQ(report.freedVehicle, 2); // the loop goes on after a match
        OBL_CHECK_EQ(report.freedStaff, 0); // a building with vehicles looks only at them
        OBL_CHECK_EQ(s.building->m_CurCapacity, 1);
        OBL_CHECK_EQ(int(s.building->m_vehicleInUse[0]), 1);
        OBL_CHECK_EQ(int(s.building->m_vehicleInUse[1]), 0);
        OBL_CHECK_EQ(int(s.building->m_vehicleInUse[2]), 0);
        OBL_CHECK_EQ(int(s.building->m_staff_busy[0]), 1);
    }
    {
        Site s;
        GoodsShop(s);
        s.building->m_CurCapacity = 1;
        s.building->m_numMalestaff = 1;
        s.building->m_numFemalestaff = 1;
        s.building->m_staff[0] = 5;
        s.building->m_staff[1] = 77;
        s.building->m_staff_busy[0] = s.building->m_staff_busy[1] = 1;
        const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 0, 77));
        OBL_CHECK_EQ(report.freedStaff, 1);
        OBL_CHECK_EQ(s.building->m_CurCapacity, 0);
        OBL_CHECK_EQ(int(s.building->m_staff_busy[0]), 1);
        OBL_CHECK_EQ(int(s.building->m_staff_busy[1]), 0);
    }
    {
        Site s;
        GoodsShop(s);
        const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 0, 7));
        OBL_CHECK(report.queueEmpty);
        OBL_CHECK_EQ(report.freedVehicle + report.freedStaff, 0);
        OBL_CHECK_EQ(s.building->m_CurCapacity, -1);
        OBL_CHECK(s.posted.empty());
    }
}

OBL_TEST_CASE("building_lifecycle.price_not_ok_hands_the_server_on_to_the_first_guest_of_the_queue")
{
    Site s;
    GoodsShop(s);
    s.building->m_CurCapacity = 2;
    s.queue = {{91, 0}, {92, 4}};
    const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 0, 77));
    OBL_CHECK(report.dequeued);
    OBL_CHECK(report.ready);
    OBL_CHECK_EQ(s.building->m_CurCapacity, 2); // the place stays taken
    OBL_CHECK_EQ(s.queue.size(), size_t{1});
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    CheckReadyTo(s, 0, 91, 77, 1.0f); // m_Commod_Price[0]
    OBL_CHECK(s.delayedPosts.empty());
    OBL_CHECK(!s.Called("event price"));
}

OBL_TEST_CASE("building_lifecycle.price_not_ok_prices_a_live_event_by_the_schedule")
{
    Site s;
    GoodsShop(s);
    s.queue = {{91, 4}};
    OBL_CHECK(PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 0, 77)).ready);
    CheckReadyTo(s, 0, 91, 77, 4.5f);
}

OBL_TEST_CASE("building_lifecycle.price_not_ok_takes_the_next_guests_rental_from_the_stock_and_tells_it_nothing")
{
    struct Case {
        int slot;
        int32_t object::Building::* stock;
    };
    for (Case c :
            {Case{1, &object::Building::m_NumSnorkels}, Case{2, &object::Building::m_NumLilos}, Case{3, &object::Building::m_NumBoards}}) {
        for (int initial : {2, 0, -1}) {
            Site s;
            GoodsShop(s);
            s.building->m_NumSnorkels = s.building->m_NumLilos = s.building->m_NumBoards = 10;
            s.building.get()->*c.stock = initial;
            s.queue = {{91, c.slot}};
            const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 0, 77));
            OBL_CHECK(report.dequeued);
            OBL_CHECK(report.swallowed);
            OBL_CHECK(!report.ready);
            OBL_CHECK_EQ(s.building.get()->*c.stock, initial == 0 ? 0 : initial - 1); // equality with zero, not a positive-stock check
            OBL_CHECK(s.posted.empty());
            OBL_CHECK(s.queue.empty());
        }
    }
}

OBL_TEST_CASE("building_lifecycle.price_not_ok_for_the_next_guests_lounger_changes_nothing_and_tells_it_nothing")
{
    Site s;
    GoodsShop(s);
    s.commodityKeys[0] = "SUNLNGER";
    s.queue = {{91, 0}};
    s.building->m_numLoungers = 2;
    const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 3, 77));
    OBL_CHECK(report.swallowed);
    OBL_CHECK_EQ(s.building->m_NumBoards, 1); // the refused guest's own board came back
    OBL_CHECK(s.posted.empty());
    OBL_CHECK(s.queue.empty());
}

OBL_TEST_CASE("building_lifecycle.price_not_ok_with_a_failing_dequeue_ends_the_message")
{
    Site s;
    GoodsShop(s);
    s.queue = {{91, 0}};
    s.dequeueFails = true;
    const PriceNotOkReport report = PriceNotOk(s, *s.building, PriceAnswer(messages::MT_PRICE_NOT_OK, 0, 77));
    OBL_CHECK(report.released);
    OBL_CHECK(!report.dequeued);
    OBL_CHECK(s.posted.empty());
}

OBL_TEST_CASE("building_lifecycle.builders_left_after_a_build_opens_the_building_and_tells_it_to_work")
{
    Site s;
    messages::Message message = From(7);
    message.type = messages::MT_BUILDERS_LEFT_BASEPLATE;
    message.data = 0x28;
    OBL_CHECK(BuildersLeftBaseplate(s, *s.building, message));
    OBL_CHECK(s.Index("open state") < s.Index("finish build"));
    OBL_CHECK(s.Index("finish build") < s.Index("post"));
    OBL_CHECK_EQ(s.posted.size(), size_t{1});
    OBL_CHECK_EQ(s.posted[0].sender, 7);
    OBL_CHECK_EQ(s.posted[0].receiver, 7);
    OBL_CHECK_EQ(s.posted[0].type, int(messages::MT_STATECHANGE));
    OBL_CHECK_EQ(s.posted[0].flags, 0);
    OBL_CHECK_EQ(s.posted[0].data, 0);
}

OBL_TEST_CASE("building_lifecycle.builders_left_after_a_demolition_only_tells_the_building_to_go")
{
    for (int32_t data : {0x2e, 0, 0x29}) {
        Site s;
        messages::Message message = From(7);
        message.type = messages::MT_BUILDERS_LEFT_BASEPLATE;
        message.data = data;
        OBL_CHECK(!BuildersLeftBaseplate(s, *s.building, message));
        OBL_CHECK(!s.Called("open state"));
        OBL_CHECK(!s.Called("finish build"));
        OBL_CHECK_EQ(s.posted.size(), size_t{1});
        OBL_CHECK_EQ(s.posted[0].receiver, 7);
        OBL_CHECK_EQ(s.posted[0].type, int(messages::MT_STATECHANGE));
        OBL_CHECK_EQ(s.posted[0].data, 3);
    }
}
