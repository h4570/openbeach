#include "core/char16.h"
#include "unit_test.h"
#include "game/objects/building_events_state.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/object_building.h"
#include "game/map/path_data.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_performers.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_staff.h"
#include "game/game/game_context.h"
#include "game/objects/object_table.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"
#include "game/ui/sprite.h"
#include "game/world/save_objects_json.h"

#include <cstring>
#include <memory>
#include <string>

using namespace obl;

namespace {

world_save::JsonContext Context()
{
    world_save::JsonContext context;
    context.narrowToUtf8 = [](std::string_view text) { return std::string(text); };
    return context;
}

} // namespace

OBL_TEST_CASE("save_objects_json.shark_writes_identity_class_moving_and_base_in_record_order")
{
    auto shark = std::make_unique<object::Shark>();
    std::strcpy(shark->m_Name, "Shark1");
    shark->m_id = 900;
    shark->m_type = object::ObjectType::Shark;
    shark->m_WorldXYZ.x() = 64.5f;
    shark->m_SwimCircleRect[1] = -2;
    shark->m_actionState = 3;
    shark->m_buildingTraversal = 0xa5; // a byte flag, kept as its value
    ui::SpriteInstance sprite{};
    char spriteName[] = "SHARK";
    sprite.m_name = spriteName;
    sprite.m_direction = 2;
    sprite.m_lit = 1;
    shark->m_sprite = &sprite;
    auto route = std::make_shared<path::Route>();
    route->m_steps = {{1, 2}, {3, 4}};
    path::RouteCursor cursor{route, 1, 1};
    shark->m_pathCursor = &cursor;

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*shark, Context(), out, error));
    shark->m_pathCursor = nullptr;
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("animal"));
    OBL_CHECK_EQ(out["type"].get<std::string>(), std::string("shark1"));
    OBL_CHECK(!out.contains("name"));
    OBL_CHECK(!out.contains("created_as"));
    OBL_CHECK_EQ(out["pos"].dump(), std::string("[64.5,0.0,0.0]"));
    OBL_CHECK_EQ(out["shark"]["swim_rect"].dump(), std::string("[0,4294967294,0,0]"));
    OBL_CHECK_EQ(out["shark"]["action_state"].get<int>(), 3);
    OBL_CHECK_EQ(out["moving"]["building_traversal"].get<int>(), 0xa5);
    OBL_CHECK_EQ(out["moving"]["path"]["steps"].dump(), std::string("[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}]"));
    OBL_CHECK(out["moving"]["path"]["reverse"].get<bool>());
    OBL_CHECK_EQ(out["base"]["sprite"].get<std::string>(), std::string("SHARK"));
    OBL_CHECK_EQ(out["sprite"]["dir"].get<int>(), 2);
    OBL_CHECK(out["sprite"]["lit"].get<bool>());
    std::string keys;
    for (auto it = out.begin(); it != out.end(); ++it) keys += it.key() + " ";
    OBL_CHECK_EQ(keys, std::string("id kind type pos shark moving base sprite "));

    shark->m_visible = 2;
    OBL_CHECK(!world_save::WriteObjectJson(*shark, Context(), out, error));
    OBL_CHECK(error.find("base.visible: bool value 2") != std::string::npos);
}

OBL_TEST_CASE("save_objects_json.vehicle_writes_guests_way_points_and_present_sprites")
{
    auto vehicle = std::make_unique<object::Vehicle>();
    std::strcpy(vehicle->m_Name, "ferry");
    vehicle->m_id = 901;
    vehicle->m_type = object::ObjectType::Vehicle;
    vehicle->m_Guests[0] = -1;
    vehicle->m_Guests[299] = 77;
    vehicle->m_DestroyPoint = 200;
    vehicle->m_WayPoints[63][2] = 1.5f;
    vehicle->m_vehicleType = 2;
    ui::SpriteInstance sprite{};
    char spriteName[] = "FERRY";
    sprite.m_name = spriteName;
    ui::SpriteInstance door{};
    door.m_frameIndex = 4;
    vehicle->m_sprite = &sprite;
    vehicle->m_vehicleSprite = &sprite;
    vehicle->m_doorSprite = &door;

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*vehicle, Context(), out, error));
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("vehicle"));
    OBL_CHECK(!out.contains("name"));
    const nlohmann::ordered_json& v = out["vehicle"];
    OBL_CHECK_EQ(v["guests"].size(), size_t(300));
    OBL_CHECK_EQ(v["guests"][0].get<int>(), -1);
    OBL_CHECK_EQ(v["guests"][299].get<int>(), 77);
    OBL_CHECK_EQ(v["destroy_point"].get<int>(), 200);
    OBL_CHECK_EQ(v["way_points"].size(), size_t(64));
    OBL_CHECK_EQ(v["way_points"][63]["z"].dump(), std::string("1.5"));
    OBL_CHECK_EQ(v["sprites"].size(), size_t(2));
    OBL_CHECK_EQ(v["sprites"][1]["frame"].get<int>(), 4);
    OBL_CHECK_EQ(v["vehicle_type"].get<std::string>(), std::string("ferry"));
    std::string keys;
    for (auto it = out.begin(); it != out.end(); ++it) keys += it.key() + " ";
    OBL_CHECK_EQ(keys, std::string("id kind type pos vehicle moving base sprite "));
}

OBL_TEST_CASE("save_objects_json.singer_writes_header_action_point_and_attached_sprite")
{
    auto singer = std::make_unique<object::Singer>();
    std::strcpy(singer->m_Name, "Singer");
    singer->m_id = 902;
    singer->m_type = object::ObjectType::Singer;
    singer->m_OwnerID = 1050;
    singer->m_ActionPoint[1] = 2.5f;
    ui::SpriteInstance sprite{};
    char spriteName[] = "SINGER";
    sprite.m_name = spriteName;
    singer->m_sprite = &sprite;

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*singer, Context(), out, error));
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("performer"));
    OBL_CHECK_EQ(out["performer"]["owner"].get<int>(), 1050);
    OBL_CHECK_EQ(out["performer"]["action_point"]["z"].dump(), std::string("2.5")); // written from the y word
    OBL_CHECK(!out["performer"].contains("attached"));

    ui::SpriteInstance microphone{};
    char microphoneName[] = "MIC";
    microphone.m_name = microphoneName;
    microphone.m_frameIndex = 3;
    singer->m_attachedSprite = &microphone;
    OBL_REQUIRE(world_save::WriteObjectJson(*singer, Context(), out, error));
    const nlohmann::ordered_json& attached = out["performer"]["attached"];
    OBL_CHECK_EQ(attached["sprite"].get<std::string>(), std::string("MIC"));
    OBL_CHECK_EQ(attached["frame"].get<int>(), 3);
    OBL_CHECK_EQ(attached.begin().key(), std::string("sprite"));
}

OBL_TEST_CASE("save_objects_json.table_writes_chairs_plates_then_the_static_layer")
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
    ui::SpriteInstance sprite{};
    char spriteName[] = "TABLE";
    sprite.m_name = spriteName;
    table->m_sprite = &sprite;

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*table, Context(), out, error));
    table->m_chairs = nullptr;
    table->m_chairSprites = table->m_plateSprites = nullptr;
    table->m_pbDrawPlate = nullptr;
    table->m_definition = nullptr;
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("table"));
    OBL_CHECK_EQ(out["type"].get<std::string>(), std::string("table_cafe"));
    const nlohmann::ordered_json& t = out["table"];
    OBL_CHECK_EQ(t["chairs"].size(), size_t(2));
    OBL_CHECK_EQ(t["chairs"][1]["occupier"].get<int>(), 1200);
    OBL_CHECK(t["chairs"][1]["seated"].get<bool>());
    OBL_CHECK_EQ(t["chairs"][1]["sprite"]["frame"].get<int>(), 5);
    OBL_CHECK_EQ(t["plates"].size(), size_t(2));
    OBL_CHECK(t["plates"][1]["draw"].get<bool>());
    OBL_CHECK_EQ(out["static"]["grid_w"].get<int>(), 2);
    OBL_CHECK(!out["static"]["called_repairing"].get<bool>());
    OBL_CHECK(out["static"]["urgent_repairing"].get<bool>());
    std::string keys;
    for (auto it = out.begin(); it != out.end(); ++it) keys += it.key() + " ";
    OBL_CHECK_EQ(keys, std::string("id kind type pos table static base sprite "));
}

OBL_TEST_CASE("save_objects_json.scenery_writes_its_creation_name_rects_then_the_static_layer")
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

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*scenery, Context(), out, error));
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("scenery"));
    OBL_CHECK_EQ(out["type"].get<std::string>(), std::string("bin"));
    OBL_CHECK_EQ(out["created_as"].get<std::string>(), std::string("bin_metal"));
    OBL_CHECK(!out.contains("created_name"));
    const nlohmann::ordered_json& s = out["scenery"];
    OBL_CHECK_EQ(s["attached_guests"].dump(), std::string("[-1,0]"));
    OBL_CHECK_EQ(s["items"].get<int>(), 4);
    OBL_CHECK_EQ(s["repair_state"].get<int>(), 2);
    OBL_CHECK_EQ(s["screen_rect"].dump(), std::string(R"({"left":1,"top":2,"right":30,"bottom":40})"));
    OBL_CHECK_EQ(out["static"]["grid_h"].get<int>(), 3);
    std::string keys;
    for (auto it = out.begin(); it != out.end(); ++it) keys += it.key() + " ";
    OBL_CHECK_EQ(keys, std::string("id kind type pos created_as scenery static base sprite "));
}

OBL_TEST_CASE("save_objects_json.lifeguard_writes_texts_attached_sprite_staff_then_moving")
{
    auto guard = std::make_unique<object::Lifeguard>();
    std::strcpy(guard->m_Name, "Lifeguard");
    guard->m_id = 905;
    guard->m_type = object::ObjectType::Lifeguard;
    std::strcpy(guard->m_CurrAnimation, "SWIM");
    guard->m_HomeID = 77;
    guard->m_Thoughts[1][0] = 0x0105; // a Polish letter, written as UTF-8
    guard->m_Thoughts[1][1] = u'a';
    ui::SpriteInstance sprite{};
    char spriteName[] = "GUARD";
    sprite.m_name = spriteName;
    guard->m_sprite = &sprite;
    ui::SpriteInstance buoy{};
    char buoyName[] = "BUOY";
    buoy.m_name = buoyName;
    guard->m_attachedSprite = &buoy;
    guard->m_targetHappiness = 0.75f; // not in the old record: written after the staff records
    guard->m_happinessChanging = 1;

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*guard, Context(), out, error));
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("staff"));
    OBL_CHECK_EQ(out["lifeguard"]["current_animation"].get<std::string>(), std::string("SWIM"));
    OBL_CHECK_EQ(out["lifeguard"]["return_animation"].get<std::string>(), std::string(""));
    OBL_CHECK_EQ(out["lifeguard"]["attached"]["sprite"].get<std::string>(), std::string("BUOY"));
    OBL_CHECK_EQ(out["staff"]["home"].get<int>(), 77);
    OBL_CHECK_EQ(out["staff"]["thoughts"].dump(),
            std::string("[\"\",\"\xc4\x85"
                        "a\",\"\"]"));
    std::string keys;
    for (auto it = out.begin(); it != out.end(); ++it) keys += it.key() + " ";
    OBL_CHECK_EQ(keys, std::string("id kind type class pos lifeguard staff moving base sprite "));
    const nlohmann::ordered_json& staff = out["staff"];
    OBL_CHECK_EQ(staff["target_happiness"].dump(), std::string("0.75"));
    OBL_CHECK_EQ(staff["happiness_rate"].dump(), std::string("0.0"));
    OBL_CHECK(staff["happiness_changing"].get<bool>());
    auto last = staff.end();
    OBL_CHECK_EQ((--last).key(), std::string("happiness_changing"));
}

OBL_TEST_CASE("save_objects_json.mechanic_writes_the_class_counter_and_job_mask_bits")
{
    auto mechanic = std::make_unique<object::Mechanic>();
    std::strcpy(mechanic->m_Name, "Mechanic");
    mechanic->m_id = 906;
    mechanic->m_type = object::ObjectType::Mechanic;
    mechanic->m_JobMask = 0b101; // breakdown and watering
    ui::SpriteInstance sprite{};
    char spriteName[] = "MECH";
    sprite.m_name = spriteName;
    mechanic->m_sprite = &sprite;
    game::GetContext().staffMechanicCount = 3;

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*mechanic, Context(), out, error));
    OBL_CHECK_EQ(out["mechanic"]["class_count"].get<int>(), 3);
    OBL_CHECK_EQ(out["mechanic"]["breakdown"].get<bool>(), true);
    OBL_CHECK_EQ(out["mechanic"]["vandalism"].get<bool>(), false);
    OBL_CHECK_EQ(out["mechanic"]["watering"].get<bool>(), true);
    game::GetContext().staffMechanicCount = 0;

    mechanic->m_JobMask = 0x200; // bit 1 shifted keeps the upper byte: not a bool
    OBL_CHECK(!world_save::WriteObjectJson(*mechanic, Context(), out, error));
    OBL_CHECK(error.find("vandalism") != std::string::npos);
}

OBL_TEST_CASE("save_objects_json.guest_writes_name_arrays_and_catalog_building_keys")
{
    auto guest = std::make_unique<object::Guest>();
    std::strcpy(guest->m_Name, "Guest");
    guest->m_id = 907;
    guest->m_type = object::ObjectType::Guest;
    Char16 name[] = u"Ann";
    guest->m_name = name;
    guest->m_opinion[2] = 0.5f;
    guest->m_lastActivity[1] = 200;
    guest->m_BestType = 4;
    guest->m_WorstType = -1;
    guest->m_CurTypeID = 0;
    guest->m_DayArrived = 6;
    guest->m_topless = 1; // not in the old record: written after the guest records
    guest->m_NumTimesBlocked = 3;
    guest->m_moodIconTime = 2.0f;
    guest->m_Worst = 5;
    std::strcpy(guest->m_WalkAnimation, "WALK");
    ui::SpriteInstance sprite{};
    char spriteName[] = "GUEST";
    sprite.m_name = spriteName;
    guest->m_sprite = &sprite;
    world_save::JsonContext context = Context();
    context.buildingKey = [](int32_t id) { return id == 4 ? std::string("bar") : id == 0 ? std::string("hotel") : std::string(); };

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*guest, context, out, error));
    const nlohmann::ordered_json& g = out["guest"];
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("guest"));
    OBL_CHECK_EQ(g["name"].get<std::string>(), std::string("Ann"));
    OBL_CHECK_EQ(g["opinion"].size(), size_t(7));
    OBL_CHECK_EQ(g["opinion"][2].dump(), std::string("0.5"));
    OBL_CHECK_EQ(g["last_activity"].dump(), std::string("[0,200]"));
    OBL_CHECK_EQ(g["best_type"].get<std::string>(), std::string("bar"));
    OBL_CHECK(g["worst_type"].is_null());
    OBL_CHECK_EQ(g["current_type"].get<std::string>(), std::string("hotel"));
    OBL_CHECK_EQ(g["day_arrived"].get<int>(), 6);
    OBL_CHECK_EQ(g["walk_animation"].get<std::string>(), std::string("WALK"));
    OBL_CHECK_EQ(g["topless"].get<int>(), 1);
    OBL_CHECK_EQ(g["times_blocked"].get<int>(), 3);
    OBL_CHECK_EQ(g["mood_icon_time"].dump(), std::string("2.0"));
    OBL_CHECK_EQ(g["worst"].get<int>(), 5);
    auto last = g.end();
    OBL_CHECK_EQ((--last).key(), std::string("worst"));
    guest->m_name = nullptr;

    guest->m_BestType = 9; // no catalog entry
    OBL_CHECK(!world_save::WriteObjectJson(*guest, context, out, error));
    OBL_CHECK(error.find("best_type") != std::string::npos);
}

OBL_TEST_CASE("save_objects_json.building_writes_sprites_schedule_thoughts_queue_and_its_rotation")
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

    object::BuildingEvents events{};
    events.m_OwnerID = 12;
    events.m_slots[0].m_EventID = 0;
    events.m_slots[1].m_EventID = -1;
    events.m_slots[2].m_EventID = -1;
    events.m_slots[0].m_actors = {40, 41};
    events.m_slots[0].m_start = 20.0f;
    building->m_events = &events;

    object::ThoughtEntry thought{const_cast<Char16*>(u"warm"), 7};
    building->m_ThoughtList = {&thought};

    object::BuildingQueue queue{};
    queue.m_ownerId = 12;
    queue.m_maxLength = 4;
    queue.m_qlQueue.push_back(300);
    float points[10] = {1.0f, 2.0f, 1.5f, 2.0f, 2.0f, 2.5f, 2.5f, 3.0f, 3.0f, 3.0f}; // limit + 1 stand points, bent
    queue.m_points = points;
    queue.m_tail[0] = 1.5f;
    queue.m_tail[1] = 2.0f;
    building->m_queue = &queue;

    world_save::JsonContext context = Context();
    context.beerKey = [](int32_t id) { return id == 1 ? std::string("lager") : std::string(); };
    context.eventKey = [](int32_t id) { return id == 0 ? std::string("karaoke") : std::string(); };
    context.underConstruction = [](int32_t id) { return id == 12; };

    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*building, context, out, error));
    const nlohmann::ordered_json& b = out["building"];
    OBL_CHECK_EQ(out["kind"].get<std::string>(), std::string("building"));
    OBL_CHECK_EQ(b["beer"].get<std::string>(), std::string("lager"));
    OBL_CHECK_EQ(b["sprites"].size(), size_t(1));
    OBL_CHECK_EQ(b["sprites"][0]["frame"].get<int>(), 3);
    OBL_CHECK(b["under_construction"].get<bool>());
    OBL_CHECK(!b["under_demolition"].get<bool>());
    const nlohmann::ordered_json& slots = b["events"]["slots"];
    OBL_CHECK_EQ(slots.size(), size_t(3));
    OBL_CHECK_EQ(slots[0]["event"].get<std::string>(), std::string("karaoke"));
    OBL_CHECK(slots[1]["event"].is_null());
    OBL_CHECK_EQ(slots[0]["actors"].dump(), std::string("[40,41]"));
    OBL_CHECK_EQ(slots[0]["guests"].dump(), std::string("[]"));
    OBL_CHECK_EQ(slots[0]["start"].dump(), std::string("20.0"));
    OBL_CHECK_EQ(b["thoughts"].dump(), std::string(R"([{"object":7,"text":"warm"}])"));
    OBL_CHECK_EQ(b["queue"]["max_length"].get<int>(), 4);
    OBL_CHECK_EQ(b["queue"]["guests"].dump(), std::string("[300]"));
    OBL_CHECK_EQ(b["queue"]["commodities"].dump(), std::string("[]"));
    // The tail and the points table follow the saved records; a limit outside 1..10 has no table.
    OBL_CHECK_EQ(b["queue"]["tail"].dump(), std::string(R"({"x":1.5,"y":2.0})"));
    OBL_CHECK_EQ(b["queue"]["points"].size(), size_t(5));
    OBL_CHECK_EQ(b["queue"]["points"][2].dump(), std::string(R"({"x":2.0,"y":2.5})"));
    queue.m_maxLength = 11;
    OBL_REQUIRE(world_save::WriteObjectJson(*building, context, out, error));
    OBL_CHECK(!out["building"]["queue"].contains("points"));
    queue.m_maxLength = 4;
    // The rotation leaves the sprite for the object's last key, as the converter writes it.
    OBL_CHECK(!out["sprite"].contains("dir"));
    OBL_CHECK_EQ(out.back().get<std::string>(), std::string("s"));
    OBL_CHECK_EQ((--out.end()).key(), std::string("dir"));

    sprite.m_direction = 4;
    OBL_CHECK(!world_save::WriteObjectJson(*building, context, out, error));
    OBL_CHECK(error.find("rotation") != std::string::npos);
    sprite.m_direction = 2;
    building->m_BeerType = 5; // no catalog entry
    OBL_CHECK(!world_save::WriteObjectJson(*building, context, out, error));
    OBL_CHECK(error.find("beer") != std::string::npos);
    building->m_events = nullptr;
    building->m_queue = nullptr;
    building->m_ThoughtList.clear();
}
