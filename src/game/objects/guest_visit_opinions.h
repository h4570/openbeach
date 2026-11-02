#pragma once

#include "game/objects/guest_update_opinions.h"

namespace obl::guest_visit {

using object::Guest;
using enum tables::GuestOpinion;

inline void Subtract(float& value, double effect)
{
    value = static_cast<float>(static_cast<double>(value) - effect);
}
inline void Clamp(float& value)
{
    if (guest_opinions::Below(value, 0.0))
        value = 0.0f;
    else if (value > 1.0f)
        value = 1.0f;
}
inline double PriceRatio(float price, float reference)
{
    return reference > 0.0f ? static_cast<double>(price) / reference : static_cast<double>(price) + 1.0;
}
inline double SafetyQuality(uint32_t repair)
{
    return static_cast<double>(static_cast<int32_t>(3u - repair)) * static_cast<double>(0.33f);
}

inline void QueuePenalty(Guest& guest, const tables::GuestDef& def)
{
    Subtract(guest.m_curScore, def.m_OpinionEffect[Crowding][1]);
    Subtract(guest.m_opinion[Crowding], def.m_OpinionEffect[Crowding][1]);
    Subtract(guest.m_curScore, def.m_OpinionEffect[Variety][1]);
    guest_opinions::Lower(guest.m_opinion[Variety], def.m_OpinionEffect[Variety][1]);
    // Variety is cleared too when crowding is below zero, before the state change is delivered.
    if (guest_opinions::Below(guest.m_opinion[Crowding], 0.0)) guest.m_opinion[Variety] = 0.0f;
}

inline void FinishPurchase(Guest& guest)
{
    if (!guest.m_missedPurchase) {
        // Crowding is spilled to float before the upper comparison; safety stays in double precision.
        guest.m_opinion[Crowding] = static_cast<float>(static_cast<double>(guest.m_queueScore) + guest.m_opinion[Crowding]);
        if (guest.m_opinion[Crowding] > 1.0f) guest.m_opinion[Crowding] = 1.0f;
        guest_opinions::Raise(guest.m_opinion[Safety], guest.m_safetyScore);
    } else {
        guest.m_missedPurchase = 0;
    }
    guest.m_priceScore = guest.m_queueScore = guest.m_safetyScore = guest.m_cleanScore = 0.0f;
    if (!guest.m_activity) {
        if (guest.m_curScore > guest.m_bestScore) {
            guest.m_BestSpotOpinion = 0;
            guest.m_Best = guest.m_CurID;
            guest.m_BestType = guest.m_CurTypeID;
            guest.m_bestScore = guest.m_curScore;
        } else if (guest_opinions::Below(guest.m_curScore, guest.m_worstScore)) {
            guest.m_WorstSpotOpinion = 0;
            guest.m_Worst = guest.m_CurID;
            guest.m_WorstType = guest.m_CurTypeID;
            guest.m_worstScore = guest.m_curScore;
        }
    }
    guest.m_curScore = 0.0f;
}

inline void SpotScores(Guest& guest, const tables::GuestDef& def, uint32_t kind)
{
    const double clean = def.m_OpinionEffect[Cleanliness][1], beauty = def.m_OpinionEffect[Beauty][1];
    const double safety = def.m_OpinionEffect[Safety][1], crowd = def.m_OpinionEffect[Crowding][1];
    const double totty = def.m_OpinionEffect[Totty][0];
    double candidate = 0, stored = 0;
    switch (kind) {
    case 0:
    case 1:
        Subtract(guest.m_opinion[Cleanliness], clean);
        candidate = -clean;
        stored = kind == 1 ? clean : -clean; // reference vomit record has the opposite sign
        break;
    case 2:
        Subtract(guest.m_opinion[Cleanliness], clean);
        Subtract(guest.m_opinion[Beauty], beauty);
        Subtract(guest.m_opinion[Safety], safety);
        candidate = stored = (-clean - beauty) - safety;
        break;
    case 3:
        Subtract(guest.m_opinion[Safety], safety);
        candidate = stored = -safety;
        break;
    case 4:
    case 5:
        guest.m_opinion[Totty] = static_cast<float>(totty + guest.m_opinion[Totty]);
        if (totty > guest.m_bestScore) {
            guest.m_BestSpotOpinion = 1;
            guest.m_Best = static_cast<int32_t>(kind);
            guest.m_bestScore = static_cast<float>(totty);
        }
        return;
    case 6:
    case 7:
        Subtract(guest.m_opinion[Crowding], crowd);
        candidate = stored = -crowd;
        break;
    case 8:
    case 9:
    case 10:
        Subtract(guest.m_opinion[Safety], safety + safety);
        candidate = -(safety + safety);
        stored = -(crowd + crowd); // reference compares safety, but stores crowding
        break;
    default: return;
    }
    if (guest_opinions::Below(candidate, guest.m_worstScore)) {
        guest.m_WorstSpotOpinion = 1;
        guest.m_Worst = static_cast<int32_t>(kind);
        guest.m_worstScore = static_cast<float>(stored);
    }
}

} // namespace obl::guest_visit
