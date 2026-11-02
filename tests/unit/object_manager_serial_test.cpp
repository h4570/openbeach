#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "unit_test.h"
#include "game/objects/object_manager_serial.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
namespace om = object_manager;

OBL_TEST_CASE("object_manager.a_new_manager_is_empty_with_reserved_lists")
{
    object::Manager m;
    OBL_CHECK_EQ(m.ActiveCount(), 0u);
    OBL_CHECK(m.m_buildings.empty() && m.m_guests.empty() && m.m_others.empty());
    OBL_CHECK_EQ(m.m_nextFreeObjectId, 0u);
    OBL_CHECK_EQ(m.m_globalKeyline, 0);
    OBL_CHECK_EQ(m.m_buildings.capacity(), 500u);
    OBL_CHECK_EQ(m.m_guests.capacity(), 1500u);
    OBL_CHECK_EQ(m.m_others.capacity(), 16500u);
    const auto ids = m.SavedIds();
    OBL_CHECK_EQ(ids.size(), 18500u);
    for (auto id : ids) OBL_CHECK_EQ(id, 0xffffffffu);
}

namespace {
// The deleter of the destruct test: a plain function, so the record is static.
object::Manager* g_destructManager = nullptr;
std::vector<object::Base*> g_deleted;
void RecordDelete(object::Base* o)
{
    OBL_CHECK_EQ(g_destructManager->ActiveCount(), 5u); // the objects stay listed while they are destroyed
    g_deleted.push_back(o);
}
} // namespace

OBL_TEST_CASE("object_manager.destruct_deletes_the_owned_objects_in_order_then_empties_the_lists")
{
    auto m = std::make_unique<object::Manager>();
    object::Base objects[4]{};
    m->AddBuilding(&objects[0], 91);
    m->AddBuilding(nullptr, 92);
    m->AddGuest(&objects[1], 93);
    m->AddOther(&objects[2], 94);
    m->AddOther(&objects[3], 96); // listed by a fixture without being owned: not destroyed
    for (int i = 0; i < 3; ++i) m->Adopt(&objects[i], &RecordDelete);
    m->m_nextFreeObjectId = 95;
    m->m_globalKeyline = 1;
    g_destructManager = m.get();
    g_deleted.clear();
    om::Destruct(*m);
    OBL_CHECK((g_deleted == std::vector<object::Base*>{&objects[0], &objects[1], &objects[2]}));
    OBL_CHECK(m->m_owned.empty() && m->m_byId.empty());
    OBL_CHECK_EQ(m->ActiveCount(), 0u);
    OBL_CHECK_EQ(m->m_nextFreeObjectId, 95u);
    OBL_CHECK_EQ(m->m_globalKeyline, 1);
}

OBL_TEST_CASE("object_manager.load_routes_all_types_and_publishes_before_the_object_load")
{
    auto m = std::make_unique<object::Manager>();
    struct Env {
        // A constructor, not aggregate initialization: gcc 13 fails on a local aggregate holding an array of a class with a
        // virtual destructor.
        explicit Env(object::Manager& manager) : m(manager) {}
        object::Manager& m;
        object::Base objects[20]{};
        std::vector<uint8_t> types;
        size_t headers = 0, loaded = 0;
        void ObjectHeader(uint8_t& type, char* name)
        {
            // Building/guest ignore the serialized type when choosing a constructor.
            if (headers < 2)
                type = 20;
            else {
                type = static_cast<uint8_t>(headers - 1);
                if (type >= 2) type += 2;
            }
            std::strcpy(name, "fixture");
            ++headers;
        }
        object::Base* Construct(uint8_t type, const char* name)
        {
            OBL_CHECK_EQ(m.ActiveCount(), 20u);
            OBL_CHECK_EQ(m.m_nextFreeObjectId, 777u);
            OBL_CHECK(std::strcmp(name, "fixture") == 0);
            types.push_back(type);
            return &objects[types.size() - 1];
        }
        object::DestroyFunc Destroyer()
        {
            return [](object::Base*) {};
        } // the fixture owns the memory
        void LoadObject(object::Base* o)
        {
            object::Base* published = loaded == 0 ? m.m_buildings[0] : loaded == 1 ? m.m_guests[0] : m.m_others[loaded - 2];
            OBL_CHECK(published == o);
            // Owned and indexed under its saved id before its own Load runs (a scenery enters its cells by id while it loads).
            const uint32_t id = loaded == 0 ? m.m_buildingIds[0] : loaded == 1 ? m.m_guestIds[0] : m.m_otherIds[loaded - 2];
            OBL_CHECK(m.Resolve(id) == o);
            OBL_CHECK(m.m_owned.contains(o));
            OBL_CHECK(o == &objects[loaded]);
            ++loaded;
        }
    } env(*m);
    om::SavedHeader header;
    header.buildings = 1;
    header.guests = 1;
    header.others = 18;
    header.ids.resize(object::kSavedIdCount);
    for (size_t i = 0; i < header.ids.size(); ++i) header.ids[i] = static_cast<uint32_t>(i + 50);
    header.nextId = 777;
    header.keyline = 1;
    om::ApplyHeader(*m, header);
    om::LoadObjects(*m, env);
    OBL_CHECK_EQ(env.loaded, 20u);
    OBL_CHECK((env.types == std::vector<uint8_t>{2, 3, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}));
    OBL_CHECK_EQ(m->m_buildingIds[0], 50u);
    OBL_CHECK_EQ(m->m_guestIds[0], 50u + om::kGuestBase);
    OBL_CHECK_EQ(m->m_otherIds[0], 50u + om::kOtherBase);
    OBL_CHECK_EQ(m->m_otherIds[17], 50u + om::kOtherBase + 17);
    OBL_CHECK_EQ(m->m_nextFreeObjectId, 777u);
    OBL_CHECK_EQ(m->m_globalKeyline, 1);
}

OBL_TEST_CASE("object_manager.load_cuts_a_count_past_the_capacity_of_its_list")
{
    auto m = std::make_unique<object::Manager>();
    om::SavedHeader header;
    header.buildings = 100000;
    header.guests = 3;
    header.ids.assign(object::kSavedIdCount, 5);
    om::ApplyHeader(*m, header);
    OBL_CHECK_EQ(m->m_buildings.size(), 500u);
    OBL_CHECK_EQ(m->m_buildingIds.size(), 500u);
    OBL_CHECK_EQ(m->m_guests.size(), 3u);
    OBL_CHECK(m->m_others.empty());
}
