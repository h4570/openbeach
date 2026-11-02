#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/world/world_state.h"
#include "game/objects/staff_duties.h"
#include "game/messages/message_handler.h"

#include <cstdint>

// The small duties of staff (the logic is staff_duties.h).
namespace obl::object {

namespace {

struct DayEnv {
    float DayTime() const { return game::GetWorld()->m_gameDayTime; }
};

struct BinEnv {
    Base* Object(int32_t id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    void Post(messages::Message& message) { messages::message_facade::Post(messages::message_facade::Current(), &message); }
};

} // namespace

void Staff::SetShift(int32_t start, int32_t end)
{
    staff_duties::SetShift(*this, start, end);
}

bool Staff::IsOnShift()
{
    DayEnv env;
    return staff_duties::IsOnShift(*this, env);
}

void Builder::SetNumber(int32_t number)
{
    staff_duties::SetBuilderNumber(*this, number);
}

void FaceDirection(ui::SpriteInstance& sprite, int32_t direction)
{
    staff_duties::SetDirection(sprite, direction);
}

void SendCleanerToBin(int32_t binId, Base& cleaner, float* point)
{
    BinEnv env;
    staff_duties::MoveToBin(binId, cleaner, point, env);
}
} // namespace obl::object
