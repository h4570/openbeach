#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace obl::object {
struct Base;
} // namespace obl::object
namespace obl::layout {}

// The state of the ObjectManager (owned by the World) as a C++ class
// (docs/architecture/objects.md): the three object lists with their parallel id lists, the next free id and the
// keyline flag. The world and the image global hold an opaque pointer to the Manager, made with `new`
// (object_manager_lifecycle.cpp). The query functions return their results as a `std::vector` of their own.
namespace obl::object {

constexpr uint32_t kSavedIdCount = 18500; // m_dwObjectIDList of the save block: buildings, guests and others back to back, padded

// Destroys one object: the game deletes it through its virtual destructor, a unit test records the
// call. A plain function, so the deleter needs no virtual call and no state.
using DestroyFunc = void (*)(object::Base*);
struct ObjectDeleter {
    DestroyFunc destroy = nullptr;
    void operator()(object::Base* object) const { destroy(object); }
};
using ObjectPtr = std::unique_ptr<object::Base, ObjectDeleter>;

struct Manager {
    static constexpr int kMaxBuildings = 500;
    static constexpr int kMaxGuests = 1500;
    static constexpr int kMaxOthers = 16500;
    static_assert(kMaxBuildings + kMaxGuests + kMaxOthers == static_cast<int>(kSavedIdCount));

    // Insertion order of each list is the save order; buildings are kept sorted by Y (InsertLastBuildingByY). The ids are parallel to
    // the objects (same size, same index). A list never grows past its maximum (CreateObject is fatal when it fills), and the
    // constructor reserves that capacity: a walk that holds `data()` while a callee creates an object keeps valid pointers.
    std::vector<object::Base*> m_buildings;
    std::vector<object::Base*> m_guests;
    std::vector<object::Base*> m_others;
    std::vector<uint32_t> m_buildingIds;
    std::vector<uint32_t> m_guestIds;
    std::vector<uint32_t> m_otherIds;
    uint32_t m_nextFreeObjectId = 0;
    uint8_t m_globalKeyline = 0; // SetKeyline <- [Video] Keyline
    // The owner of every object CreateObject and Load made: an object dies when its entry goes (Destroy). A fixture's
    // object added with AddBuilding/AddGuest/AddOther is listed without being owned.
    std::unordered_map<const object::Base*, ObjectPtr> m_owned;
    // Id -> object for every listed object: the grid cells hold ids (cell_objects.h) and resolve them here. The first object
    // listed under an id keeps it (a duplicate id only happens in a fixture or a damaged save).
    std::unordered_map<uint32_t, object::Base*> m_byId;

    Manager()
    {
        m_buildings.reserve(kMaxBuildings);
        m_buildingIds.reserve(kMaxBuildings);
        m_guests.reserve(kMaxGuests);
        m_guestIds.reserve(kMaxGuests);
        m_others.reserve(kMaxOthers);
        m_otherIds.reserve(kMaxOthers);
    }
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    // Creating and deleting objects with the game's constructors (object_manager_lifecycle.cpp). CreateObject is fatal when its
    // list is full; LoadSaved reads the manager and every object of a queued obl.save load.
    object::Base* CreateObject(const char* name, uint8_t type);
    bool DeleteObject(uint32_t id);
    void InsertLastBuildingByY();
    void LoadSaved();

    // m_dwNumActiveObjects of the save: always the sum of the three counts.
    uint32_t ActiveCount() const { return static_cast<uint32_t>(m_buildings.size() + m_guests.size() + m_others.size()); }

    // m_dwObjectIDList of the save: buildings from slot 0, guests from kMaxBuildings, others from kMaxBuildings + kMaxGuests; the unused
    // slots are 0xffffffff.
    std::vector<uint32_t> SavedIds() const
    {
        std::vector<uint32_t> ids(kSavedIdCount, 0xffffffffu);
        std::copy(m_buildingIds.begin(), m_buildingIds.end(), ids.begin());
        std::copy(m_guestIds.begin(), m_guestIds.end(), ids.begin() + kMaxBuildings);
        std::copy(m_otherIds.begin(), m_otherIds.end(), ids.begin() + kMaxBuildings + kMaxGuests);
        return ids;
    }

    void Adopt(object::Base* object, DestroyFunc destroy)
    {
        if (object) m_owned.emplace(object, ObjectPtr(object, ObjectDeleter{destroy}));
    }
    // Runs the deleter of an owned object (nothing for a fixture's object). The entry leaves the map before the destructor runs,
    // so a destructor that deletes other objects does not walk a map in the middle of an erase.
    void Destroy(object::Base* object) { [[maybe_unused]] auto node = m_owned.extract(object); }

    void Index(uint32_t id, object::Base* object)
    {
        if (object) m_byId.emplace(id, object);
    }
    void Unindex(uint32_t id, const object::Base* object)
    {
        const auto it = m_byId.find(id);
        if (it != m_byId.end() && it->second == object) m_byId.erase(it);
    }
    object::Base* Resolve(uint32_t id) const
    {
        const auto it = m_byId.find(id);
        return it == m_byId.end() ? nullptr : it->second;
    }

    // Appends an object with its id (the lifecycle; a fixture may use an id unrelated to the object's own).
    void AddBuilding(object::Base* object, uint32_t id) { Add(m_buildings, m_buildingIds, object, id); }
    void AddGuest(object::Base* object, uint32_t id) { Add(m_guests, m_guestIds, object, id); }
    void AddOther(object::Base* object, uint32_t id) { Add(m_others, m_otherIds, object, id); }

private:
    void Add(std::vector<object::Base*>& objects, std::vector<uint32_t>& ids, object::Base* object, uint32_t id)
    {
        objects.push_back(object);
        ids.push_back(id);
        Index(id, object);
    }
};

// The world's manager: `new` and `delete`, and GetObjectManager() follows them. Destroying it destroys every object first.
Manager* CreateManager();
void DestroyManager(Manager* manager);

} // namespace obl::object
