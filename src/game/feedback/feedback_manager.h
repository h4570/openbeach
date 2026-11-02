#pragma once

#include "core/char16.h"
#include <cstdint>

namespace obl::feedback {
struct Manager;
} // namespace obl::feedback

namespace obl::feedback {

class Feedback {
public:
    explicit Feedback(feedback::Manager* manager) : m_manager(manager) {}
    static Feedback OfWorld(); // the current world's manager

    // Construction (with new, not on the game heap).
    static feedback::Manager* Create();
    static void Destroy(feedback::Manager* manager);

    // A notice: shown at once when the balloon is free, queued otherwise. `type` is the colour/category (0x100+ are silenced kinds).
    void AddKey(const char* key, int32_t objectId, uint32_t type); // a text-table key
    void AddText(const Char16* text, int32_t objectId, uint32_t type); // a finished text (copied)
    void AddScenarioMessage(const char* word); // a MESS_TYPE_* word of the scenario script
    void AppendHistoryKey(const char* key, int32_t objectId);
    void AppendHistoryText(const Char16* text, int32_t objectId);
    void RemoveByObject(uint32_t objectId);
    void RemoveByText(const char* key);
    void InvalidateObject(int32_t objectId);
    void Update(); // once per world step: the next waiting notice, the half-hourly complaint review

    // Staff messages already announced, one bit mask per staff class.
    uint32_t StaffMessages(int32_t objectType) const;
    void MarkStaffMessage(uint32_t bits, int32_t objectType);
    void ClearStaffMessages(int32_t objectType);

    // The history dialog's cursor: hour, day and object id through the pointers, the text as the return value (null at the end). The text
    // lives until the history changes.
    const Char16* BeginHistory(float* hour, uint32_t* day, uint32_t* objectId);
    const Char16* NextHistory(float* hour, uint32_t* day, uint32_t* objectId);

    static const char* CommodityMessageKey(uint32_t family);

    feedback::Manager& State() const { return *m_manager; } // read by the snapshot and the test probes

private:
    feedback::Manager* m_manager;
};

// Reads `scripts/feedback_vocabulary.json` of the selected pack (the notice keys, scenario words, missing-staff notices and professions);
// false, with the reason in the log, when the file is missing or refused.
bool LoadFeedbackVocabulary(const Text16& gameDirectory);

// Registers the call counters and the `call-drive` fuzz driver of the facade (tests/feedback_manager.txt).
bool RegisterFeedbackTestDriver();

} // namespace obl::feedback
