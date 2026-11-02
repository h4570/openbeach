#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/objects/staff_call.h"
#include "game/objects/lifeguard_fns.h"
#include "game/ui/rep_security.h"
#include "game/objects/staff_feedback.h"
#include "game/objects/object_staff.h"
#include "game/objects/staff_text.h"
#include "game/objects/entity_accessors.h"
#include "game/construction/construction_manager.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/feedback/feedback_manager.h"
#include "game/map/grid_state.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/tables/building_behavior.h"
#include "game/objects/staff_roster.h"
#include "game/objects/object_manager_queries.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "game/objects/staff_call_function.h"
#include "game/objects/staff_call_rep.h"
#include "game/objects/staff_call_security.h"
#include "game/objects/staff_call_builder.h"
#include "game/objects/staff_call_cleaner.h"
#include "game/objects/staff_call_lifeguard.h"
#include "game/objects/staff_call_mechanic.h"
#include "core/call_counter.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/messages/message_handler.h"
#include "game/repair/repair_manager.h"
#include "game/objects/staff_queries.h"
#include "game/world/world_params.h"
#include "game/game/diagnostics.h"
#include "game/game/game_api.h"
#include "game/objects/object_static.h"
#include "game/objects/object_scenery.h"
#include "game/construction/builder_script_queries.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <type_traits>

namespace obl::object {
using feedback::Feedback;
using game::FatalError;
using game::GetGrid;
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;
using game::NumBuildersWhistling;
using game::Rand;
using ui::RepArrivedAtGuest;
using ui::RepLookForGuests;
using ui::RepMoveToNextBuilding;
using ui::SecurityChooseAction;
using ui::SecurityLookForOffender;

namespace {
using namespace layout;
using map::Grid;
using messages::Message;
using object::Builder; // object_staff.h
using object::Cleaner;
using object::Lifeguard;
using object::Mechanic;
using object::Rep;
using object::SecurityGuard;
using ui::SpriteInstance;
namespace call = staff::call;

test::CallCounter g_counter("objects.staff-call-function");

struct GameEnv {
    object::Staff& employee;
    void FindWanderPos() { object::staff_facade::FindWanderPos(&employee); }
    void PushThought(uint32_t bits) { StaffPushThought(employee, bits); }
    void DirectToObject(int32_t id) { employee.DirectToObject(id); }
    void Fatal(const char* text) { FatalError(text); }
};

void RunCall(object::Staff* employee, int32_t id)
{
    g_counter.Count();
    GameEnv env{*employee};
    call::CallFunction(*employee, id, env);
}

struct RepEnv : GameEnv {
    void MoveToNextBuilding() { RepMoveToNextBuilding(static_cast<Rep&>(employee)); }
    void LookForGuests() { RepLookForGuests(static_cast<Rep&>(employee)); }
    void ArrivedAtGuest() { RepArrivedAtGuest(static_cast<Rep&>(employee)); }
    void UpdateFeedback() { RepUpdateFeedback(static_cast<Rep&>(employee)); }
    void Blink(const char* icon, bool blink) { reinterpret_cast<ui::GuiManager*>(GetGuiManager())->BlinkActionButtons(icon, blink); }
    void SelectTab(const char* dialog, int32_t tab) { GetGuiManager()->SelectDialogTab(dialog, tab); }
    void Select() { employee.OnSelect(1, 1); }
    const char* FeedbackKey(uint32_t flags) { return employee.GetFeedbackText(flags); }
    void AddFeedback(const char* key, int32_t id, uint32_t type) { feedback::Feedback::OfWorld().AddKey(key, id, type); }
    void BaseCall(int32_t id) { employee.object::Staff::CallFunction(id); }
};

struct SecurityEnv : RepEnv {
    uint8_t LookForOffender(float*) { return SecurityLookForOffender(static_cast<SecurityGuard&>(employee)); }
    void ChooseAction() { SecurityChooseAction(static_cast<SecurityGuard&>(employee)); }
    const float* ServePosition(int32_t id, float* out)
    {
        auto* object = object::object_manager::ObjectById(GetObjectManager(), id);
        return building_api::GetServePosition(static_cast<const object::Building*>(object), out);
    }
    void FaceObject(int32_t id)
    {
        auto* object = object::object_manager::ObjectById(GetObjectManager(), id);
        float point[3];
        const float x = employee.m_WorldXYZ[0], y = employee.m_WorldXYZ[1], z = employee.m_WorldXYZ[2];
        entity::accessors::GetPosition(*object, point);
        (static_cast<SpriteInstance*>(employee.m_sprite))->FaceTowards(point[0], point[1], point[2], x, y, z);
    }
    void CollectFine(const char* key)
    {
        auto* params = GetWorldParams();
        const int32_t id = world::world_params::GetFineId(params, key);
        const float cost = world::world_params::GetFineRecord(params, id)->m_cost;
        auto* world = GetWorld();
        world->m_player->m_cash += cost;
        world->m_incomeToday += cost;
        world->m_fineIncomeToday += cost;
        world->m_totalFines += cost;
    }
};
test::CallCounter g_security("objects.security-guard-call-function");
void RunSecurityCall(object::Staff* employee, int32_t id)
{
    g_security.Count();
    SecurityEnv env{{{*employee}}};
    call::SecurityCallFunction(*static_cast<SecurityGuard*>(employee), id, env);
}

struct BuilderEnv : RepEnv {
    float DayTime() { return GetWorld()->m_gameDayTime; }
    void FindNearestValidTile() { construction::FindNearestValidTile(static_cast<object::Builder*>(&employee)); }
    void CheckForFemaleGuests() { construction::CheckForFemaleGuests(static_cast<object::Builder*>(&employee)); }
    int32_t& Whistling() { return NumBuildersWhistling(); }
    void RemoveCostPoint(int32_t id) { construction::runtime::RemoveCostPoint(GetWorld()->m_constructionManager, id); }
    void FreeJob(int32_t id) { construction::runtime::FreeBuilderOfJob(GetWorld()->m_constructionManager, id); }
    void FreeJobNoBuildCheck(int32_t id) { construction::runtime::ClearBuilderJobEntries(GetWorld()->m_constructionManager, id); }
    void ReallocateBuilders() { construction::runtime::ReallocateBuilders(GetWorld()->m_constructionManager); }
    int32_t DayLength()
    {
        int32_t length = 0;
        game::game_parameters::GetInt(game::game_parameters::Current(), &length, "iDAY_LENGTH_IN_SECONDS");
        return length;
    }
    object::Base* Object(int32_t id) { return object::object_manager::ObjectById(GetObjectManager(), id); }
    bool HasDemolishJob(object::Base* object) { return building_api::HasDemolishJob(static_cast<const object::Building*>(object)); }
    int Rand() { return game::Rand(); }
    void PostDelayed(const Message& message, float delay)
    {
        Message copy = message;
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &copy, delay);
    }
    void ClearDelayed(int32_t id) { messages::message_facade::ClearDelayedForReceiver(messages::message_facade::Current(), id); }
};
test::CallCounter g_builder("objects.builder-call-function");
void RunBuilderCall(object::Staff* employee, int32_t id)
{
    g_builder.Count();
    BuilderEnv env{{{*employee}}};
    call::BuilderCallFunction(*static_cast<Builder*>(employee), id, env);
}

struct CleanerEnv : BuilderEnv {
    int32_t FindJob(bool urgent, int32_t& type)
    {
        const auto& cleaner = static_cast<const Cleaner&>(employee);
        float point[3];
        std::memcpy(point, employee.m_WorldXYZ, sizeof(point));
        cleaning::Manager* manager = GetWorld()->m_cleaningManager;
        // The three flags keep their positions: the urgent search reads them as bins, scenery and buildings (in that order).
        const bool bins = cleaner.m_cleanBins, litter = cleaner.m_cleanLitter, toilets = cleaner.m_cleanToilets;
        if (urgent) return cleaning::cleaning_facade::FindUrgentJob(manager, &type, bins, litter, toilets);
        return cleaning::cleaning_facade::FindBestScoredJob(manager, point, &type, bins, litter, toilets);
    }
    void CancelJob(int32_t id) { cleaning::cleaning_facade::CancelJob(GetWorld()->m_cleaningManager, id); }
    void DidNotClean(int32_t id) { cleaning::cleaning_facade::DidNotClean(GetWorld()->m_cleaningManager, id); }
    void StartedCleaning(int32_t id) { cleaning::cleaning_facade::StartedCleaning(GetWorld()->m_cleaningManager, id); }
    void SetMoving(object::Base* object, bool moving) { object::SetMovingFlag(*object, moving); }
    void SetCalledCleaning(object::Base* object, bool called)
    {
        static_cast<object::Prop*>(object)->SetUrgentCleaningCalled(called ? 1 : 0);
    }
    void SetCleanerClaimed(object::Base* object, bool claimed) { static_cast<object::Prop*>(object)->SetCleanerClaimed(claimed ? 1 : 0); }
    void SetBinFill(object::Base* object, uint32_t fill) { static_cast<object::Scenery*>(object)->SetBinFill(fill); }
    void MoveToBin(int32_t id, float* point) { object::SendCleanerToBin(id, employee, point); }
    void FaceSameDirection(int32_t id)
    {
        const int32_t direction = static_cast<const SpriteInstance*>(Object(id)->m_sprite)->m_direction;
        object::FaceDirection(*static_cast<SpriteInstance*>(employee.m_sprite), direction);
    }
    void FaceObjectIfPresent(int32_t id)
    {
        auto* object = Object(id);
        if (!object) return;
        float point[3];
        const float x = employee.m_WorldXYZ[0], y = employee.m_WorldXYZ[1], z = employee.m_WorldXYZ[2];
        entity::accessors::GetPosition(*object, point);
        (static_cast<SpriteInstance*>(employee.m_sprite))->FaceTowards(point[0], point[1], point[2], x, y, z);
    }
    void Post(const Message& message)
    {
        Message copy = message;
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
};
test::CallCounter g_cleaner("objects.cleaner-call-function");
void RunCleanerCall(object::Staff* employee, int32_t id)
{
    g_cleaner.Count();
    CleanerEnv env{{{{*employee}}}};
    call::CleanerCallFunction(*static_cast<Cleaner*>(employee), id, env);
}

struct MechanicEnv : BuilderEnv {
    int32_t FindRepairJob(int32_t filter, int32_t& type)
    {
        float point[3];
        std::memcpy(point, employee.m_WorldXYZ, sizeof(point));
        // The type out-parameter is int32_t here and uint32_t in the facade: the same four bytes.
        return repair::repair_facade::FindJob(GetWorld()->m_repairManager, point, filter, reinterpret_cast<uint32_t*>(&type));
    }
    int32_t BetterJob(int32_t current, int32_t& type)
    {
        float point[3];
        std::memcpy(point, employee.m_WorldXYZ, sizeof(point));
        return repair::repair_facade::LookForBetterJob(GetWorld()->m_repairManager, point, current, reinterpret_cast<uint32_t*>(&type));
    }
    int32_t WateringJob()
    {
        float point[3];
        std::memcpy(point, employee.m_WorldXYZ, sizeof(point));
        return repair::repair_facade::FindWateringJob(GetWorld()->m_repairManager, point);
    }
    void ReturnJob(int32_t id, uint32_t type) { repair::repair_facade::ReturnJob(GetWorld()->m_repairManager, id, type); }
    void ReleaseJob(int32_t id) { repair::repair_facade::ReleaseJob(GetWorld()->m_repairManager, id); }
    void StartedJob(int32_t id) { repair::repair_facade::StartedJob(GetWorld()->m_repairManager, id); }
};
test::CallCounter g_mechanic("objects.mechanic-call-function");
void RunMechanicCall(object::Staff* employee, int32_t id)
{
    g_mechanic.Count();
    MechanicEnv env{{{*employee}}};
    call::MechanicCallFunction(*static_cast<Mechanic*>(employee), id, env);
}

struct LifeguardEnv : CleanerEnv {
    object::Lifeguard& Lifeguard() { return static_cast<object::Lifeguard&>(employee); }
    void HeadForShore() { LifeguardHeadForShore(Lifeguard()); }
    void MoveToTower() { LifeguardMove2Tower(Lifeguard()); }
    void FindBeachPoint() { LifeguardFindBeachPoint(Lifeguard()); }
    void Move2SeaEntry() { LifeguardMove2SeaEntry(Lifeguard()); }
    void SetDrawPosBuild() { LifeguardSetDrawPosBuild(Lifeguard()); }
    void SetDrawPosServe() { LifeguardSetDrawPosServe(Lifeguard()); }
    void FindBeach() { LifeguardFindBeach(Lifeguard()); }
    object::Base* Hotel()
    {
        const auto hotels = obl::object_manager::FindBuildings(*GetObjectManager(), BuildingBehavior::Hotel);
        return hotels.empty() ? nullptr : hotels.front();
    }
    void Teleport(object::Base* object, float x, float y, float z) { static_cast<object::Mover*>(object)->Teleport(x, y, z); }
    void SetDirection(SpriteInstance& sprite, int32_t direction) { object::FaceDirection(sprite, direction); }
    int32_t SeaArea(const float* point)
    {
        auto* grid = GetGrid();
        return grid->GetSeaAreaOfPoint(point[0], point[1], point[2]);
    }
};
test::CallCounter g_lifeguard("objects.lifeguard-call-function");
void RunLifeguardCall(object::Staff* employee, int32_t id)
{
    g_lifeguard.Count();
    LifeguardEnv env{{{{{*employee}}}}};
    call::LifeguardCallFunction(*static_cast<object::Lifeguard*>(employee), id, env);
}

test::CallCounter g_rep("objects.rep-call-function");
void RunRepCall(object::Staff* employee, int32_t id)
{
    g_rep.Count();
    RepEnv env{{*employee}};
    call::RepCallFunction(*static_cast<Rep*>(employee), id, env);
}
} // namespace

void Staff::CallFunction(int id)
{
    RunCall(this, id);
}
void Builder::CallFunction(int id)
{
    RunBuilderCall(this, id);
}
void Cleaner::CallFunction(int id)
{
    RunCleanerCall(this, id);
}
void Lifeguard::CallFunction(int id)
{
    RunLifeguardCall(this, id);
}
void Mechanic::CallFunction(int id)
{
    RunMechanicCall(this, id);
}
void Rep::CallFunction(int id)
{
    RunRepCall(this, id);
}
void SecurityGuard::CallFunction(int id)
{
    RunSecurityCall(this, id);
}
} // namespace obl::object
