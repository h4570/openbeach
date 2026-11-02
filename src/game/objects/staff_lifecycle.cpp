#include "game/ui/sprite_bank.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "game/objects/staff_roster.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/objects/staff_serial.h"
#include "core/call_counter.h"

#include <array>
#include <cstring>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace obl::object {

namespace {
using namespace layout;
using map::Grid;
using object::Builder; // object_staff.h
using object::Cleaner;
using object::Lifeguard;
using object::Mechanic;
using object::Rep;
using object::SecurityGuard;
using object::ServiceStaff; // the macros paste the class names
using object::SnootyStaff;
using object::Staff;
using ui::SpriteInstance;

template<class T> bool Matches(object::Staff* employee)
{
    if constexpr (std::is_same_v<T, object::Staff>) {
        return true;
    } else {
        return typeid(*employee) == typeid(T);
    }
}
void DriveAttachedFixture(int count);
// `call-drive objects.staff-attached-fixture <count>` (the obl.save round trips): long and short thoughts and attached sprites.
test::CallCounter g_fixture("objects.staff-attached-fixture", &DriveAttachedFixture);
void DriveAttachedFixture(int count)
{
    for (auto* staff : StaffOfEveryClass()) {
        // Long and short UTF-16 strings exercise terminators and the untouched buffer tails on real loads.
        for (int row = 0; row < 3; ++row) {
            const int length = row == 0 ? 0 : row == 1 ? 255 : 17;
            for (int i = 0; i < length; ++i) staff->m_Thoughts[row][i] = static_cast<Char16>(0x100 + i);
            staff->m_Thoughts[row][length] = 0;
        }
        void** target = nullptr;
        if (Matches<Lifeguard>(staff)) target = &static_cast<Lifeguard*>(staff)->m_attachedSprite;
        if (Matches<Rep>(staff)) target = &static_cast<Rep*>(staff)->m_attachedSprite;
        if (!target) continue;
        if (*target) (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(*target);
        *target = nullptr;
        if (count % 2) {
            auto* sprite = static_cast<SpriteInstance*>(staff->m_sprite);
            *target = (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(sprite->m_name);
        }
        g_fixture.Count();
    }
}
} // namespace
} // namespace obl::object

namespace obl::object {} // namespace obl::object
