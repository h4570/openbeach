// ObjectManager::CreateObject / DeleteObject / InsertLastBuildingByY (src/game/objects/object_manager_lifecycle.h, docs/architecture/objects.md).

#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/object_manager_lifecycle.h"

#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using object::ObjectType;
namespace om = obl::object_manager;

namespace {

// The manager's deleter is a plain function: the record of destroyed objects is static, cleared by each FakeEnv.
std::vector<object::Base*> g_destroyed;

struct FakeEnv {
    std::vector<std::unique_ptr<object::Base>> objects;
    std::vector<std::pair<object::Base*, uint32_t>> ids;
    std::vector<object::Base*>& destroyed = g_destroyed;
    std::vector<std::string> fatal;
    std::vector<uint8_t> constructed;
    bool failAllocation = false;

    object::Base* Construct(uint8_t type, const char*)
    {
        constructed.push_back(type);
        if (failAllocation) return nullptr;
        objects.push_back(std::make_unique<object::Base>());
        obl::unit::FillObject<object::Base>(*objects.back());
        return objects.back().get();
    }
    void SetId(object::Base* o, uint32_t id)
    {
        ids.emplace_back(o, id);
        if (o) o->m_id = id;
    }
    FakeEnv() { g_destroyed.clear(); }
    FakeEnv(const FakeEnv&) = delete;
    FakeEnv& operator=(const FakeEnv&) = delete;
    object::DestroyFunc Destroyer()
    {
        return [](object::Base* o) { g_destroyed.push_back(o); };
    }
    void Fatal(const char* message) { fatal.push_back(message); }
};

std::unique_ptr<object::Manager> Manager()
{
    auto m = std::make_unique<object::Manager>();
    m->m_nextFreeObjectId = 1;
    return m;
}

} // namespace

OBL_TEST_CASE("object_manager.create_appends_to_the_array_of_its_class_and_hands_out_ids")
{
    auto m = Manager();
    FakeEnv env;
    object::Base* b = om::CreateObject(*m, "b", 2, env);
    object::Base* g = om::CreateObject(*m, "g", 3, env);
    object::Base* s = om::CreateObject(*m, "s", 1, env);
    object::Base* v = om::CreateObject(*m, "v", 20, env);
    OBL_CHECK_EQ(m->m_buildings.size(), 1u);
    OBL_CHECK_EQ(m->m_guests.size(), 1u);
    OBL_CHECK_EQ(m->m_others.size(), 2u);
    OBL_CHECK_EQ(m->ActiveCount(), 4u);
    OBL_CHECK_EQ(m->m_nextFreeObjectId, 5u);
    OBL_CHECK(m->m_buildings[0] == b);
    OBL_CHECK(m->m_guests[0] == g);
    OBL_CHECK(m->m_others[0] == s);
    OBL_CHECK(m->m_others[1] == v);
    OBL_CHECK_EQ(m->m_buildingIds[0], 1u);
    OBL_CHECK_EQ(m->m_guestIds[0], 2u);
    OBL_CHECK_EQ(m->m_otherIds[0], 3u);
    OBL_CHECK_EQ(m->m_otherIds[1], 4u);
    OBL_CHECK_EQ(env.ids.size(), 4u);
    OBL_CHECK_EQ(g->m_id, 2u);
    OBL_CHECK(env.fatal.empty());
}

OBL_TEST_CASE("object_manager.create_rejects_types_outside_1_to_20_before_allocating")
{
    auto m = Manager();
    FakeEnv env;
    OBL_CHECK(om::CreateObject(*m, "x", 0, env) == nullptr);
    OBL_CHECK(om::CreateObject(*m, "x", 21, env) == nullptr);
    OBL_CHECK(om::CreateObject(*m, "x", 0xff, env) == nullptr);
    OBL_CHECK_EQ(env.fatal.size(), 3u);
    OBL_CHECK(env.constructed.empty());
    OBL_CHECK_EQ(m->m_nextFreeObjectId, 1u);
}

OBL_TEST_CASE("object_manager.create_reports_a_full_array_after_filling_it")
{
    auto m = Manager();
    FakeEnv env;
    for (int i = 0; i < object::Manager::kMaxBuildings - 1; ++i) m->AddBuilding(nullptr, 0xffffffffu);
    om::CreateObject(*m, "b", 2, env);
    OBL_CHECK_EQ(m->m_buildings.size(), 500u);
    OBL_CHECK_EQ(m->m_buildings.capacity(), 500u); // the reserved capacity holds: a walk's data() pointer stays valid
    OBL_CHECK_EQ(env.fatal.size(), 1u);
    OBL_CHECK_EQ(env.fatal[0], std::string(om::kOutOfBuildingSlots));
    m = Manager();
    m->m_nextFreeObjectId = om::kUnusableId;
    FakeEnv env2;
    om::CreateObject(*m, "g", 3, env2);
    OBL_CHECK_EQ(env2.fatal.size(), 1u);
    OBL_CHECK_EQ(env2.fatal[0], std::string(om::kOutOfIds));
    OBL_CHECK_EQ(m->m_nextFreeObjectId, om::kUnusableId + 1);
}

OBL_TEST_CASE("object_manager.create_with_a_failed_allocation_stores_null")
{
    auto m = Manager();
    FakeEnv env;
    env.failAllocation = true;
    OBL_CHECK(om::CreateObject(*m, "g", 3, env) == nullptr);
    OBL_CHECK_EQ(m->m_guests.size(), 1u);
    OBL_CHECK(m->m_guests[0] == nullptr);
    OBL_CHECK_EQ(env.ids.size(), 1u);
}

OBL_TEST_CASE("object_manager.delete_building_keeps_the_order_guests_and_others_take_the_last")
{
    auto m = Manager();
    FakeEnv env;
    object::Base* b[3];
    for (auto& p : b) p = om::CreateObject(*m, "b", 2, env);
    object::Base* g[3];
    for (auto& p : g) p = om::CreateObject(*m, "g", 3, env);
    object::Base* o[3];
    for (auto& p : o) p = om::CreateObject(*m, "s", 5, env);
    OBL_CHECK(!om::DeleteObject(*m, 99));
    OBL_CHECK(om::DeleteObject(*m, b[0]->m_id));
    OBL_CHECK((m->m_buildings == std::vector<object::Base*>{b[1], b[2]}));
    OBL_CHECK((m->m_buildingIds == std::vector<uint32_t>{b[1]->m_id, b[2]->m_id}));
    OBL_CHECK(om::DeleteObject(*m, g[0]->m_id));
    OBL_CHECK((m->m_guests == std::vector<object::Base*>{g[2], g[1]}));
    OBL_CHECK((m->m_guestIds == std::vector<uint32_t>{g[2]->m_id, g[1]->m_id}));
    OBL_CHECK(om::DeleteObject(*m, o[2]->m_id)); // the last one: no move
    OBL_CHECK((m->m_others == std::vector<object::Base*>{o[0], o[1]}));
    OBL_CHECK_EQ(m->ActiveCount(), 6u);
    const auto saved = m->SavedIds();
    OBL_CHECK_EQ(saved[2], 0xffffffffu); // the vacated slot reads as unused in the saved list
    OBL_CHECK_EQ(env.destroyed.size(), 3u);
    OBL_CHECK(env.destroyed[0] == b[0]);
    OBL_CHECK(om::GetObject(*m, g[2]->m_id) == g[2]);
    // The manager owns what it created and indexes it by id; a deleted object leaves both.
    OBL_CHECK_EQ(m->m_owned.size(), 6u);
    OBL_CHECK_EQ(m->m_byId.size(), 6u);
    OBL_CHECK(m->Resolve(b[1]->m_id) == b[1]);
    OBL_CHECK(m->Resolve(o[0]->m_id) == o[0]);
    OBL_CHECK(m->Resolve(b[0]->m_id) == nullptr);
}

OBL_TEST_CASE("object_manager.insert_last_building_sorts_by_y_and_is_stable")
{
    auto m = Manager();
    FakeEnv env;
    const float ys[] = {5, 2, 8, 2, 1};
    object::Base* made[5];
    for (int i = 0; i < 5; ++i) {
        made[i] = om::CreateObject(*m, "b", 2, env);
        made[i]->m_WorldXYZ[1] = ys[i];
        om::InsertLastBuildingByY(*m);
    }
    const float expected[] = {1, 2, 2, 5, 8};
    for (int i = 0; i < 5; ++i) OBL_CHECK_EQ(m->m_buildings[i]->m_WorldXYZ[1], expected[i]);
    OBL_CHECK(m->m_buildings[1] == made[1]); // equal Y: the older one stays left
    OBL_CHECK(m->m_buildings[2] == made[3]);
    for (int i = 0; i < 5; ++i) OBL_CHECK_EQ(m->m_buildingIds[i], m->m_buildings[i]->m_id);
}

OBL_TEST_CASE("object_manager.insert_last_building_walks_past_nan")
{
    auto m = Manager();
    FakeEnv env;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float ys[] = {1, nan, 0};
    object::Base* made[3];
    for (int i = 0; i < 3; ++i) {
        made[i] = om::CreateObject(*m, "b", 2, env);
        made[i]->m_WorldXYZ[1] = ys[i];
        om::InsertLastBuildingByY(*m);
    }
    // [NaN, 1] after the second insert (NaN passed 1); then 0 passes both, because 0 >= NaN is false too.
    OBL_CHECK(m->m_buildings[0] == made[2]);
    OBL_CHECK(m->m_buildings[1] == made[1]);
    OBL_CHECK(m->m_buildings[2] == made[0]);
    auto single = Manager();
    FakeEnv env2;
    om::CreateObject(*single, "b", 2, env2);
    om::InsertLastBuildingByY(*single);
    OBL_CHECK_EQ(single->m_buildings.size(), 1u);
}
