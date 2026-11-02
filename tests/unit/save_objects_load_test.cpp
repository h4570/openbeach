#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/map/path_data.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/object_manager_serial.h"
#include "game/objects/object_performers.h"
#include "game/objects/object_table.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/world/save_objects_json.h"
#include "formats/save_states.h"
#include "game/world/save_objects_load.h"

#include <bit>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;

namespace {

world_save::JsonContext WriteContext()
{
    world_save::JsonContext context;
    context.narrowToUtf8 = [](std::string_view text) { return std::string(text); };
    return context;
}

world_save::ReadContext ReadContext()
{
    world_save::ReadContext context;
    context.utf8ToNarrow = [](std::string_view text, std::string& out) {
        out = text;
        return true;
    };
    return context;
}

// The callbacks with a recorded position and one sprite made by name.
struct LoadCallbacks {
    uint32_t position[3] = {};
    int positions = 0;
    std::string created;
    ui::SpriteInstance sprite{};
    int circuit = 0;
    std::string steps; // the order of the calls
    map::Area area{};

    world_save::ObjectLoadCallbacks Make()
    {
        world_save::ObjectLoadCallbacks callbacks;
        callbacks.setPosition = [this](object::Base& o, const uint32_t xyz[3]) {
            std::memcpy(position, xyz, sizeof(position));
            steps += "position ";
            for (int i = 0; i < 3; ++i) o.m_WorldXYZ[i] = std::bit_cast<float>(xyz[i]); // the virtual stores it too
            ++positions;
        };
        callbacks.createSprite = [this](const char* name) -> void* {
            created = name;
            return &sprite;
        };
        callbacks.circuitPointCount = [](int32_t type) { return type == 1 ? 12u : 0u; };
        callbacks.circuitPoints = [this](int32_t) -> void* { return &circuit; };
        callbacks.sceneryDraw = [this](object::Scenery&) { steps += "draw "; };
        callbacks.sceneryFootprint = [this](object::Scenery& s) { steps += "footprint " + std::to_string(s.m_gridH) + " "; };
        callbacks.staffCounter = [this](const char* label, int32_t value) {
            steps += std::string(label) + "=" + std::to_string(value) + " ";
        };
        callbacks.beachArea = [this](int32_t index) -> const map::Area* {
            steps += "area " + std::to_string(index) + " ";
            return index == 2 ? &area : nullptr;
        };
        return callbacks;
    }
};

std::unique_ptr<object::Shark> NewShark()
{
    auto shark = std::make_unique<object::Shark>();
    shark->m_type = object::ObjectType::Shark;
    return shark;
}

} // namespace

OBL_TEST_CASE("save_objects_load.shark_reads_back_class_moving_route_base_and_sprite")
{
    auto shark = NewShark();
    std::strcpy(shark->m_Name, "Shark1");
    shark->m_id = 900;
    shark->m_WorldXYZ.x() = 64.5f;
    shark->m_WorldXYZ.z() = -1.0f;
    shark->m_SwimCircleRect[1] = 0xfffffffeu;
    shark->m_actionState = 3;
    shark->m_ownerId = 7;
    shark->m_vVelocity[1] = 0.25f;
    shark->m_buildingTraversal = 0xa5;
    shark->m_state = 12;
    shark->m_owner = 2;
    shark->m_SelectLevel = 4;
    ui::SpriteInstance sprite{};
    char spriteName[] = "SHARK";
    sprite.m_name = spriteName;
    sprite.m_direction = 2;
    sprite.m_lit = 1;
    sprite.m_startTime = 1.5f;
    shark->m_sprite = &sprite;
    auto route = std::make_shared<path::Route>();
    route->m_steps = {{1, 2}, {3, 4}};
    route->m_stop[1] = 9;
    route->m_pathType = 3;
    path::RouteCursor cursor{route, 0, 1};
    shark->m_pathCursor = &cursor;

    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*shark, WriteContext(), written, error));
    shark->m_pathCursor = nullptr;

    auto loaded = NewShark();
    LoadCallbacks callbacks;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json(written), ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(error.empty());
    OBL_CHECK(std::string(loaded->m_Name) == "shark1");
    OBL_CHECK(loaded->m_id == 900);
    OBL_CHECK(loaded->m_SwimCircleRect[1] == 0xfffffffeu);
    OBL_CHECK(loaded->m_actionState == 3);
    OBL_CHECK(loaded->m_ownerId == 7);
    OBL_CHECK(loaded->m_vVelocity[1] == 0.25f);
    OBL_CHECK(loaded->m_buildingTraversal == 0xa5);
    OBL_CHECK(loaded->m_state == 12);
    OBL_CHECK(loaded->m_owner == 2);
    OBL_CHECK(loaded->m_SelectLevel == 4);
    // The position goes to SetPosition once, as its float bits.
    OBL_CHECK(callbacks.positions == 1);
    OBL_CHECK(callbacks.position[0] == std::bit_cast<uint32_t>(64.5f));
    OBL_CHECK(callbacks.position[2] == std::bit_cast<uint32_t>(-1.0f));
    OBL_CHECK(callbacks.created == "SHARK");
    OBL_REQUIRE(loaded->m_sprite == &callbacks.sprite);
    OBL_CHECK(callbacks.sprite.m_direction == 2);
    OBL_CHECK(callbacks.sprite.m_lit == 1);
    OBL_CHECK(callbacks.sprite.m_startTime == 1.5f);
    OBL_REQUIRE(loaded->m_pathCursor != nullptr);
    OBL_CHECK(loaded->m_pathCursor->m_reverse == 0);
    OBL_CHECK(loaded->m_pathCursor->m_currentStep == 1);
    OBL_CHECK(loaded->m_pathCursor->m_route->m_steps.size() == 2);
    OBL_CHECK(loaded->m_pathCursor->m_route->m_steps[1].y == 4);
    OBL_CHECK(loaded->m_pathCursor->m_route->m_stop[1] == 9);
    OBL_CHECK(loaded->m_pathCursor->m_route->m_pathType == 3);

    // Write again: the same entry.
    callbacks.sprite.m_name = spriteName;
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, WriteContext(), again, error));
    OBL_CHECK(again == written);
    delete loaded->m_pathCursor;
    loaded->m_pathCursor = nullptr;
}

OBL_TEST_CASE("save_objects_load.vehicle_reads_its_constructed_sprites_and_restores_the_circuit")
{
    auto vehicle = std::make_unique<object::Vehicle>();
    std::strcpy(vehicle->m_Name, "ferry");
    vehicle->m_id = 901;
    vehicle->m_type = object::ObjectType::Vehicle;
    vehicle->m_Guests[0] = -1;
    vehicle->m_Guests[299] = 77;
    vehicle->m_DestroyPoint = 200;
    vehicle->m_WayPoints[63][2] = 1.5f;
    vehicle->m_vehicleType = 1;
    ui::SpriteInstance sprite{};
    char spriteName[] = "FERRY";
    sprite.m_name = spriteName;
    ui::SpriteInstance door{};
    door.m_frameIndex = 4;
    vehicle->m_sprite = &sprite;
    vehicle->m_vehicleSprite = &sprite;
    vehicle->m_doorSprite = &door;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*vehicle, WriteContext(), written, error));

    // The constructed vehicle has its own sprites; the base sprite is one of them, as in the game.
    auto loaded = std::make_unique<object::Vehicle>();
    loaded->m_type = object::ObjectType::Vehicle;
    ui::SpriteInstance loadedSprite{}, loadedDoor{};
    loadedSprite.m_name = spriteName;
    loaded->m_sprite = &loadedSprite;
    loaded->m_vehicleSprite = &loadedSprite;
    loaded->m_doorSprite = &loadedDoor;
    LoadCallbacks callbacks;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json(written), ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(loaded->m_Guests[0] == -1);
    OBL_CHECK(loaded->m_Guests[299] == 77);
    OBL_CHECK(loaded->m_DestroyPoint == 200);
    OBL_CHECK(loaded->m_WayPoints[63][2] == 1.5f);
    OBL_CHECK(loadedDoor.m_frameIndex == 4);
    OBL_CHECK(loaded->m_circuitPointCount == 12);
    OBL_CHECK(loaded->m_circuitPoints == &callbacks.circuit);
    OBL_CHECK(callbacks.created.empty());
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, WriteContext(), again, error));
    OBL_CHECK(again == written);
}

OBL_TEST_CASE("save_objects_load.weightlifter_creates_its_attached_sprite_and_keeps_the_action_point_slip")
{
    auto lifter = std::make_unique<object::WeightLifter>();
    std::strcpy(lifter->m_Name, "WeightLifter");
    lifter->m_id = 903;
    lifter->m_type = object::ObjectType::WeightLifter;
    lifter->m_OwnerID = 1050;
    lifter->m_ActionPoint[0] = 1.0f;
    lifter->m_ActionPoint[1] = 2.5f;
    lifter->m_finished = 1;
    ui::SpriteInstance sprite{}, weights{};
    char spriteName[] = "LIFTER", weightsName[] = "WEIGHTS";
    sprite.m_name = spriteName;
    weights.m_name = weightsName;
    weights.m_frameIndex = 3;
    lifter->m_sprite = &sprite;
    lifter->m_attachedSprite = &weights;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*lifter, WriteContext(), written, error));

    auto loaded = std::make_unique<object::WeightLifter>();
    loaded->m_type = object::ObjectType::WeightLifter;
    ui::SpriteInstance own{};
    own.m_name = spriteName;
    loaded->m_sprite = &own;
    loaded->m_ActionPoint[2] = 7.0f;
    LoadCallbacks callbacks;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json(written), ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(loaded->m_OwnerID == 1050);
    OBL_CHECK(loaded->m_finished == 1);
    OBL_CHECK(loaded->m_ActionPoint[0] == 1.0f);
    OBL_CHECK(loaded->m_ActionPoint[1] == 2.5f);
    OBL_CHECK(loaded->m_ActionPoint[2] == 7.0f);
    OBL_CHECK(callbacks.created == "WEIGHTS");
    OBL_REQUIRE(loaded->m_attachedSprite == &callbacks.sprite);
    OBL_CHECK(callbacks.sprite.m_frameIndex == 3);
    callbacks.sprite.m_name = weightsName;
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, WriteContext(), again, error));
    OBL_CHECK(again == written);
}

OBL_TEST_CASE("save_objects_load.table_reads_chairs_plates_and_the_static_layer")
{
    tables::TableDef definition;
    definition.m_NumChairs = 2;
    definition.m_hasPlates = true;
    object::TableChair chairs[2];
    chairs[1].m_ChairOccupier = 1200;
    chairs[1].m_ChairSeated = 1;
    ui::SpriteInstance chairSprites[2]{}, plateSprites[2]{};
    chairSprites[1].m_frameIndex = 5;
    ui::SpriteInstance* chairList[] = {&chairSprites[0], &chairSprites[1]};
    ui::SpriteInstance* plateList[] = {&plateSprites[0], &plateSprites[1]};
    uint8_t drawPlate[] = {0, 1};
    auto table = std::make_unique<object::Furniture>();
    std::strcpy(table->m_Name, "TABLE_CAFE");
    table->m_id = 903;
    table->m_type = object::ObjectType::Table;
    table->m_definition = &definition;
    table->m_chairs = chairs;
    table->m_chairSprites = chairList;
    table->m_plateSprites = plateList;
    table->m_pbDrawPlate = drawPlate;
    table->m_gridW = 2;
    table->m_urgentRepairCalled = 1;
    table->m_WaiterID = 1300;
    ui::SpriteInstance sprite{};
    char spriteName[] = "TABLE";
    sprite.m_name = spriteName;
    table->m_sprite = &sprite;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*table, WriteContext(), written, error));

    object::TableChair loadedChairs[2];
    ui::SpriteInstance loadedChairSprites[2]{}, loadedPlateSprites[2]{};
    ui::SpriteInstance* loadedChairList[] = {&loadedChairSprites[0], &loadedChairSprites[1]};
    ui::SpriteInstance* loadedPlateList[] = {&loadedPlateSprites[0], &loadedPlateSprites[1]};
    uint8_t loadedDrawPlate[] = {0, 0};
    auto loaded = std::make_unique<object::Furniture>();
    loaded->m_type = object::ObjectType::Table;
    loaded->m_definition = &definition;
    loaded->m_chairs = loadedChairs;
    loaded->m_chairSprites = loadedChairList;
    loaded->m_plateSprites = loadedPlateList;
    loaded->m_pbDrawPlate = loadedDrawPlate;
    LoadCallbacks callbacks;
    callbacks.sprite.m_name = spriteName;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json(written), ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(loadedChairs[1].m_ChairOccupier == 1200);
    OBL_CHECK(loadedChairs[1].m_ChairSeated == 1);
    OBL_CHECK(loadedChairSprites[1].m_frameIndex == 5);
    OBL_CHECK(loadedDrawPlate[1] == 1);
    OBL_CHECK(loaded->m_WaiterID == 1300);
    OBL_CHECK(loaded->m_gridW == 2);
    OBL_CHECK(loaded->m_urgentRepairCalled == 1);
    OBL_CHECK(loaded->m_calledRepairing == 0);
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, WriteContext(), again, error));
    OBL_CHECK(again == written);
    for (auto* o : {table.get(), loaded.get()}) {
        o->m_chairs = nullptr;
        o->m_chairSprites = o->m_plateSprites = nullptr;
        o->m_pbDrawPlate = nullptr;
        o->m_definition = nullptr;
    }
}

OBL_TEST_CASE("save_objects_load.scenery_reads_rects_and_calls_draw_before_and_footprint_after_the_base")
{
    auto scenery = std::make_unique<object::Scenery>();
    std::strcpy(scenery->m_Name, "BIN");
    std::strcpy(scenery->m_ObjectName, "Bin_Metal");
    scenery->m_id = 904;
    scenery->m_type = object::ObjectType::Scenery;
    scenery->m_AttachedGuest[0] = -1;
    scenery->m_NumItems = 4;
    scenery->m_ScrXYRect = {1, 2, 30, 40};
    scenery->m_RepairState = 2;
    scenery->m_gridH = 3;
    ui::SpriteInstance sprite{};
    char spriteName[] = "BIN";
    sprite.m_name = spriteName;
    scenery->m_sprite = &sprite;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*scenery, WriteContext(), written, error));

    // Constructed by its creation name with a sprite, as the object manager load routine does.
    auto loaded = std::make_unique<object::Scenery>();
    loaded->m_type = object::ObjectType::Scenery;
    std::strcpy(loaded->m_ObjectName, "Bin_Metal");
    ui::SpriteInstance own{};
    own.m_name = spriteName;
    loaded->m_sprite = &own;
    LoadCallbacks callbacks;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json(written), ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(callbacks.steps == "draw position footprint 3 ");
    OBL_CHECK(std::string(loaded->m_Name) == "bin");
    OBL_CHECK(loaded->m_AttachedGuest[0] == -1);
    OBL_CHECK(loaded->m_NumItems == 4);
    OBL_CHECK(loaded->m_ScrXYRect.right == 30);
    OBL_CHECK(loaded->m_ScrXYRect.bottom == 40);
    OBL_CHECK(loaded->m_RepairState == 2);
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, WriteContext(), again, error));
    OBL_CHECK(again == written);
}

OBL_TEST_CASE("save_objects_load.lifeguard_reads_texts_thoughts_area_and_a_new_attached_sprite")
{
    auto guard = std::make_unique<object::Lifeguard>();
    std::strcpy(guard->m_Name, "Lifeguard");
    guard->m_id = 905;
    guard->m_type = object::ObjectType::Lifeguard;
    std::strcpy(guard->m_CurrAnimation, "SWIM");
    guard->m_BeachArea = 2;
    guard->m_HomeID = 77;
    guard->m_happiness = 0.5f;
    guard->m_Thoughts[1][0] = 0x0105;
    guard->m_Thoughts[1][1] = u'a';
    guard->m_gotTheSack = 1;
    ui::SpriteInstance sprite{};
    char spriteName[] = "GUARD";
    sprite.m_name = spriteName;
    guard->m_sprite = &sprite;
    ui::SpriteInstance buoy{};
    char buoyName[] = "BUOY";
    buoy.m_name = buoyName;
    buoy.m_frameIndex = 3;
    guard->m_attachedSprite = &buoy;
    guard->m_AttachedOffsetY = -4;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*guard, WriteContext(), written, error));

    auto loaded = std::make_unique<object::Lifeguard>();
    loaded->m_type = object::ObjectType::Lifeguard;
    loaded->m_sprite = &sprite;
    ui::SpriteInstance constructed{};
    loaded->m_attachedSprite = &constructed;
    LoadCallbacks callbacks;
    callbacks.sprite.m_name = buoyName;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json(written), ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(callbacks.steps == "area 2 position ");
    OBL_CHECK(callbacks.created == "BUOY");
    OBL_CHECK(loaded->m_attachedSprite == &callbacks.sprite); // always a new one, as the stream Load makes
    OBL_CHECK(callbacks.sprite.m_frameIndex == 3);
    OBL_CHECK(loaded->m_beachArea == &callbacks.area);
    OBL_CHECK(std::string(loaded->m_CurrAnimation) == "SWIM");
    OBL_CHECK(loaded->m_Thoughts[1][0] == 0x0105);
    OBL_CHECK(loaded->m_Thoughts[1][2] == 0);
    OBL_CHECK(loaded->m_HomeID == 77);
    OBL_CHECK(loaded->m_happiness == 0.5f);
    OBL_CHECK(loaded->m_gotTheSack == 1);
    OBL_CHECK(loaded->m_AttachedOffsetY == -4);
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, WriteContext(), again, error));
    OBL_CHECK(again == written);
}

OBL_TEST_CASE("save_objects_load.mechanic_reads_the_class_counter_and_job_mask_bits")
{
    auto loaded = std::make_unique<object::Mechanic>();
    loaded->m_type = object::ObjectType::Mechanic;
    loaded->m_JobMask = 0x102; // vandalism set, an upper bit outside the records
    ui::SpriteInstance own{};
    loaded->m_sprite = &own;
    LoadCallbacks callbacks;
    std::string error;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json::parse(R"({"id":6,"type":"mechanic","mechanic":{"class_count":3,
            "vandalism":false,"breakdown":true,"watering":true},"staff":{"thoughts":["x"]}})"),
            ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(callbacks.steps == "mechanic.class_count=3 position ");
    OBL_CHECK(loaded->m_JobMask == 0x105);
    OBL_CHECK(loaded->m_Thoughts[0][0] == u'x');

    const char* const bad[] = {
            R"({"id":6,"type":"mechanic","mechanic":{"breakdown":256}})",
            R"({"id":6,"type":"mechanic","staff":{"thoughts":[5]}})",
    };
    for (const char* text : bad) {
        OBL_CHECK(!world_save::ReadObjectJson(nlohmann::json::parse(text), ReadContext(), callbacks.Make(), *loaded, error));
        OBL_CHECK(!error.empty());
    }
    OBL_CHECK(world_save::HasObjectReader(static_cast<uint8_t>(object::ObjectType::SnootyStaff)));
}

OBL_TEST_CASE("save_objects_load.missing_keys_read_as_zero_and_an_existing_sprite_is_kept")
{
    auto shark = NewShark();
    shark->m_actionState = 5;
    ui::SpriteInstance own{};
    own.m_fps = 18;
    shark->m_sprite = &own;
    LoadCallbacks callbacks;
    std::string error;
    OBL_REQUIRE(world_save::ReadObjectJson(
            nlohmann::json::parse(R"({"id":4,"kind":"shark","type":"shark"})"), ReadContext(), callbacks.Make(), *shark, error));
    OBL_CHECK(shark->m_actionState == 0);
    OBL_CHECK(std::string(shark->m_Name) == "shark");
    OBL_CHECK(shark->m_sprite == &own);
    OBL_CHECK(own.m_fps == 0);
    OBL_CHECK(callbacks.created.empty());
    OBL_CHECK(shark->m_pathCursor == nullptr);
}

OBL_TEST_CASE("save_objects_load.refuses_values_the_object_cannot_hold")
{
    const char* const bad[] = {
            R"({"id":4,"type":"shark","shark":{"action_state":1.5}})",
            R"({"id":4,"type":"shark","base":{"visible":256}})",
            R"({"id":4,"type":"shark","pos":[1,2]})",
            R"({"id":4,"type":"a_key_longer_than_the_thirty_two_bytes_of_the_name"})",
            R"({"id":4,"type":"shark","moving":{"path":{"steps":5}}})",
    };
    for (const char* text : bad) {
        auto shark = NewShark();
        LoadCallbacks callbacks;
        std::string error;
        OBL_CHECK(!world_save::ReadObjectJson(nlohmann::json::parse(text), ReadContext(), callbacks.Make(), *shark, error));
        OBL_CHECK(!error.empty());
        delete shark->m_pathCursor;
    }
    OBL_CHECK(world_save::HasObjectReader(static_cast<uint8_t>(object::ObjectType::Shark)));
    OBL_CHECK(!world_save::HasObjectReader(uint8_t{0xff}));
}

OBL_TEST_CASE("save_objects_load.building_reads_sprites_rotation_catalog_keys_and_hands_on_its_parts")
{
    auto building = std::make_unique<object::Building>();
    std::strcpy(building->m_Name, "Bar");
    building->m_id = 12;
    building->m_type = object::ObjectType::Building;
    tables::BuildingDef def{};
    def.m_NumAnimSprites = 1;
    building->m_definition = &def;
    ui::SpriteInstance anim{};
    anim.m_frameIndex = 3;
    building->m_animSprites[0] = &anim;
    ui::SpriteInstance sprite{};
    char spriteName[] = "BAR";
    sprite.m_name = spriteName;
    sprite.m_direction = 2;
    building->m_sprite = &sprite;
    building->m_BeerType = 1;
    building->m_burn = 1; // what a load of a finished building makes of it
    building->m_staff_busy[3] = 1;
    building->m_profitRecord[63] = 2.5f;
    world_save::JsonContext write = WriteContext();
    write.beerKey = [](int32_t id) { return id == 1 ? std::string("lager") : std::string(); };
    write.eventKey = [](int32_t id) { return id == 0 ? std::string("karaoke") : std::string(); };
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*building, write, written, error));

    nlohmann::json in(written);
    in["building"]["events"] = nlohmann::json::parse(R"({"owner":12,"active":true,"slots":[{"event":"karaoke","actor_count":2,
            "actors":[40,41],"guest_count":0,"guests":[],"start":20.0,"duration":2.0,"price":5.0},{"event":null},{}]})");
    in["building"]["thoughts"] = nlohmann::json::parse(R"([{"object":7,"text":"warm"}])");
    in["building"]["queue"] = nlohmann::json::parse(R"({"owner":12,"max_length":4,"serve_point":{"x":1.0,"y":2.0},"guests":[300],
            "commodities":[1],"tail":{"x":0.0,"y":0.0}})");
    world_save::ReadContext read = ReadContext();
    read.beerId = [](std::string_view key) { return key == "lager" ? 1 : -1; };
    read.eventId = [](std::string_view key) { return key == "karaoke" ? 0 : -1; };
    auto loaded = std::make_unique<object::Building>();
    loaded->m_type = object::ObjectType::Building;
    loaded->m_definition = &def;
    ui::SpriteInstance loadedAnim{};
    loaded->m_animSprites[0] = &loadedAnim;
    ui::SpriteInstance loadedSprite{};
    loadedSprite.m_name = spriteName;
    loaded->m_sprite = &loadedSprite;
    LoadCallbacks callbacks;
    world_save::ObjectLoadCallbacks made = callbacks.Make();
    world_save::BuildingEventsJson events;
    world_save::BuildingQueueJson queue;
    std::vector<world_save::BuildingThoughtJson> thoughts;
    made.buildingEvents = [&](object::Building&, const world_save::BuildingEventsJson& e) { events = e; };
    made.buildingQueue = [&](object::Building&, const world_save::BuildingQueueJson& q) { queue = q; };
    made.buildingThoughts = [&](object::Building&, const std::vector<world_save::BuildingThoughtJson>& t) { thoughts = t; };
    OBL_REQUIRE(world_save::ReadObjectJson(in, read, made, *loaded, error));
    OBL_CHECK(loaded->m_BeerType == 1);
    OBL_CHECK(loadedAnim.m_frameIndex == 3);
    OBL_CHECK(loadedSprite.m_direction == 2);
    OBL_CHECK(loaded->m_burn == 1);
    OBL_CHECK(loaded->m_staff_busy[3] == 1);
    OBL_CHECK(loaded->m_profitRecord[63] == 2.5f);
    OBL_CHECK(events.owner == 12);
    OBL_CHECK(events.active == 1);
    OBL_CHECK(events.slots[0].event == 0);
    OBL_CHECK((events.slots[0].actors == std::vector<int32_t>{40, 41}));
    OBL_CHECK(events.slots[0].price == 5.0f);
    OBL_CHECK(events.slots[1].event == -1);
    OBL_CHECK(events.slots[2].event == -1);
    OBL_CHECK(queue.maxLength == 4);
    OBL_CHECK(queue.servePoint[1] == 2.0f);
    OBL_CHECK(queue.guests == std::vector<int32_t>{300});
    OBL_CHECK(queue.commodities == std::vector<int32_t>{1});
    OBL_REQUIRE(thoughts.size() == 1);
    OBL_CHECK(thoughts[0].object == 7);
    OBL_CHECK(thoughts[0].text == u"warm");
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, write, again, error));
    OBL_CHECK(again == written);

    const char* const bad[] = {
            R"({"id":12,"type":"bar","building":{"beer":"stout"}})",
            R"({"id":12,"type":"bar","building":{"events":{"slots":[{"event":"opera"},{},{}]}}})",
            R"({"id":12,"type":"bar","building":{"staff_busy":[1,2]}})",
            R"({"id":12,"type":"bar","building":{"thoughts":[{"object":1,"text":5}]}})",
            R"({"id":12,"type":"bar","dir":"up"})",
    };
    for (const char* text : bad) {
        OBL_CHECK(!world_save::ReadObjectJson(nlohmann::json::parse(text), read, made, *loaded, error));
        OBL_CHECK(!error.empty());
    }
}

OBL_TEST_CASE("save_objects_load.object_headers_give_the_type_byte_and_the_creation_name")
{
    struct Case {
        const char* entry;
        uint8_t type;
        const char* name;
    };
    const Case cases[] = {
            {R"({"id":1,"kind":"scenery","type":"palm","created_as":"palm01"})", 1, "palm01"},
            {R"({"id":2,"kind":"building","type":"bar"})", 2, "bar"},
            {R"({"id":3,"kind":"guest","type":"guest"})", 3, "guest"},
            {R"({"id":4,"kind":"staff","type":"waiter","class":"waiter","service":{}})", 19, "waiter"},
            {R"({"id":5,"kind":"staff","type":"barman","class":"service","service":{}})", 6, "barman"},
            {R"({"id":6,"kind":"staff","type":"lg","class":"lifeguard","lifeguard":{}})", 7, "lg"},
            {R"({"id":7,"kind":"staff","type":"guard","class":"security","security":{}})", 12, "guard"},
            {R"({"id":8,"kind":"performer","type":"weightlifter","class":"weightlifter"})", 16, "weightlifter"},
            {R"({"id":9,"kind":"animal","type":"shark"})", 13, "shark"},
    };
    for (const Case& c : cases) {
        uint8_t type = 0;
        std::string name, error;
        OBL_CHECK(world_save::ReadObjectHeaderJson(nlohmann::json::parse(c.entry), ReadContext(), type, name, error));
        OBL_CHECK(type == c.type);
        OBL_CHECK(name == c.name);
    }
    for (const char* bad : {R"({"id":1,"kind":"rock","type":"x"})", R"({"id":1,"kind":"staff","type":"x"})", R"({"kind":"guest"})"}) {
        uint8_t type = 0;
        std::string name, error;
        OBL_CHECK(!world_save::ReadObjectHeaderJson(nlohmann::json::parse(bad), ReadContext(), type, name, error));
        OBL_CHECK(!error.empty());
    }
}

OBL_TEST_CASE("save_objects_load.object_manager_block_from_the_document")
{
    const auto objects = nlohmann::json::parse(R"([{"id":7,"kind":"building","type":"bar"},{"id":3,"kind":"guest","type":"g"},
            {"id":12,"kind":"guest","type":"g"},{"id":5,"kind":"animal","type":"shark"}])");
    object_manager::SavedHeader h;
    std::string error;
    OBL_REQUIRE(
            world_save::ReadObjectManagerJson(nlohmann::json::parse(R"({"next_id":40,"keyline":true})"), objects, ReadContext(), h, error));
    OBL_CHECK(h.active == 4);
    OBL_CHECK(h.buildings == 1);
    OBL_CHECK(h.guests == 2);
    OBL_CHECK(h.others == 1);
    OBL_REQUIRE(h.ids.size() == object::kSavedIdCount);
    OBL_CHECK(h.ids[0] == 7);
    OBL_CHECK(h.ids[1] == 0xFFFFFFFFu);
    OBL_CHECK(h.ids[object_manager::kGuestBase] == 3);
    OBL_CHECK(h.ids[object_manager::kGuestBase + 1] == 12);
    OBL_CHECK(h.ids[object_manager::kOtherBase] == 5);
    OBL_CHECK(h.nextId == 40);
    OBL_CHECK(h.keyline == 1);
    // Without next_id: one past the largest id.
    OBL_REQUIRE(world_save::ReadObjectManagerJson(nlohmann::json::object(), objects, ReadContext(), h, error));
    OBL_CHECK(h.nextId == 13);
    OBL_CHECK(h.keyline == 0);
    // Another order than buildings, guests, others; bad values.
    const auto unordered = nlohmann::json::parse(R"([{"id":3,"kind":"guest","type":"g"},{"id":7,"kind":"building","type":"bar"}])");
    OBL_CHECK(!world_save::ReadObjectManagerJson(nlohmann::json::object(), unordered, ReadContext(), h, error));
    OBL_CHECK(!world_save::ReadObjectManagerJson(nlohmann::json::parse(R"({"next_id":-1})"), objects, ReadContext(), h, error));
    OBL_CHECK(!world_save::ReadObjectManagerJson(nlohmann::json::parse(R"({"keyline":1})"), objects, ReadContext(), h, error));
}

OBL_TEST_CASE("save_states.names_round_trip_and_other_values_stay_numbers")
{
    using obl::formats::SaveStateSet;
    OBL_CHECK(obl::formats::SaveStateSetOf("building.open_status") == SaveStateSet::BuildingOpen);
    OBL_CHECK(obl::formats::SaveStateSetOf("Weather.previous_state") == SaveStateSet::Weather);
    OBL_CHECK(obl::formats::SaveStateSetOf("guest.action_state") == SaveStateSet::None);
    OBL_CHECK(obl::formats::SaveStateValue(SaveStateSet::Weather, 5) == "thunderstorm");
    OBL_CHECK(obl::formats::SaveStateValue(SaveStateSet::VehicleType, 0) == "jetski");
    OBL_CHECK(obl::formats::SaveStateValue(SaveStateSet::Weather, 6) == 6);
    OBL_CHECK(obl::formats::SaveStateValue(SaveStateSet::BuildingWear, -1) == -1);
    OBL_CHECK(*obl::formats::SaveStateNumber(SaveStateSet::BuildingWear, nlohmann::json("broken")) == 3);
    OBL_CHECK(*obl::formats::SaveStateNumber(SaveStateSet::BuildingWear, nlohmann::json(77)) == 77);
    OBL_CHECK(!obl::formats::SaveStateNumber(SaveStateSet::BuildingWear, nlohmann::json("shiny")));

    const obl::formats::SaveObjectStates states{{1, "STATE_STARTUP"}};
    OBL_CHECK(obl::formats::SaveStateValue(SaveStateSet::ObjectState, 1, &states) == "STATE_STARTUP");
    OBL_CHECK(obl::formats::SaveStateValue(SaveStateSet::ObjectState, 9, &states) == 9);
    OBL_CHECK(*obl::formats::SaveStateNumber(SaveStateSet::ObjectState, nlohmann::json("STATE_STARTUP"), &states) == 1);
    OBL_CHECK(!obl::formats::SaveStateNumber(SaveStateSet::ObjectState, nlohmann::json("STATE_STARTUP")));
}
