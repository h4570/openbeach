#pragma once

#include "game/messages/message_state.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// The calls a guest dispatcher (Update, ReceiveMessage) makes into the game, as a list a tap can intercept
// (guest update dispatch, docs/architecture/objects.md). Nothing activates a tap. While one is active the shared handlers of the human Update,
// the script's OnUpdate and OnReceipt runs and the human ReceiveMovementMessage (src/game/objects/staff_feedback.cpp, shared by
// staff and guests) hand the calls that concern the tapped guest to the tap instead of running them: the base class would move the real
// guest's route, the script would send and change anything.
namespace obl::object {

struct GuestCallTap {
    enum Kind : uint32_t { Human, Script, Receipt, Movement };
    struct Call {
        Kind kind;
        int32_t a; // the state (Script), the message type (Receipt, Movement)
        std::string text; // what the receiver holds at the call (Receipt)
        bool operator==(const Call&) const = default;
    };

    const void* object = nullptr; // the copy the recorded run works on
    bool replay = false; // false: the recorded run, true: the replay
    std::vector<Call> recorded, replayed;
    int32_t answerHuman = 1, answerReceipt = 0, answerMovement = 0;

    int32_t Answer(Kind kind) const
    {
        return kind == Human ? answerHuman : kind == Receipt ? answerReceipt : kind == Movement ? answerMovement : 0;
    }
    // The call of whichever run is on; the answer is the scripted one in both.
    int32_t Record(Kind kind, int32_t a, std::string text = {})
    {
        (replay ? replayed : recorded).push_back({kind, a, std::move(text)});
        return Answer(kind);
    }
    void Clear()
    {
        recorded.clear();
        replayed.clear();
        replay = false;
    }
};

// What the receiver holds at a receipt: the by-value message the script gets (eight words) and the copy in `m_currentMessage` (seven: the eighth is never
// written).
inline std::string ReceiptText(const void* current, const messages::Message& message)
{
    char text[256];
    const auto* words = reinterpret_cast<const uint32_t*>(&message);
    int length = 0;
    for (int i = 0; i < 8; ++i) length += std::snprintf(text + length, sizeof(text) - length, "%x.", words[i]);
    length += std::snprintf(text + length, sizeof(text) - length, " cur=");
    const auto* held = static_cast<const uint32_t*>(current);
    for (int i = 0; held && i < 7; ++i) length += std::snprintf(text + length, sizeof(text) - length, "%x.", held[i]);
    return text;
}

// Null outside a call-drive of a guest dispatcher.
GuestCallTap*& ActiveGuestCallTap();

// The tap, when `object` is the copy a call-drive is running on.
inline GuestCallTap* GuestTapFor(const void* object)
{
    GuestCallTap* tap = ActiveGuestCallTap();
    return tap && !tap->replay && tap->object == object ? tap : nullptr;
}

} // namespace obl::object
