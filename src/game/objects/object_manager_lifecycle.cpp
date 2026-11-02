#include "game/ui/furniture_runtime.h"
#include "game/objects/staff_snooty.h"
#include "game/objects/scenery_lifecycle.h"
#include "game/objects/performer_virtuals.h"
#include "game/objects/guest_lifecycle_driver.h"
#include "game/objects/object_base.h"

#include "game/game/game_accessors.h"
#include "game/objects/object_base.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_performers.h"
#include "game/objects/object_shark.h"
#include "game/objects/object_table.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/object_building.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_manager_lifecycle.h"
#include "game/objects/object_manager_serial.h"
#include "game/game/save_load.h"
#include "core/call_counter.h"
#include "game/game/diagnostics.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

// Creating and deleting objects of every type, and the manager's own life cycle (the logic is object_manager_lifecycle.h and
// object_manager_serial.h).
namespace obl::object {
using game::FatalError;

namespace {

namespace om = obl::object_manager;

test::CallCounter g_lifecycle("objects.manager-lifecycle");
test::CallCounter g_construct("objects.manager-construct"), g_destruct("objects.manager-destruct");

// The object of one type: the member initializers zero every field, then each class's constructor, base first, sets its own part.
// Every field is set, so nothing of an earlier heap block survives.
template<class T> object::Base* Create(const char* name)
{
    static_assert(std::has_virtual_destructor_v<T>, "the manager deletes objects through object::Base*");
    return new T(name);
}

// The creator per type number 1..20.
using CreateFunc = object::Base* (*)(const char* name);
constexpr CreateFunc kClasses[] = {
        &Create<object::Scenery>, // 1
        &Create<object::Building>, // 2
        &Create<object::Guest>, // 3
        &Create<object::Vehicle>, // 4
        &Create<object::Furniture>, // 5
        &Create<object::ServiceStaff>, // 6
        &Create<object::Lifeguard>, // 7
        &Create<object::Mechanic>, // 8
        &Create<object::Rep>, // 9
        &Create<object::Builder>, // 10
        &Create<object::Cleaner>, // 11
        &Create<object::SecurityGuard>, // 12
        &Create<object::Shark>, // 13
        &Create<object::Cabaret>, // 14
        &Create<object::Singer>, // 15
        &Create<object::WeightLifter>, // 16
        &Create<object::Catwalk>, // 17
        &Create<object::WetTShirt>, // 18
        &Create<object::SnootyStaff>, // 19
        &Create<object::Aerobic>, // 20
};
static_assert(sizeof(kClasses) / sizeof(kClasses[0]) == om::kLastObjectType);

void GameFatal(const char* message)
{
    FatalError(message);
}

// The manager's deleter: the virtual destructor runs the object's teardown, most derived class first.
void DestroyObject(object::Base* object)
{
    delete object;
}

// The default path: the real allocation, constructors and destructors.
struct GameEnv {
    object::Base* Construct(uint8_t type, const char* name) { return kClasses[type - 1](name); }
    void SetId(object::Base* object, uint32_t id) { object->SetId(id); }
    object::DestroyFunc Destroyer() { return &DestroyObject; }
    void Fatal(const char* message) { GameFatal(message); }
};

// An obl.save load (save-switch-objects.md): the header and every object from the held document, nothing from the stream.
struct LoadEnv : GameEnv {
    void ObjectHeader(uint8_t& type, char* name)
    {
        std::string text;
        game::obl_load::ReadObjectHeader(headers++, type, text);
        headerType = type;
        const size_t length = std::min<size_t>(text.size(), 127); // the record's 128-byte buffer
        std::memcpy(name, text.data(), length);
        name[length] = 0;
    }
    void LoadObject(object::Base* object)
    {
        // Every document kind constructs (ReadObjectHeaderJson refuses the rest), so the slot is never empty. The type comes from
        // the header: the object's own m_type is a record of its body.
        game::obl_load::ReadObject(*object, headerType, loaded++);
    }
    size_t headers = 0; // object headers read so far
    size_t loaded = 0; // objects read so far, the index of the document's entry
    uint8_t headerType = 0; // the type byte of the last object header
};

} // namespace

namespace om = obl::object_manager;

Manager* CreateManager()
{
    g_construct.Count();
    auto* manager = new Manager;
    game::SetObjectManager(manager);
    return manager;
}

void DestroyManager(Manager* manager)
{
    g_destruct.Count();
    om::Destruct(*manager);
    game::SetObjectManager(nullptr);
    delete manager;
}

void Manager::LoadSaved()
{
    om::ApplyHeader(*this, game::obl_load::ReadObjectManager());
    LoadEnv env;
    om::LoadObjects(*this, env);
}

Base* Manager::CreateObject(const char* name, uint8_t type)
{
    g_lifecycle.Count();
    GameEnv env;
    return om::CreateObject(*this, name, type, env);
}

bool Manager::DeleteObject(uint32_t id)
{
    g_lifecycle.Count();
    return om::DeleteObject(*this, id);
}

void Manager::InsertLastBuildingByY()
{
    g_lifecycle.Count();
    om::InsertLastBuildingByY(*this);
}
} // namespace obl::object
