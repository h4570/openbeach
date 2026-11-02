#include "core/char16.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/feedback/feedback_manager.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/feedback/feedback_lifecycle.h"
#include "game/feedback/feedback_logic.h"
#include "game/feedback/feedback_vocabulary.h"
#include "game/objects/staff_classes.h"
#include "core/pack_path.h"
#include "core/wide_path.h"
#include "game/ui/text_table.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace obl::feedback {
namespace {

using commodity::Providers;
using ui::GuiManager;
using world_data::World;

// Texts cross into the game as Char16 (UTF-16).
static_assert(sizeof(Char16) == sizeof(char16_t));
static_assert(feedback::kFamilies == commodity::kFamilies);

ui::GuiManager* Gui()
{
    return game::GetGuiManager();
}

// The game side of the Env contract (src/game/feedback/feedback_logic.h): the world clock, the GUI balloon, the game's RNG and the complaint counters.
class GameEnv {
public:
    int GameDay() { return game::GetWorld()->m_gameDay; }
    float GameHour() { return game::GetWorld()->m_gameDayTime; }
    bool CanShowMessage() { return Gui()->CanShowMessage(); }
    void ShowMessage(const char16_t* text, int mode) { Gui()->ShowMessage(text, mode); }
    void ShowMessageKey(const char* key, int mode) { Gui()->ShowMessageKey(key, mode); }
    int Rand() { return game::Rand(); }
    uint32_t* FamilyCounts() { return game::GetCommodityProviders()->complaints.data(); }
};

enum Method {
    MethodAdd,
    MethodAddWide,
    MethodAppendHistoryKey,
    MethodAppendHistoryText,
    MethodUpdate,
    MethodInvalidateObject,
    MethodRemoveByObject,
    MethodRemoveByText,
    MethodAddScenarioMessage,
    MethodMarkStaffMessage,
    MethodClearStaffMessages,
    MethodGetStaffMessages,
    MethodGetCommodityMessageKey,
    MethodBeginHistory,
    MethodNextHistory,
    MethodCount
};

const char* const kCounterNames[MethodCount] = {"feedback.manager-add-key", "feedback.manager-add-text",
        "feedback.manager-append-history-key", "feedback.manager-append-history-text", "feedback.manager-update",
        "feedback.manager-invalidate-object", "feedback.manager-remove-by-object", "feedback.manager-remove-by-text",
        "feedback.manager-add-scenario-message", "feedback.manager-mark-staff-message", "feedback.manager-clear-staff-messages",
        "feedback.manager-staff-messages", "feedback.manager-commodity-message-key", "feedback.manager-begin-history",
        "feedback.manager-next-history"};

// Call counters (`call-report <name>`).
test::CallCounter g_calls[MethodCount] = {test::CallCounter(kCounterNames[0]), test::CallCounter(kCounterNames[1]),
        test::CallCounter(kCounterNames[2]), test::CallCounter(kCounterNames[3]), test::CallCounter(kCounterNames[4]),
        test::CallCounter(kCounterNames[5]), test::CallCounter(kCounterNames[6]), test::CallCounter(kCounterNames[7]),
        test::CallCounter(kCounterNames[8]), test::CallCounter(kCounterNames[9]), test::CallCounter(kCounterNames[10]),
        test::CallCounter(kCounterNames[11]), test::CallCounter(kCounterNames[12]), test::CallCounter(kCounterNames[13]),
        test::CallCounter(kCounterNames[14])};

// One shape for every method that changes the manager.
template<class Logic> void Run(Method method, feedback::Manager* manager, Logic&& logic)
{
    g_calls[method].Count();
    GameEnv env;
    logic(*manager, env);
}

// ---- history readers -----------------------------------------------------------------------------------------------------------------

const Char16* LookupText(const char* key)
{
    return ui::text_table::Lookup(key);
}

const Char16* ReadHistoryEntry(const feedback::HistoryEntry* entry, float* hour, uint32_t* day, uint32_t* objectId)
{
    *hour = 0.0f;
    *day = 0;
    if (!entry) return nullptr;
    *hour = entry->hour;
    *day = static_cast<uint32_t>(entry->day);
    *objectId = static_cast<uint32_t>(entry->objectId);
    return entry->text.wide ? entry->text.wideText.c_str() : LookupText(entry->text.key.c_str());
}

// ---- call-drive --------------------------------------------------------------------------------------------------------------------
// A random but valid sequence of calls of every method against the living manager: notices with and without a key, shown or queued (the
// balloon is freed at random, as the GUI's own timer would), a clock that moves on (day wraps, a review clock of 25), complaint counters
// that cross the threshold, silenced types that come and go, staff masks, the history cursor walked over exactly the entries there are.
// The balloons it requests stay on screen, so a script ends right after it.

struct DriveRandom {
    uint32_t seed = 0x2545f491;
    uint32_t Next() { return (seed = seed * 1664525u + 1013904223u) >> 8; }
    float Unit() { return static_cast<float>(Next() & 0xffff) / 65535.0f; }
};

void DriveFuzz(int count)
{
    World* world = game::GetWorld();
    GuiManager* gui = Gui();
    Providers* providers = game::GetCommodityProviders();
    if (!world || !world->m_feedbackManager || !gui || !providers) {
        OBL_LOG("autotest: FAILED call-drive feedback.manager-add-key: no world");
        return;
    }
    feedback::Manager* manager = world->m_feedbackManager;
    Feedback feedback(manager);
    // The notice keys and scenario words are the pack's (feedback vocabulary); the words a pack lacks and the case-changed one probe the misses.
    const feedback::Vocabulary& vocabulary = feedback::CurrentVocabulary();
    std::vector<const char*> keys, words = {"mess_type_guest", "MESS_TYPE_NOTHING"};
    for (const std::string& commodityKey : vocabulary.commodityKeys) keys.push_back(commodityKey.c_str());
    for (const feedback::MissingStaff& missing : vocabulary.missingStaff) keys.push_back(missing.key.c_str());
    for (const feedback::MessageType& type : vocabulary.messageTypes) words.push_back(type.word.c_str());
    if (keys.empty()) {
        OBL_LOG("autotest: FAILED call-drive feedback.manager-add-key: no feedback vocabulary");
        return;
    }
    static const Char16* const kTexts[] = {u"Notice one", u"Notice two", u"Notice three", u"Notice four"};
    // Every personnel class, the shark (a staff type without a mask) before the guests.
    std::vector<int> staffTypes(object::kHumanClasses.begin(), object::kHumanClasses.end() - 1);
    staffTypes.push_back(static_cast<int>(object::ObjectType::Shark));
    staffTypes.push_back(static_cast<int>(object::ObjectType::Guest));
    DriveRandom random;
    auto key = [&]() { return keys[random.Next() % keys.size()]; };
    auto text = [&]() { return kTexts[random.Next() % (sizeof(kTexts) / sizeof(kTexts[0]))]; };
    auto objectId = [&]() { return random.Next() % 4 == 0 ? -2 : static_cast<int>(random.Next() % 12); };
    auto type = [&]() { return static_cast<uint32_t>(random.Next() % 5 == 0 ? 0x100 + random.Next() % 12 : 3 + random.Next() % 11); };

    for (int i = 0; i < count; ++i) {
        if (random.Next() % 3 != 0) gui->m_balloonActive = 0; // the balloon timed out
        switch (random.Next() % 17) {
        case 0:
        case 1:
        case 2: feedback.AddKey(key(), objectId(), type()); break;
        case 3:
        case 4: feedback.AddText(text(), objectId(), type()); break;
        case 5:
        case 6:
        case 7: {
            float hour = world->m_gameDayTime + random.Unit() * 0.9f;
            if (hour >= 24.0f) {
                hour -= 24.0f;
                ++world->m_gameDay;
            }
            world->m_gameDayTime = hour;
            if (random.Next() % 15 == 0) manager->lastReviewHour = 25.0f;
            if (random.Next() % 2 == 0)
                for (int f = 0; f < feedback::kFamilies; ++f)
                    if (random.Next() % 4 == 0) providers->complaints[f] = random.Next() % 10;
            feedback.Update();
            break;
        }
        case 8: feedback.InvalidateObject(static_cast<int>(random.Next() % 12)); break;
        case 9: feedback.RemoveByObject(random.Next() % 5 == 0 ? static_cast<uint32_t>(-2) : random.Next() % 12); break;
        case 10: feedback.RemoveByText(random.Next() % 5 == 0 ? "FEEDBACK_NOT_THERE" : key()); break;
        case 11:
            if (manager->suppressedCount >= 3) { // let the silenced types go again, or nothing would reach the queues
                manager->suppressedCount = 0;
                manager->suppressWorld = manager->suppressObject = 0;
            }
            feedback.AddScenarioMessage(words[random.Next() % words.size()]);
            break;
        case 12: {
            int staff = staffTypes[random.Next() % staffTypes.size()];
            switch (random.Next() % 3) {
            case 0: feedback.MarkStaffMessage(1u << (random.Next() % 12), staff); break;
            case 1: feedback.ClearStaffMessages(staff); break;
            default: feedback.StaffMessages(staff); break;
            }
            break;
        }
        case 13: Feedback::CommodityMessageKey(random.Next() % 28); break;
        case 14: {
            float hour;
            uint32_t day, id = 0;
            feedback.BeginHistory(&hour, &day, &id);
            for (size_t n = manager->history.size(); n > 1; --n) // the entries after the first
                feedback.NextHistory(&hour, &day, &id);
            break;
        }
        case 15: feedback.AppendHistoryKey(key(), objectId()); break;
        default: feedback.AppendHistoryText(text(), objectId()); break;
        }
    }
}

test::CallCounter g_constructCalls("feedback.manager-construct"), g_destructCalls("feedback.manager-destruct");

} // namespace

Feedback Feedback::OfWorld()
{
    return Feedback(game::GetWorld()->m_feedbackManager);
}

feedback::Manager* Feedback::Create()
{
    g_constructCalls.Count();
    return new feedback::Manager();
}

void Feedback::Destroy(feedback::Manager* manager)
{
    g_destructCalls.Count();
    delete manager;
}

void Feedback::AddKey(const char* key, int32_t objectId, uint32_t type)
{
    Run(MethodAdd, m_manager, [&](auto& m, auto& env) { feedback::Add(m, env, key, objectId, type); });
}

void Feedback::AddText(const Char16* text, int32_t objectId, uint32_t type)
{
    Run(MethodAddWide, m_manager, [&](auto& m, auto& env) { feedback::AddWide(m, env, text, objectId, type); });
}

void Feedback::AppendHistoryKey(const char* key, int32_t objectId)
{
    Run(MethodAppendHistoryKey, m_manager, [&](auto& m, auto& env) { feedback::AppendHistoryKey(m, env, key, objectId); });
}

void Feedback::AppendHistoryText(const Char16* text, int32_t objectId)
{
    Run(MethodAppendHistoryText, m_manager, [&](auto& m, auto& env) { feedback::AppendHistoryText(m, env, text, objectId); });
}

void Feedback::Update()
{
    Run(MethodUpdate, m_manager, [&](auto& m, auto& env) { feedback::Update(m, env); });
}

void Feedback::InvalidateObject(int32_t objectId)
{
    Run(MethodInvalidateObject, m_manager, [&](auto& m, auto&) { feedback::InvalidateObject(m, objectId); });
}

void Feedback::RemoveByObject(uint32_t objectId)
{
    Run(MethodRemoveByObject, m_manager, [&](auto& m, auto&) { feedback::RemoveByObject(m, objectId); });
}

void Feedback::RemoveByText(const char* key)
{
    Run(MethodRemoveByText, m_manager, [&](auto& m, auto&) { feedback::RemoveByText(m, key); });
}

void Feedback::AddScenarioMessage(const char* word)
{
    Run(MethodAddScenarioMessage, m_manager, [&](auto& m, auto&) { feedback::AddScenarioMessage(m, word); });
}

void Feedback::MarkStaffMessage(uint32_t bits, int32_t objectType)
{
    Run(MethodMarkStaffMessage, m_manager, [&](auto& m, auto&) { feedback::MarkStaffMessage(m, bits, objectType); });
}

void Feedback::ClearStaffMessages(int32_t objectType)
{
    Run(MethodClearStaffMessages, m_manager, [&](auto& m, auto&) { feedback::ClearStaffMessages(m, objectType); });
}

uint32_t Feedback::StaffMessages(int32_t objectType) const
{
    g_calls[MethodGetStaffMessages].Count();
    return feedback::GetStaffMessages(*m_manager, objectType);
}

const char* Feedback::CommodityMessageKey(uint32_t family)
{
    g_calls[MethodGetCommodityMessageKey].Count();
    return feedback::CommodityMessageKey(family);
}

const Char16* Feedback::BeginHistory(float* hour, uint32_t* day, uint32_t* objectId)
{
    g_calls[MethodBeginHistory].Count();
    return ReadHistoryEntry(feedback::BeginHistory(*m_manager), hour, day, objectId);
}

const Char16* Feedback::NextHistory(float* hour, uint32_t* day, uint32_t* objectId)
{
    g_calls[MethodNextHistory].Count();
    return ReadHistoryEntry(feedback::NextHistory(*m_manager), hour, day, objectId);
}

bool LoadFeedbackVocabulary(const Text16& gameDirectory)
{
    std::ifstream file(
            pack_path::Resolve(WidePath(gameDirectory) / pack_path::Selected(), "scripts/feedback_vocabulary.json"), std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    feedback::Vocabulary vocabulary;
    std::string error;
    const bool ok = file && feedback::ParseVocabulary(json, vocabulary, error);
    OBL_LOG("feedback vocabulary %s (%s)", ok ? "loaded" : "unavailable", ok ? "" : (file ? error.c_str() : "cannot open"));
    if (ok) feedback::SetVocabulary(std::move(vocabulary));
    return ok;
}

bool RegisterFeedbackTestDriver()
{
    g_calls[MethodAdd].SetDriver(&DriveFuzz);
    return true;
}

} // namespace obl::feedback
