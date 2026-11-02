#pragma once

// Shared fake environment and object helpers for the staff_call_<profession> tests. One Env serves every profession dispatcher
// (members a dispatcher never calls are simply unused); it records each call as text so a test compares whole call sequences.

#include "object_storage.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace staff_call_test {

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::ObjectType;
using obl::ui::Sprite;
using obl::ui::SpriteAnim;
using obl::ui::SpriteInstance;

template<class T> std::unique_ptr<T> Zeroed()
{
    auto object = std::make_unique<T>();
    obl::unit::FillObject(*object);
    return object;
}

// A sprite instance with one animation of `directions` directions, attached to `object`.
struct SpriteRig {
    SpriteInstance instance;
    Sprite sprite;
    SpriteAnim anim;
    SpriteAnim* table[1];

    explicit SpriteRig(object::Base& object, uint8_t directions = 8, int32_t direction = 0)
    {
        std::memset(static_cast<void*>(&instance), 0, sizeof(instance));
        std::memset(static_cast<void*>(&sprite), 0, sizeof(sprite));
        std::memset(static_cast<void*>(&anim), 0, sizeof(anim));
        anim.m_directions = directions;
        table[0] = &anim;
        sprite.m_animCount = 1;
        sprite.m_anims = table;
        instance.m_sprite = &sprite;
        instance.m_direction = direction;
        object.m_sprite = &instance;
    }
    SpriteRig(const SpriteRig&) = delete;
    SpriteRig& operator=(const SpriteRig&) = delete;
};

inline std::string Fmt(const char* format, double a = 0, double b = 0, double c = 0)
{
    char text[128];
    std::snprintf(text, sizeof(text), format, a, b, c);
    return text;
}

struct FakeEnv {
    std::string log;
    std::map<int32_t, object::Base*> objects;
    object::Base* hotel = nullptr;
    const char* feedbackKey = nullptr;
    float dayTime = 12.5f;
    float dayLength = 240.0f;
    int32_t rand = 0; // returned by Rand() when `randQueue` is empty
    std::map<int, int> repairJobs; // FindRepairJob(kind) -> job id (missing = -1)
    int32_t betterJob = -1, wateringJob = -1, seaArea = 0, findJobResult = -1, findJobType = 0, whistling = 7;
    bool offender = false, demolish = false;
    float servePoint[3] = {};
    int32_t repairTypeAfterFind = -1; // when >= 0, FindRepairJob writes it to the repair-type out parameter
    Message posted{};
    float postedDelay = 0;

    void Add(const std::string& text) { log += text + " "; }

    object::Base* Object(int32_t id)
    {
        Add("object(" + std::to_string(id) + ")");
        auto it = objects.find(id);
        return it == objects.end() ? nullptr : it->second;
    }
    void PushThought(uint32_t bits)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "thought(%x)", bits);
        Add(text);
    }
    void Blink(const char* icon, bool on) { Add(std::string("blink(") + icon + "," + (on ? "1" : "0") + ")"); }
    void Select() { Add("select"); }
    void SelectTab(const char* dialog, int tab) { Add(std::string("tab(") + dialog + "," + std::to_string(tab) + ")"); }
    const char* FeedbackKey(int32_t feedback)
    {
        Add("key(" + std::to_string(feedback) + ")");
        return feedbackKey;
    }
    void AddFeedback(const char* key, uint32_t id, int type)
    {
        Add(std::string("feedback(") + key + "," + std::to_string(id) + "," + std::to_string(type) + ")");
    }
    void BaseCall(int32_t id) { Add("base(" + std::to_string(id) + ")"); }
    void Post(const Message& message)
    {
        posted = message;
        Add("post");
    }
    void PostDelayed(const Message& message, float delay)
    {
        posted = message;
        postedDelay = delay;
        Add("delayed");
    }
    float DayTime() { return dayTime; }
    float DayLength() { return dayLength; }
    int32_t Rand() { return rand; }

    // Lifeguard.
    void HeadForShore() { Add("shore"); }
    const object::Base* Hotel() { return hotel; }
    void MoveToTower() { Add("tower"); }
    void FindBeachPoint() { Add("beachpoint"); }
    void Move2SeaEntry() { Add("seaentry"); }
    void SetDrawPosBuild() { Add("drawbuild"); }
    void SetDrawPosServe() { Add("drawserve"); }
    void FindBeach() { Add("beach"); }
    int32_t SeaArea(const float*) { return seaArea; }
    void Teleport(object::Base* object, float x, float y, float z)
    {
        Add(Fmt("teleport(%g,%g,%g)", x, y, z));
        object->m_WorldXYZ[0] = x;
        object->m_WorldXYZ[1] = y;
        object->m_WorldXYZ[2] = z;
    }
    void SetDirection(SpriteInstance& sprite, int32_t direction)
    {
        Add("dir(" + std::to_string(direction) + ")");
        sprite.m_direction = direction;
    }

    // Mechanic.
    int32_t FindRepairJob(int32_t kind, int32_t& repairType)
    {
        Add("find(" + std::to_string(kind) + ")");
        if (repairTypeAfterFind >= 0) repairType = repairTypeAfterFind;
        auto it = repairJobs.find(kind);
        return it == repairJobs.end() ? -1 : it->second;
    }
    int32_t BetterJob(int32_t, int32_t) { return betterJob; }
    int32_t WateringJob() { return wateringJob; }
    void ReturnJob(int32_t id, int32_t kind) { Add("return(" + std::to_string(id) + "," + std::to_string(kind) + ")"); }
    void ReleaseJob(int32_t id) { Add("release(" + std::to_string(id) + ")"); }
    void StartedJob(int32_t id) { Add("started(" + std::to_string(id) + ")"); }

    // Cleaner.
    int32_t FindJob(bool urgent, int32_t& type)
    {
        Add(urgent ? "findjob(urgent)" : "findjob(any)");
        type = findJobType;
        return findJobResult;
    }
    void CancelJob(int32_t id) { Add("cancel(" + std::to_string(id) + ")"); }
    void SetMoving(object::Base*, bool on) { Add(on ? "moving(1)" : "moving(0)"); }
    void SetCalledCleaning(object::Base*, bool on) { Add(on ? "called(1)" : "called(0)"); }
    void SetCleanerClaimed(object::Base*, bool on) { Add(on ? "claimed(1)" : "claimed(0)"); }
    void SetBinFill(object::Base*, int32_t value) { Add("binfill(" + std::to_string(value) + ")"); }
    void MoveToBin(int32_t id, const float* pos) { Add("tobin(" + std::to_string(id) + "," + Fmt("%g,%g)", pos[0], pos[1])); }
    void FaceSameDirection(int32_t id) { Add("same(" + std::to_string(id) + ")"); }
    void FaceObjectIfPresent(int32_t id) { Add("ifpresent(" + std::to_string(id) + ")"); }
    void DidNotClean(int32_t id) { Add("didnot(" + std::to_string(id) + ")"); }
    void StartedCleaning(int32_t id) { Add("startedclean(" + std::to_string(id) + ")"); }

    // Rep.
    void MoveToNextBuilding() { Add("nextbuilding"); }
    void LookForGuests() { Add("lookguests"); }
    void ArrivedAtGuest() { Add("arrived"); }
    void UpdateFeedback() { Add("updatefeedback"); }

    // Security guard.
    bool LookForOffender(float*) { return offender; }
    void CollectFine(const char* kind) { Add(std::string("fine(") + kind + ")"); }
    const float* ServePosition(int32_t home, float* out)
    {
        Add("serve(" + std::to_string(home) + ")");
        std::memcpy(out, servePoint, sizeof(servePoint));
        return out;
    }
    void FaceObject(int32_t id) { Add("face(" + std::to_string(id) + ")"); }
    void ChooseAction() { Add("choose"); }

    // Builder.
    void FindNearestValidTile() { Add("tile"); }
    void CheckForFemaleGuests() { Add("females"); }
    int32_t& Whistling() { return whistling; }
    void RemoveCostPoint(int32_t id) { Add("costpoint(" + std::to_string(id) + ")"); }
    void FreeJob(uint32_t id) { Add("free(" + std::to_string(id) + ")"); }
    void FreeJobNoBuildCheck(uint32_t id) { Add("freenocheck(" + std::to_string(id) + ")"); }
    void ReallocateBuilders() { Add("realloc"); }
    bool HasDemolishJob(object::Base*) { return demolish; }
    void ClearDelayed(uint32_t id) { Add("cleardelayed(" + std::to_string(id) + ")"); }
};

} // namespace staff_call_test
