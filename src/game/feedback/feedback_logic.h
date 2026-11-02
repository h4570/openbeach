#pragma once

#include "feedback_state.h"
#include "feedback_vocabulary.h"
#include "game/objects/object_base.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace obl::feedback {

constexpr size_t kPendingLimit = 16; // the oldest waiting notice is dropped when a 17th arrives
constexpr size_t kHistoryLimit = 6; // the bar remembers the last six shown ones
constexpr size_t kLongQueue = 5; // more than this many waiting notices are shown in the short balloon
constexpr int kLongBalloon = 2, kShortBalloon = 1;
constexpr int kNoGoTo = -2; // object id of a notice without a "go to" button (also -1 after the object died)
constexpr uint32_t kComplaintThreshold = 5;
constexpr int kSunbathing = 12, kSwimmingFun = 13; // families that never produce a complaint notice
constexpr int kComplaintType = 5; // the message type passed to Add for a complaint (suppressible like every type)
constexpr double kReviewInterval = 0.5; // game hours between two complaint reviews

// The text keys of the complaint notices by commodity family (the pack's vocabulary); null for a family outside 0..25.
inline const char* CommodityMessageKey(uint32_t family)
{
    const std::vector<std::string>& keys = CurrentVocabulary().commodityKeys;
    return family < keys.size() ? keys[family].c_str() : nullptr;
}

// The mask of a staff class (object type 7 lifeguard, 8 mechanic, 9 rep, 10 builder, 11 cleaner, 12 security guard, 6 other staff); null for
// every other type. Get/Mark/Clear differ only in what they do with it.
inline uint32_t* StaffMask(Manager& manager, int objectType)
{
    using object::ObjectType;
    if (objectType < 0 || objectType > 0xff) return nullptr; // the enum's byte would wrap a larger number onto a class
    switch (static_cast<ObjectType>(objectType)) {
    case ObjectType::Lifeguard: return &manager.lifeGuardMessages;
    case ObjectType::Cleaner: return &manager.cleanerMessages;
    case ObjectType::SecurityGuard: return &manager.securityGuardMessages;
    case ObjectType::Mechanic: return &manager.mechanicMessages;
    case ObjectType::Rep: return &manager.repMessages;
    case ObjectType::Builder: return &manager.builderMessages;
    case ObjectType::ServiceStaff: return &manager.staffMessages;
    default: return nullptr;
    }
}

inline uint32_t GetStaffMessages(Manager& manager, int objectType)
{
    const uint32_t* mask = StaffMask(manager, objectType);
    return mask ? *mask : 0;
}
inline void MarkStaffMessage(Manager& manager, uint32_t bits, int objectType)
{
    if (uint32_t* mask = StaffMask(manager, objectType)) *mask |= bits;
}
inline void ClearStaffMessages(Manager& manager, int objectType)
{
    if (uint32_t* mask = StaffMask(manager, objectType)) *mask = 0;
}

inline void AddScenarioMessage(Manager& manager, const char* word)
{
    for (const MessageType& entry : CurrentVocabulary().messageTypes) {
        if (entry.word != word) continue;
        if (entry.suppressWorld || entry.suppressObject) {
            if (entry.suppressWorld) manager.suppressWorld = 1;
            if (entry.suppressObject) manager.suppressObject = 1;
            return;
        }
        if (manager.suppressedCount < static_cast<uint32_t>(kMaxSuppressed)) manager.suppressed[manager.suppressedCount++] = entry.type;
        return;
    }
}

// ---- history -------------------------------------------------------------------------------------------------------------------------

// A history record stamped with the current day and hour; the oldest record drops out beyond six.
template<class Env> void AppendHistory(Manager& manager, Env& env, Text text, int objectId)
{
    manager.history.push_back({objectId, env.GameDay(), env.GameHour(), std::move(text)});
    if (manager.history.size() > kHistoryLimit) manager.history.pop_front();
}

template<class Env> void AppendHistoryKey(Manager& manager, Env& env, const char* key, int objectId)
{
    AppendHistory(manager, env, Text::Key(key), objectId);
}

template<class Env> void AppendHistoryText(Manager& manager, Env& env, const char16_t* text, int objectId)
{
    AppendHistory(manager, env, Text::Wide(text), objectId);
}

inline const HistoryEntry* BeginHistory(Manager& manager)
{
    manager.historyCursor = 0;
    return manager.history.empty() ? nullptr : &manager.history.front();
}
inline const HistoryEntry* NextHistory(Manager& manager)
{
    if (manager.historyCursor < manager.history.size()) ++manager.historyCursor;
    return manager.historyCursor < manager.history.size() ? &manager.history[manager.historyCursor] : nullptr;
}

// ---- notices -------------------------------------------------------------------------------------------------------------------------

// The type filter shared by both Add methods: only the low byte of the caller's type is compared with the silenced ones.
inline bool IsSuppressed(const Manager& manager, uint32_t type)
{
    for (uint32_t i = 0; i < manager.suppressedCount && i < static_cast<uint32_t>(kMaxSuppressed); ++i)
        if (manager.suppressed[i] == (type & 0xff)) return true;
    return false;
}

// A notice that has to wait: the oldest one makes room when sixteen are already waiting.
inline void Enqueue(Manager& manager, Text text, int objectId)
{
    if (manager.pending.size() >= kPendingLimit) manager.pending.pop_front();
    manager.pending.push_back({objectId, std::move(text)});
}

template<class Env> void Add(Manager& manager, Env& env, const char* key, int objectId, uint32_t type)
{
    if (IsSuppressed(manager, type)) return;
    if (env.CanShowMessage()) {
        env.ShowMessageKey(key, kLongBalloon);
        AppendHistoryKey(manager, env, key, objectId);
        return;
    }
    Enqueue(manager, Text::Key(key), objectId);
}

template<class Env> void AddWide(Manager& manager, Env& env, const char16_t* text, int objectId, uint32_t type)
{
    if (IsSuppressed(manager, type)) return;
    if (env.CanShowMessage()) {
        env.ShowMessage(text, kLongBalloon);
        AppendHistoryText(manager, env, text, objectId);
        return;
    }
    Enqueue(manager, Text::Wide(text), objectId);
}

inline void InvalidateObject(Manager& manager, int objectId)
{
    for (HistoryEntry& entry : manager.history)
        if (entry.objectId == objectId) entry.objectId = -1;
    for (Notice& notice : manager.pending)
        if (notice.objectId == objectId) notice.objectId = -1;
}

inline void RemoveByObject(Manager& manager, uint32_t objectId)
{
    std::erase_if(manager.pending, [&](const Notice& notice) { return static_cast<uint32_t>(notice.objectId) == objectId; });
}

inline void RemoveByText(Manager& manager, const char* key)
{
    std::erase_if(manager.pending, [&](const Notice& notice) { return !notice.text.wide && notice.text.key == key; });
}

template<class Env> void ReviewComplaints(Manager& manager, Env& env)
{
    const double hour = env.GameHour();
    const double last = manager.lastReviewHour;
    // Not due yet while less than half an hour passed; a later `last` (day wrap) is compared across midnight; 25 = never reviewed.
    if (!(hour - last > kReviewInterval)) {
        if (!(last > hour)) return;
        if (!(hour + 24.0 - last > kReviewInterval)) return;
    }
    uint32_t* counts = env.FamilyCounts();
    int candidates = 0;
    for (int family = 0; family < kFamilies; ++family)
        if (counts[family] > kComplaintThreshold && family != kSunbathing && family != kSwimmingFun) ++candidates;
    if (candidates != 0) {
        int pick = env.Rand() % candidates;
        for (int family = 0; family < kFamilies; ++family) {
            if (counts[family] <= kComplaintThreshold) continue;
            if (family != kSunbathing && family != kSwimmingFun) {
                if (pick == 0) Add(manager, env, CommodityMessageKey(static_cast<uint32_t>(family)), kNoGoTo, kComplaintType);
                --pick;
            }
            counts[family] = 0; // every family over the threshold is reset, the two excluded ones as well
        }
    }
    manager.lastReviewHour = env.GameHour();
}

template<class Env> void Update(Manager& manager, Env& env)
{
    if (env.CanShowMessage() && !manager.pending.empty()) {
        const int mode = manager.pending.size() > kLongQueue ? kShortBalloon : kLongBalloon;
        Notice front = std::move(manager.pending.front());
        manager.pending.pop_front();
        if (front.text.wide)
            env.ShowMessage(front.text.wideText.c_str(), mode);
        else
            env.ShowMessageKey(front.text.key.c_str(), mode);
        AppendHistory(manager, env, std::move(front.text), front.objectId);
    }
    ReviewComplaints(manager, env);
}

} // namespace obl::feedback
