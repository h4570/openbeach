#include "object_storage.h"
#include "unit_test.h"

#include "game/objects/object_building.h"
#include "game/world/world_objects_logic.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/world/world_state.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_objects;
using object::ObjectType;

namespace {

// Objects are zeroed memory of the real layout; the tests only set the fields the functions read.
struct Slab {
    std::unique_ptr<layout::World> world = std::make_unique<layout::World>();
    std::unique_ptr<object::Manager> manager = std::make_unique<object::Manager>();
    std::vector<std::unique_ptr<object::Guest>> guestObjects;
    std::vector<std::unique_ptr<object::Building>> buildingObjects;
    std::vector<std::unique_ptr<object::Scenery>> sceneryObjects;
    std::vector<std::unique_ptr<tables::GuestDef>> guestDefs;
    std::vector<std::unique_ptr<tables::SceneryDef>> sceneryDefs;
    uint32_t nextId = 100;

    Slab() { world->m_objectManager.reset(manager.get()); }

    template<class T> T* Zeroed(std::vector<std::unique_ptr<T>>& store)
    {
        store.push_back(std::make_unique<T>());
        obl::unit::FillObject(*store.back());
        return store.back().get();
    }

    // Definitions hold std::string and vector members: value-initialised, never memset (that would leak their heap blocks).
    template<class T> T* Fresh(std::vector<std::unique_ptr<T>>& store)
    {
        store.push_back(std::make_unique<T>());
        return store.back().get();
    }

    object::Guest* Guest(int gender, bool leavingEarly = false, int state = 0)
    {
        auto* guest = Zeroed(guestObjects);
        auto* def = Fresh(guestDefs);
        def->m_Gender = gender;
        guest->m_definition = def;
        guest->m_type = ObjectType::Guest;
        guest->m_id = nextId++;
        guest->m_state = state;
        guest->m_leavingEarly = leavingEarly;
        return guest;
    }
    object::Building* Building()
    {
        auto* building = Zeroed(buildingObjects);
        building->m_type = ObjectType::Building;
        building->m_id = nextId++;
        return building;
    }
    object::Scenery* Scenery(const char* name, float rating)
    {
        auto* scenery = Zeroed(sceneryObjects);
        auto* def = Fresh(sceneryDefs);
        def->m_Rating = rating;
        scenery->m_definition = def;
        scenery->m_type = ObjectType::Scenery;
        scenery->m_id = nextId++;
        std::strncpy(scenery->m_Name, name, sizeof(scenery->m_Name) - 1);
        return scenery;
    }

    void AddBuilding(object::Base* object) { manager->AddBuilding(object, object->m_id); }
    void AddOther(object::Base* object) { manager->AddOther(object, object->m_id); }
    void AddGuest(object::Base* object) { manager->AddGuest(object, object->m_id); }
};

// Removes the object with `id` from the three arrays, shifting the rest down as the object manager delete object routine does.
void RemoveFromArrays(object::Manager& manager, uint32_t id)
{
    const auto remove = [&](std::vector<object::Base*>& list, std::vector<uint32_t>& ids) {
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i]->m_id == id) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
    };
    remove(manager.m_buildings, manager.m_buildingIds);
    remove(manager.m_others, manager.m_otherIds);
    remove(manager.m_guests, manager.m_guestIds);
}

struct UpdateEnv {
    std::vector<uint32_t> updated;
    void (*onUpdate)(UpdateEnv&, object::Base&) = nullptr;
    void Update(object::Base& object)
    {
        updated.push_back(object.m_id);
        if (onUpdate) onUpdate(*this, object);
    }
};

struct DeleteEnv {
    object::Manager* manager = nullptr;
    layout::World* world = nullptr;
    std::vector<std::string> calls;
    uint32_t idAfterExit = 0; // the id the object gets while its OnExitState runs (0 = unchanged)

    void OnObjectDeleting(object::Base& object) { calls.push_back("deleting " + std::to_string(object.m_id)); }
    void OnExitState(object::Base& object)
    {
        calls.push_back("exit " + std::to_string(object.m_id));
        if (idAfterExit) object.m_id = idAfterExit;
    }
    void DeleteObject(uint32_t id)
    {
        calls.push_back("delete " + std::to_string(id));
        RemoveFromArrays(*manager, id);
    }
    void RefreshDialogs() { calls.push_back("refresh"); }
};

struct DeletingEnv {
    std::vector<std::string> calls;
    void CloseDialog(object::Base& object) { calls.push_back("dialog " + std::to_string(object.m_id)); }
    void RecordGuestLeaving(object::Base& object) { calls.push_back("leaving " + std::to_string(object.m_id)); }
    void RemoveThoughts(object::Base& building, uint32_t id)
    {
        calls.push_back("thoughts " + std::to_string(building.m_id) + " of " + std::to_string(id));
    }
    void InvalidateFeedback(uint32_t id) { calls.push_back("feedback " + std::to_string(id)); }
};

} // namespace

OBL_TEST_CASE("world_objects.update_walks_buildings_then_others_then_guests_each_from_the_end")
{
    Slab slab;
    auto* b1 = slab.Building();
    auto* b2 = slab.Building();
    auto* g1 = slab.Guest(0);
    auto* g2 = slab.Guest(1);
    auto* o1 = slab.Scenery("Palm", 0.0f);
    auto* o2 = slab.Scenery("Bin", 0.0f);
    slab.AddBuilding(b1);
    slab.AddBuilding(b2);
    slab.AddGuest(g1);
    slab.AddGuest(g2);
    slab.AddOther(o1);
    slab.AddOther(o2);
    UpdateEnv env;
    UpdateObjects(*slab.world, env);
    const std::vector<uint32_t> expected = {b2->m_id, b1->m_id, o2->m_id, o1->m_id, g2->m_id, g1->m_id};
    OBL_CHECK(env.updated == expected);
}

OBL_TEST_CASE("world_objects.update_skips_objects_waiting_for_death_and_empty_lists")
{
    Slab slab;
    UpdateEnv none;
    UpdateObjects(*slab.world, none);
    OBL_CHECK(none.updated.empty());

    auto* g1 = slab.Guest(0);
    auto* g2 = slab.Guest(0);
    g2->m_waitingForDeath = 1;
    slab.AddGuest(g1);
    slab.AddGuest(g2);
    UpdateEnv env;
    UpdateObjects(*slab.world, env);
    const std::vector<uint32_t> expected = {g1->m_id};
    OBL_CHECK(env.updated == expected);
}

OBL_TEST_CASE("world_objects.update_reads_the_count_once_and_the_array_live")
{
    // The first object updated (the last guest) appends one more and marks the next one to be visited: the count is the old one, so the new one is
    // not visited, and the mark is seen because the element is read only when its turn comes.
    Slab slab;
    auto* g1 = slab.Guest(0);
    auto* g2 = slab.Guest(0);
    auto* late = slab.Guest(0);
    slab.AddGuest(g1);
    slab.AddGuest(g2);
    static Slab* s_slab;
    static object::Base *lateGuest, *firstGuest; // static: the observer below is a plain function
    s_slab = &slab;
    lateGuest = late;
    firstGuest = g1;
    UpdateEnv env;
    env.onUpdate = [](UpdateEnv&, object::Base& object) {
        if (&object == firstGuest) return;
        s_slab->AddGuest(lateGuest);
        firstGuest->m_waitingForDeath = 1;
    };
    UpdateObjects(*slab.world, env);
    const std::vector<uint32_t> expected = {g2->m_id};
    OBL_CHECK(env.updated == expected);
    OBL_CHECK_EQ(slab.manager->m_guests.size(), 3u);
}

OBL_TEST_CASE("world_objects.process_deleted_removes_only_the_marked_ones_in_order")
{
    Slab slab;
    auto* b1 = slab.Building();
    auto* b2 = slab.Building();
    auto* g1 = slab.Guest(0);
    auto* g2 = slab.Guest(0);
    auto* g3 = slab.Guest(0);
    auto* o1 = slab.Scenery("Palm", 0.0f);
    slab.AddBuilding(b1);
    slab.AddBuilding(b2);
    slab.AddGuest(g1);
    slab.AddGuest(g2);
    slab.AddGuest(g3);
    slab.AddOther(o1);
    b1->m_waitingForDeath = 1;
    b2->m_waitingForDeath = 1;
    g1->m_waitingForDeath = 1;
    g3->m_waitingForDeath = 1;
    o1->m_waitingForDeath = 1;
    DeleteEnv env;
    env.manager = slab.manager.get();
    ProcessDeletedObjects(*slab.world, env);
    const auto line = [](const char* what, const object::Base* object) { return std::string(what) + " " + std::to_string(object->m_id); };
    const std::vector<std::string> expected = {line("deleting", b2), line("exit", b2), line("delete", b2), line("deleting", b1),
            line("exit", b1), line("delete", b1), line("deleting", o1), line("exit", o1), line("delete", o1), line("deleting", g3),
            line("exit", g3), line("delete", g3), line("deleting", g1), line("exit", g1), line("delete", g1), "refresh"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK_EQ(slab.manager->m_buildings.size(), 0u);
    OBL_CHECK_EQ(slab.manager->m_guests.size(), 1u);
    OBL_CHECK(slab.manager->m_guests[0] == g2); // the unmarked guest is the one left, however the deletions shifted the array
}

OBL_TEST_CASE("world_objects.process_deleted_does_not_refresh_when_nothing_was_removed")
{
    Slab slab;
    slab.AddGuest(slab.Guest(0));
    DeleteEnv env;
    env.manager = slab.manager.get();
    ProcessDeletedObjects(*slab.world, env);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("world_objects.process_deleted_reads_the_id_after_the_exit_state_ran")
{
    Slab slab;
    auto* guest = slab.Guest(0);
    const uint32_t guestId = guest->m_id;
    guest->m_waitingForDeath = 1;
    slab.AddGuest(guest);
    DeleteEnv env;
    env.manager = slab.manager.get();
    env.idAfterExit = 777;
    ProcessDeletedObjects(*slab.world, env);
    const std::vector<std::string> expected = {
            "deleting " + std::to_string(guestId), "exit " + std::to_string(guestId), "delete 777", "refresh"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_objects.guest_leaving_counts_the_departure_by_gender_and_asks_every_building")
{
    Slab slab;
    auto* b1 = slab.Building();
    auto* b2 = slab.Building();
    slab.AddBuilding(b1);
    slab.AddBuilding(b2);
    slab.world->m_numGuests = 5;
    auto* guest = slab.Guest(1, false, kStateLeaving);
    DeletingEnv env;
    OnObjectDeleting(*slab.world, *guest, env);
    OBL_CHECK_EQ(slab.world->m_numGuests, 4);
    OBL_CHECK_EQ(slab.world->m_totalGuestDepart[1], 1);
    OBL_CHECK_EQ(slab.world->m_todayGuestDepart[1], 1);
    OBL_CHECK_EQ(slab.world->m_totalGuestDepart[0], 0);
    OBL_CHECK_EQ(slab.world->m_totalGuestUnhappyDepart[1], 0);
    OBL_CHECK_EQ(slab.world->m_todayGuestUnhappyDepart[1], 0);
    const std::string id = std::to_string(guest->m_id);
    const std::vector<std::string> expected = {"dialog " + id, "leaving " + id, "thoughts " + std::to_string(b1->m_id) + " of " + id,
            "thoughts " + std::to_string(b2->m_id) + " of " + id, "feedback " + id};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_objects.guest_leaving_early_is_also_an_unhappy_departure")
{
    Slab slab;
    auto* guest = slab.Guest(0, true, kStateLeaving);
    DeletingEnv env;
    OnObjectDeleting(*slab.world, *guest, env);
    OBL_CHECK_EQ(slab.world->m_totalGuestDepart[0], 1);
    OBL_CHECK_EQ(slab.world->m_todayGuestDepart[0], 1);
    OBL_CHECK_EQ(slab.world->m_totalGuestUnhappyDepart[0], 1);
    OBL_CHECK_EQ(slab.world->m_todayGuestUnhappyDepart[0], 1);
}

OBL_TEST_CASE("world_objects.guest_removed_in_another_state_leaves_only_the_count")
{
    Slab slab;
    slab.world->m_numGuests = 3;
    auto* guest = slab.Guest(1, true, 7);
    DeletingEnv env;
    OnObjectDeleting(*slab.world, *guest, env);
    OBL_CHECK_EQ(slab.world->m_numGuests, 2);
    OBL_CHECK_EQ(slab.world->m_totalGuestDepart[1], 0);
    OBL_CHECK_EQ(slab.world->m_todayGuestUnhappyDepart[1], 0);
    const std::string id = std::to_string(guest->m_id);
    const std::vector<std::string> expected = {"dialog " + id, "feedback " + id};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_objects.building_removed_leaves_the_building_count")
{
    Slab slab;
    slab.world->m_numBuildings = 4;
    auto* building = slab.Building();
    DeletingEnv env;
    OnObjectDeleting(*slab.world, *building, env);
    OBL_CHECK_EQ(slab.world->m_numBuildings, 3);
    const std::string id = std::to_string(building->m_id);
    const std::vector<std::string> expected = {"dialog " + id, "feedback " + id};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_objects.litter_leaves_the_litter_count_and_other_scenery_the_scenic_value")
{
    Slab slab;
    slab.world->m_numLitter = 2;
    slab.world->m_scenicVal = 10.0f;
    auto* litter = slab.Scenery("Litter1", 3.0f);
    DeletingEnv env;
    OnObjectDeleting(*slab.world, *litter, env);
    OBL_CHECK_EQ(slab.world->m_numLitter, 1);
    OBL_CHECK_EQ(slab.world->m_scenicVal, 10.0f); // rubbish has a rating too, but it is never subtracted
    OBL_CHECK_EQ(env.calls.size(), 2u);

    auto* palm = slab.Scenery("PalmTree", 2.5f);
    DeletingEnv palmEnv;
    OnObjectDeleting(*slab.world, *palm, palmEnv);
    OBL_CHECK_EQ(slab.world->m_numLitter, 1);
    OBL_CHECK_EQ(slab.world->m_scenicVal, 7.5f);
    const std::string id = std::to_string(palm->m_id);
    const std::vector<std::string> expected = {"dialog " + id, "feedback " + id};
    OBL_CHECK(palmEnv.calls == expected);
}
