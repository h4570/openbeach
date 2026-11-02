#pragma once

#include "game/objects/guest_script_functions.h"
#include "game/objects/guest_visit_opinions.h"

#include <cstdint>
#include <string>

// The thought functions of a guest that the script calls by number (AddIllPurchaseThought, AddCommodBeachClosed, AddThoughtIDidnt,
// AddThoughtIDidIt, NoRouteToLodging, AddBuildingFullThought, AddDrowningThought; guest thoughts, docs/architecture/objects.md) and the two
// like them: DemolishedAsleep (a thought, the bad-visit sound and penalties) and GenerateBlockedMessage (a ticker line, no thought).
// Each looks up `<prefix><digit>` in the text table (digit = rand() % 3 + 1), pushes the id onto the guest's three thoughts and then applies a small
// penalty; what they do to other objects (the building's thought list, the ticker, a sound) stays with the game side (src/game/objects/guest_thoughts.cpp).
namespace obl::guest_thoughts {

using object::Guest;
using enum tables::GuestOpinion;

// The prefixes as the text keys spell them. Some end in a separator before the digit,
// some do not (the text table has the keys the same way).
inline constexpr const char* kIllPurchase = "GUEST_THOUGHT_NICE_";
inline constexpr const char* kBeachClosed = "GUEST_THOUGHT_BEACH_CLOSED_";
inline constexpr const char* kDetainedForNothing = "GUEST_THOUGHT_DETAINED_FOR_NOTHING";
inline constexpr const char* kDetained = "GUEST_THOUGHT_DETAINED";
inline constexpr const char* kMaleDrowning = "GUEST_THOUGHT_MALE_DROWNING";
inline constexpr const char* kFemaleDrowning = "GUEST_THOUGHT_FEMALE_DROWNING";
inline constexpr const char* kNoPathToAccommodation = "GUEST_THOUGHT_NO_PATH_ACCOM";
inline constexpr const char* kBuildingFull = "GUEST_THOUGHT_FULL_";
inline constexpr const char* kNoPathTicker = "TICKER_MSG_GUEST_NO_PATH_TO_ACCOM"; // Text lookup key of the feedback line
inline constexpr const char* kDemolishedAsleep = "GUEST_THOUGHT_DEMOLISHED_ASLEEP";
// GenerateBlockedMessage keys: the building's own key follows the first, the other two are complete.
inline constexpr const char* kBlockedBuilding = "TICKER_MSG_NO_PATH_";
inline constexpr const char* kBlockedSea = "TICKER_MSG_NO_PATH_SEA";
inline constexpr const char* kBlockedBeach = "TICKER_MSG_NO_PATH_BEACH";

constexpr float kDetainedPenalty = 0.4f; // taken off every opinion of a guest detained for nothing
constexpr double kNoPathTickerGap = 20.0; // seconds between two "no path to accommodation" lines
constexpr int32_t kNoPathTickerType = 5; // feedback::Feedback::AddText type of that line
constexpr float kDemolishedScorePenalty = 1.4f; // taken off the visit score of a guest whose bed was demolished
constexpr float kDemolishedOpinionPenalty = 0.2f; // taken off every opinion
constexpr int32_t kBlockedTickerType = 5; // feedback::Feedback::AddText type of the "no path" lines
constexpr int32_t kSeaCommodity = 2; // m_desired_commod of an activity that is the sea (the rest are beach activities)

inline const char* DrowningPrefix(const tables::GuestDef& def)
{
    return def.m_Gender != 0 ? kFemaleDrowning : kMaleDrowning;
}

// Looks `prefix` + digit up, pushes the text (a missing key changes nothing) and returns its id, -1 included: the building thoughts are still handed
// the text of id -1.
template<class Env> int32_t ThinkOf(Guest& g, const std::string& prefix, Env& env)
{
    const int32_t text = env.TextId(guest_script::ThoughtKey(prefix.c_str(), env.Rand()).c_str());
    guest_script::PushThought(g, text);
    return text;
}

// The sad face (icon 1) for two seconds, written without looking at the running timer.
inline void ShowSadFace(Guest& g)
{
    g.m_moodIcon->m_frameIndex = 1;
    g.m_moodIconTime = guest_script::kMoodIconSeconds;
}

// Detained for nothing: every opinion loses 0.4 (the float constant widened, the difference stored rounded and held at zero by the unrounded value).
inline void PenaliseDetainedForNothing(Guest& g)
{
    guest_script::LowerOpinions(g, static_cast<double>(kDetainedPenalty));
    ShowSadFace(g);
}

// The beach is closed for the commodity the guest wants: variety loses the guest's effect, with no lower bound (the next opinion update holds it).
inline void PenaliseBeachClosed(Guest& g, const tables::GuestDef& def)
{
    ShowSadFace(g);
    guest_visit::Subtract(g.m_opinion[Variety], def.m_OpinionEffect[Variety][1]);
}

// The line is due only when strictly more than 20 s passed (an unordered pair is not).
inline bool NoPathTickerDue(float now, float last)
{
    return static_cast<double>(now) - static_cast<double>(last) > kNoPathTickerGap;
}

// DemolishedAsleep: the visit is spoiled (score -= 1.4, missed purchase); the bad-visit mood and sound follow in the game, then the thought, then the opinions.
inline void PenaliseDemolishedScore(Guest& g)
{
    guest_visit::Subtract(g.m_curScore, static_cast<double>(kDemolishedScorePenalty));
    g.m_missedPurchase = 1;
}
inline void PenaliseDemolishedOpinions(Guest& g)
{
    guest_script::LowerOpinions(g, static_cast<double>(kDemolishedOpinionPenalty));
}

// GenerateBlockedMessage: the key of the ticker line. A guest on foot (activity 0) names the building it was heading for (`buildingKey`, not read otherwise),
// an activity names the sea (desired commodity 2) or the beach; the digit is `rand() % 3 + 1`.
inline std::string BlockedKey(const Guest& g, const char* buildingKey, int randomValue)
{
    if (g.m_activity == 0) return guest_script::ThoughtKey((std::string(kBlockedBuilding) + buildingKey).c_str(), randomValue);
    return guest_script::ThoughtKey(g.m_desired_commod == kSeaCommodity ? kBlockedSea : kBlockedBeach, randomValue);
}

} // namespace obl::guest_thoughts
