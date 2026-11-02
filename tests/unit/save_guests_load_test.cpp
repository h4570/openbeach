#include "core/char16.h"
#include "unit_test.h"
#include "game/map/path_data.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"
#include "game/world/save_guests_load.h"
#include "game/world/save_objects_json.h"
#include "game/world/save_objects_load.h"

#include <cstring>
#include <memory>
#include <string>

using namespace obl;

namespace {

const char* const kBuildings[] = {"obl.building.kiosk", "obl.building.bar"};

world_save::JsonContext WriteContext()
{
    world_save::JsonContext context;
    context.narrowToUtf8 = [](std::string_view text) { return std::string(text); };
    context.buildingKey = [](int32_t id) { return id >= 0 && id < 2 ? std::string(kBuildings[id]) : std::string(); };
    return context;
}

world_save::ReadContext ReadContext()
{
    world_save::ReadContext context;
    context.utf8ToNarrow = [](std::string_view text, std::string& out) {
        out = text;
        return true;
    };
    context.buildingId = [](std::string_view key) -> int32_t {
        for (int32_t i = 0; i < 2; ++i)
            if (key == kBuildings[i]) return i;
        return -1;
    };
    return context;
}

struct LoadCallbacks {
    std::string created;
    ui::SpriteInstance sprite{}, attached{};

    world_save::ObjectLoadCallbacks Make()
    {
        world_save::ObjectLoadCallbacks callbacks;
        callbacks.setPosition = [](object::Base&, const uint32_t*) {};
        callbacks.createSprite = [this](const char* name) -> void* {
            created += std::string(name) + " ";
            return std::string(name) == "ICE_CREAM" ? &attached : &sprite;
        };
        return callbacks;
    }
};

std::unique_ptr<object::Guest> NewGuest()
{
    auto guest = std::make_unique<object::Guest>();
    guest->m_type = object::ObjectType::Guest;
    return guest;
}

} // namespace

OBL_TEST_CASE("save_guests_load.guest_reads_back_its_records_name_sprite_and_building_types")
{
    auto guest = NewGuest();
    std::strcpy(guest->m_Name, "Guest");
    guest->m_id = 1200;
    guest->m_Tolerance[13] = 0.75f;
    guest->m_desires[25] = -2.5f;
    guest->m_invent_slot[4] = 9;
    guest->m_walkpoints[2][1] = 12.5f;
    guest->m_cash = 40.0f;
    Char16 name[] = {u'Z', 0x15b, u'a', 0};
    guest->m_name = name;
    std::strcpy(guest->m_WalkAnimation, "WALK_A");
    std::strcpy(guest->m_ReturnAnimation, "STAND_B");
    guest->m_lastActivity[1] = 0xfe;
    guest->m_BestType = 1;
    guest->m_WorstType = -1;
    guest->m_CurTypeID = 0;
    guest->m_Thoughts[2] = 44;
    ui::SpriteInstance sprite{}, carried{};
    char spriteName[] = "GUEST_F", carriedName[] = "ICE_CREAM";
    sprite.m_name = spriteName;
    carried.m_name = carriedName;
    carried.m_frameIndex = 3;
    guest->m_sprite = &sprite;
    guest->m_attachedSprite = &carried;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteObjectJson(*guest, WriteContext(), written, error));
    OBL_CHECK(written["guest"]["best_type"] == "obl.building.bar");
    OBL_CHECK(written["guest"]["worst_type"].is_null());

    // Constructed with a heap name and without a sprite, as the object manager load routine does.
    auto loaded = NewGuest();
    loaded->m_name = static_cast<Char16*>(::operator new(sizeof(Char16)));
    loaded->m_name[0] = 0;
    LoadCallbacks callbacks;
    callbacks.sprite.m_name = spriteName;
    callbacks.attached.m_name = carriedName;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json(written), ReadContext(), callbacks.Make(), *loaded, error));
    OBL_CHECK(callbacks.created == "ICE_CREAM GUEST_F ");
    OBL_CHECK(loaded->m_attachedSprite == &callbacks.attached);
    OBL_CHECK(callbacks.attached.m_frameIndex == 3);
    OBL_CHECK(loaded->m_sprite == &callbacks.sprite);
    OBL_CHECK(loaded->m_name[1] == 0x15b);
    OBL_CHECK(loaded->m_name[3] == 0);
    OBL_CHECK(loaded->m_BestType == 1);
    OBL_CHECK(loaded->m_WorstType == -1);
    OBL_CHECK(loaded->m_CurTypeID == 0);
    OBL_CHECK(loaded->m_lastActivity[1] == 0xfe);
    OBL_CHECK(loaded->m_walkpoints[2][1] == 12.5f);
    OBL_CHECK(std::string(loaded->m_ReturnAnimation) == "STAND_B");
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteObjectJson(*loaded, WriteContext(), again, error));
    OBL_CHECK(again == written);
    ::operator delete(loaded->m_name);
}

OBL_TEST_CASE("save_guests_load.missing_guest_keys_read_as_the_bridge_defaults")
{
    auto guest = NewGuest();
    guest->m_BestType = 5;
    guest->m_Tolerance[0] = 1.0f;
    LoadCallbacks callbacks;
    std::string error;
    OBL_REQUIRE(world_save::ReadObjectJson(nlohmann::json::parse(R"({"id":4,"kind":"guest","type":"guest","guest":{"worst_type":null}})"),
            ReadContext(), callbacks.Make(), *guest, error));
    OBL_CHECK(guest->m_BestType == 0);
    OBL_CHECK(guest->m_WorstType == -1);
    OBL_CHECK(guest->m_Tolerance[0] == 0.0f);
    OBL_CHECK(guest->m_attachedSprite == nullptr);
    OBL_REQUIRE(guest->m_name != nullptr);
    OBL_CHECK(guest->m_name[0] == 0);
    ::operator delete(guest->m_name);
    delete guest->m_pathCursor;
}

OBL_TEST_CASE("save_guests_load.refuses_values_the_guest_cannot_hold")
{
    const std::string longName(128, 'a');
    const std::string bad[] = {
            R"({"id":4,"type":"guest","guest":{"best_type":"obl.building.none"}})",
            R"({"id":4,"type":"guest","guest":{"current_type":3}})",
            R"({"id":4,"type":"guest","guest":{"tolerance":[1,2]}})",
            R"({"id":4,"type":"guest","guest":{"last_activity":[1,256]}})",
            R"({"id":4,"type":"guest","guest":{"walk_animation":"a name past the forty bytes of the animation"}})",
            R"({"id":4,"type":"guest","guest":{"attached":5}})",
            R"({"id":4,"type":"guest","guest":{"name":")" + longName + R"("}})",
    };
    for (const std::string& text : bad) {
        auto guest = NewGuest();
        LoadCallbacks callbacks;
        std::string error;
        OBL_CHECK(!world_save::ReadObjectJson(nlohmann::json::parse(text), ReadContext(), callbacks.Make(), *guest, error));
        OBL_CHECK(error.rfind("object ", 0) == 0);
        ::operator delete(guest->m_name);
        delete guest->m_pathCursor;
    }
    OBL_CHECK(world_save::HasObjectReader(static_cast<uint8_t>(object::ObjectType::Guest)));
}
