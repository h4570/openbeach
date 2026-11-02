#include "game/weather/weather_state.h"
// The scenario interpreter's hand-over commands (src/game/world/world_script_delegate.h): the payloads' translation into the managers' arguments.

#include "unit_test.h"

#include "game/world/world_script_delegate.h"
#include "game/messages/message_state.h"
#include "game/world/world_script_types.h"

#include <bit>
#include <cstring>

using namespace obl::script;

OBL_TEST_CASE("world_script_delegate.circuit_point_uses_the_kind_as_the_circuit")
{
    const obl::world_script::WorldScriptCircuitData data{11, 22, 1};
    const CircuitCall call = CircuitPointCall(data);
    OBL_CHECK_EQ(call.circuit, 1);
    OBL_CHECK_EQ(call.x, 11);
    OBL_CHECK_EQ(call.y, 22);
}

OBL_TEST_CASE("world_script_delegate.boat_event_swaps_counts_and_flags_into_the_managers_record")
{
    obl::world_script::WorldScriptBoatEventData data{};
    data.m_hour = 7.5f;
    data.m_day = 3;
    data.m_timeFlags = 2;
    data.m_guestInfo = 5;
    data.m_number = 9;
    std::memset(data.m_keywords, 0xcd, sizeof(data.m_keywords)); // stack garbage behind the terminators
    std::strcpy(data.m_keywords[0], "MSunbather");
    std::strcpy(data.m_keywords[19], "");
    for (int i = 0; i < 20; ++i) {
        data.m_counts[i] = 100 + i;
        data.m_random[i] = 200 + i;
    }
    obl::ferry::Boat record = BoatEventRecord(data);
    OBL_CHECK_EQ(record.GuestInfo, 5);
    OBL_CHECK_EQ(record.NumGuests, 9);
    OBL_CHECK_EQ(record.Time, 7.5f);
    OBL_CHECK_EQ(record.Day, 3);
    OBL_CHECK_EQ(record.TimeFlags, 2);
    OBL_CHECK_EQ(record.GuestType[4], 204); // the parser's m_random
    OBL_CHECK_EQ(record.GuestKeywordNumbers[4], 104); // the parser's m_counts
    OBL_CHECK(std::strcmp(record.GuestKeyword[0], "MSunbather") == 0);
    OBL_CHECK_EQ(record.GuestKeyword[0][11], 0); // zero behind the terminator
    OBL_CHECK_EQ(record.GuestKeyword[19][0], 0);

    obl::ferry::Boat garbage;
    std::memset(&garbage, 0xcd, sizeof(garbage));
    std::memcpy(&garbage, &record, offsetof(obl::ferry::Boat, GuestKeyword));
    for (int i = 0; i < 20; ++i) std::memcpy(garbage.GuestKeyword[i], record.GuestKeyword[i], std::strlen(record.GuestKeyword[i]) + 1);
    std::memcpy(garbage.GuestKeywordNumbers, record.GuestKeywordNumbers, sizeof(record.GuestKeywordNumbers));
    garbage.NumGuests = record.NumGuests;
    garbage.Time = record.Time;
    garbage.Day = record.Day;
    garbage.TimeFlags = record.TimeFlags;
    OBL_CHECK(std::memcmp(&garbage, &record, sizeof(record)) != 0);
    NormalizeBoatEvent(garbage);
    OBL_CHECK(std::memcmp(&garbage, &record, sizeof(record)) == 0);
}

OBL_TEST_CASE("world_script_delegate.disaster_keeps_the_payload_words_and_the_owned_target")
{
    obl::world_script::WorldScriptDisasterData data{};
    data.m_kind = 3;
    data.m_rectangle[0] = 1;
    data.m_rectangle[3] = 4;
    data.m_duration = 2.5f;
    data.m_hour = 6.0f;
    data.m_day = 12;
    data.m_scale = 7;
    data.m_value = 0.75f;
    data.m_beer = 2;
    data.m_replacementBeer = 4;
    data.m_strength = 0.5f;
    data.m_target = const_cast<char*>("HOTEL");
    const obl::disaster::Event event = DisasterEventRecord(data);
    OBL_CHECK_EQ(event.type, 3);
    OBL_CHECK_EQ(event.rect[0], 1);
    OBL_CHECK_EQ(event.rect[3], 4);
    OBL_CHECK_EQ(event.duration, 2.5f);
    OBL_CHECK_EQ(event.time, 6.0f);
    OBL_CHECK_EQ(event.day, 12u);
    OBL_CHECK_EQ(event.scale, 7);
    OBL_CHECK_EQ(event.disasterValue, 0.75f);
    OBL_CHECK_EQ(event.beerType, 2);
    OBL_CHECK_EQ(event.replacementBeer, 4);
    OBL_CHECK_EQ(event.replacementStrength, 0.5f);
    OBL_CHECK(event.target == std::optional<std::string>("HOTEL"));
    data.m_target = nullptr;
    OBL_CHECK(!DisasterEventRecord(data).target.has_value());
}

OBL_TEST_CASE("world_script_delegate.weather_entry_is_the_payload")
{
    obl::world_script::WorldScriptWeatherData data{};
    data.m_kind = 4;
    data.m_hour = 13.5f;
    data.m_day = 2;
    for (int i = 0; i < 26; ++i) data.m_modifiers[i] = 0.1f * i;
    const obl::weather::WeatherEntry entry = WeatherEntryRecord(data);
    OBL_CHECK_EQ(entry.m_kind, 4);
    OBL_CHECK_EQ(entry.m_hour, 13.5f);
    OBL_CHECK_EQ(entry.m_day, 2);
    OBL_CHECK_EQ(entry.m_desireModifiers[0], 0.0f);
    OBL_CHECK(std::memcmp(entry.m_desireModifiers, data.m_modifiers, sizeof(data.m_modifiers)) == 0);
}

OBL_TEST_CASE("world_script_delegate.paint_masks_are_the_ones_the_build_tools_use")
{
    // The gui mode place pavement place tile routine / the gui mode place grass place tile routine (docs/architecture/map.md) set the same bits as the script's Path / Grass.
    OBL_CHECK_EQ(kPathFlags, 0x10000080u);
    OBL_CHECK_EQ(kGrassFlags, 0x7000u);
}

namespace {

obl::world_script::WorldScriptCreateData CreateData(int32_t createClass)
{
    static char type[] = "HOTEL";
    static char sprite[] = "Litter1";
    obl::world_script::WorldScriptCreateData data{};
    data.m_class = createClass;
    data.m_type = type;
    data.m_sprite = sprite;
    data.m_x = 12;
    data.m_y = 34;
    data.m_layer = 0x1234;
    data.m_number = 1;
    for (int i = 0; i < 5; ++i) data.m_prices[i] = 1.5f + i;
    data.m_beer = 2;
    data.m_beerPrice = 0.75f;
    data.m_age = 9;
    data.m_broken = 1;
    return data;
}

int32_t Word(const void* p)
{
    int32_t w;
    std::memcpy(&w, p, 4);
    return w;
}

} // namespace

OBL_TEST_CASE("world_script_delegate.create_message_goes_from_the_world_to_the_world_with_the_class")
{
    const obl::messages::Message message = CreateMessage(kCreateGuest);
    OBL_CHECK_EQ(message.sender, -2);
    OBL_CHECK_EQ(message.receiver, -2);
    OBL_CHECK_EQ(message.type, 0);
    OBL_CHECK_EQ(message.flags, 0);
    OBL_CHECK_EQ(message.data, 3);
}

OBL_TEST_CASE("world_script_delegate.create_placement_writes_each_class_s_own_words")
{
    const obl::world_script::ScriptPlacement building = CreatePlacement(CreateData(kCreateBuilding));
    OBL_CHECK(std::strcmp(building.m_name, "HOTEL") == 0);
    OBL_CHECK_EQ(building.m_sprite[0], 0);
    OBL_CHECK_EQ(building.m_x, 12);
    OBL_CHECK_EQ(building.m_y, 34);
    OBL_CHECK_EQ(building.m_layer, 0x1234);
    OBL_CHECK_EQ(building.m_prices[4], 5.5f);
    OBL_CHECK_EQ(building.m_beer, 2);
    OBL_CHECK_EQ(building.m_beerPrice, 0.75f);
    OBL_CHECK_EQ(building.m_age, 9);
    OBL_CHECK_EQ(building.m_broken, 1);

    // Scenery: the sprite, the tile, byte 1 and the layer's low byte, -2 in the first price word.
    const obl::world_script::ScriptPlacement scenery = CreatePlacement(CreateData(kCreateScenery));
    OBL_CHECK(std::strcmp(scenery.m_sprite, "Litter1") == 0);
    OBL_CHECK_EQ(scenery.m_x, 12);
    OBL_CHECK_EQ(scenery.m_layer, 0x3401);
    OBL_CHECK_EQ(Word(&scenery.m_prices[0]), -2);
    OBL_CHECK_EQ(scenery.m_beer, 0);

    // A guest stands at a world point: the tile numbers as floats.
    const obl::world_script::ScriptPlacement guest = CreatePlacement(CreateData(kCreateGuest));
    OBL_CHECK_EQ(guest.m_x, std::bit_cast<int32_t>(12.0f));
    OBL_CHECK_EQ(guest.m_y, std::bit_cast<int32_t>(34.0f));
    OBL_CHECK_EQ(guest.m_layer, 0);
}

OBL_TEST_CASE("world_script_delegate.a_long_type_runs_on_into_the_sprite_field_like_strcpy")
{
    char type[] = "a type name that is longer than its 32-byte field";
    obl::world_script::WorldScriptCreateData data = CreateData(kCreateBuilding);
    data.m_type = type;
    const obl::world_script::ScriptPlacement placement = CreatePlacement(data);
    // Read as raw bytes of the placement: the text spans m_name and m_sprite, which a read through the m_name array may not.
    OBL_CHECK(std::strcmp(reinterpret_cast<const char*>(&placement) + offsetof(obl::world_script::ScriptPlacement, m_name), type) == 0);
    // Scenery copies the sprite over the tail.
    data.m_class = kCreateScenery;
    const obl::world_script::ScriptPlacement scenery = CreatePlacement(data);
    OBL_CHECK(std::strcmp(scenery.m_sprite, "Litter1") == 0);
    OBL_CHECK(std::memcmp(scenery.m_name, type, 32) == 0);
}

OBL_TEST_CASE("world_script_delegate.normalized_placements_drop_the_words_a_class_does_not_write")
{
    for (int32_t createClass : {kCreateScenery, kCreateBuilding, kCreateGuest}) {
        const obl::world_script::ScriptPlacement clean = CreatePlacement(CreateData(createClass));
        obl::world_script::ScriptPlacement garbage;
        std::memset(&garbage, 0xcd, sizeof(garbage));
        std::memcpy(garbage.m_name, clean.m_name, std::strlen(clean.m_name) + 1);
        switch (createClass) {
        case kCreateScenery:
            std::memcpy(garbage.m_sprite, clean.m_sprite, std::strlen(clean.m_sprite) + 1);
            garbage.m_x = clean.m_x;
            garbage.m_y = clean.m_y;
            std::memcpy(&garbage.m_layer, &clean.m_layer, 2); // two bytes
            garbage.m_prices[0] = clean.m_prices[0];
            break;
        case kCreateBuilding:
            std::memcpy(&garbage.m_x, &clean.m_x,
                    offsetof(obl::world_script::ScriptPlacement, m_broken) + 1 - offsetof(obl::world_script::ScriptPlacement, m_x));
            break;
        default:
            garbage.m_x = clean.m_x;
            garbage.m_y = clean.m_y;
            break;
        }
        const obl::world_script::ScriptPlacement a = NormalizePlacement(garbage, createClass), b = NormalizePlacement(clean, createClass);
        OBL_CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);
    }
}

OBL_TEST_CASE("world_script_delegate.random_create_point_scales_two_draws")
{
    int draws[] = {0, 32767, 16384, 1};
    int i = 0;
    auto draw = [&]() { return draws[i++]; };
    const CreatePoint low = RandomCreatePoint(draw);
    OBL_CHECK_EQ(low.x, 1.0f);
    OBL_CHECK_EQ(low.y, 7999.0f); // 32767 * (1/32767 as float) is a hair above 1
    const CreatePoint mid = RandomCreatePoint(draw);
    OBL_CHECK_EQ(mid.x, static_cast<float>(16384.0 * static_cast<double>(kRandUnit) * 3998.0 + 1.0));
    OBL_CHECK_EQ(mid.y, static_cast<float>(static_cast<double>(kRandUnit) * 7998.0 + 1.0));
    OBL_CHECK_EQ(i, 4);
}

OBL_TEST_CASE("world_script_delegate.create_tiles_need_land_and_no_0x10_bit")
{
    OBL_CHECK(IsCreateTile(0x2));
    OBL_CHECK(IsCreateTile(0x10000000));
    OBL_CHECK(!IsCreateTile(0x12));
    OBL_CHECK(!IsCreateTile(0x1));
    OBL_CHECK(!IsCreateTile(0));
    OBL_CHECK(IsRandomCreate({0, nullptr, nullptr, -1, -1}));
    OBL_CHECK(!IsRandomCreate({0, nullptr, nullptr, -1, 0}));
}
