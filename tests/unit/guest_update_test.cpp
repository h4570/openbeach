#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/guest_update.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using enum obl::messages::MessageType;
using obl::object::Guest;
using obl::script::ScriptProgram;

namespace {

// The game as a script of answers; every call is written to `log` in order.
struct UpdateEnv {
    std::string log;
    float delta = 0.02f;
    float now = 500.0f;
    int32_t dayLength = 100;
    bool human = true;
    int rand = 16383;
    Message posted{}, delayedMessage{};
    float delay = 0.0f;
    int32_t scriptState = -1;
    const void* released = nullptr;
    const char* fatalName = nullptr;

    float GameDeltaTime() { return delta; }
    float GameTime() { return now; }
    int32_t DayLengthSeconds()
    {
        add("Day");
        return dayLength;
    }
    bool HumanUpdate()
    {
        add("Human");
        return human;
    }
    void Fatal(const char* name)
    {
        add("Fatal");
        fatalName = name;
    }
    void ReleaseSprite(void* sprite)
    {
        add("Release");
        released = sprite;
    }
    void Post(const Message& message)
    {
        add("Post");
        posted = message;
    }
    void PostDelayed(const Message& message, float seconds)
    {
        add("PostDelayed");
        delayedMessage = message;
        delay = seconds;
    }
    int Rand()
    {
        add("Rand");
        return rand;
    }
    void RunOnUpdate(int32_t state)
    {
        add("Script");
        scriptState = state;
    }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

std::unique_ptr<object::Guest> MakeGuest()
{
    auto guest = std::make_unique<object::Guest>();
    obl::unit::FillObject<object::Guest>(*guest);
    guest->m_id = 77;
    guest->m_state = 1;
    guest->m_moving = 1;
    std::strcpy(guest->m_Name, "Gail");
    return guest;
}

} // namespace

OBL_TEST_CASE("guest_update.quiet_frame_calls_the_base_update_then_the_script_with_the_state")
{
    auto guest = MakeGuest();
    guest->m_state = 9;
    UpdateEnv env;
    OBL_CHECK(guest_update::UpdateGuest(*guest, env));
    OBL_CHECK_EQ(env.log, std::string("Day Human Script")); // the day length is looked up unless a lounger with no tan to lose
    OBL_CHECK_EQ(env.scriptState, 9);
}

OBL_TEST_CASE("guest_update.a_lounger_without_a_tan_to_recover_skips_the_day_length_lookup")
{
    auto guest = MakeGuest();
    guest->m_state = 29;
    UpdateEnv env;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.log, std::string("Human Script"));
    guest->m_sunBurnLevel = -0.5f; // a tanning lounger still recovers
    env.log.clear();
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.log, std::string("Day Human Script"));
}

OBL_TEST_CASE("guest_update.sun_burn_climbs_by_twice_the_frame_time_per_day_second_and_stops_at_zero")
{
    auto guest = MakeGuest();
    guest->m_sunBurnLevel = -1.0f;
    UpdateEnv env;
    env.delta = 0.5f;
    env.dayLength = 10;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_sunBurnLevel, static_cast<float>(0.5 * 2.0 / 10.0 + -1.0)); // -0.9
    guest->m_sunBurnLevel = -0.05f;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_sunBurnLevel, 0.0f); // the sum 0.05 is above 0
    guest->m_sunBurnLevel = -0.1f;
    guest_update::UpdateGuest(*guest, env);
    // the double sum 0.1 - 0.1f is a hair below 0: not above 0, so the rounded sum stays
    OBL_CHECK_EQ(guest->m_sunBurnLevel, static_cast<float>(0.1 + static_cast<double>(-0.1f)));
}

OBL_TEST_CASE("guest_update.a_nan_sun_burn_level_counts_as_below_zero_like_the_fpu_flags")
{
    auto guest = MakeGuest();
    guest->m_state = 29; // a lounger: only a level below 0 (or NaN) is recovered
    guest->m_sunBurnLevel = std::numeric_limits<float>::quiet_NaN();
    UpdateEnv env;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.log, std::string("Day Human Script"));
    OBL_CHECK(std::isnan(guest->m_sunBurnLevel)); // NaN + anything: the sum is not above 0, the NaN is stored
}

OBL_TEST_CASE("guest_update.sun_burn_divides_by_zero_day_length_like_the_fpu")
{
    auto guest = MakeGuest();
    guest->m_sunBurnLevel = -1.0f;
    UpdateEnv env;
    env.dayLength = 0;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_sunBurnLevel, 0.0f); // +inf is above 0
}

OBL_TEST_CASE("guest_update.idle_clock_follows_the_game_time_while_moving_and_after_a_long_stand")
{
    auto guest = MakeGuest();
    UpdateEnv env;
    guest->m_lastUpdateHappened = 1.0f;
    guest_update::UpdateGuest(*guest, env); // moving
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 500.0f);
    guest->m_moving = 0;
    guest->m_lastUpdateHappened = 450.0f; // 50 s: not long enough
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 450.0f);
    guest->m_lastUpdateHappened = 400.0f; // exactly 100 s: not above
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 400.0f);
    guest->m_lastUpdateHappened = 399.0f; // 101 s in a state that is not the chat-up
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 500.0f);
}

OBL_TEST_CASE("guest_update.chatted_up_for_too_long_drops_the_sprite_and_goes_back_to_searching")
{
    auto guest = MakeGuest();
    int sprite = 0;
    guest->m_state = 35;
    guest->m_moving = 0;
    guest->m_lastUpdateHappened = 300.0f;
    guest->m_attachedSprite = &sprite;
    UpdateEnv env;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.log, std::string("Day Release Post Human Script"));
    OBL_CHECK_EQ(env.released, static_cast<const void*>(&sprite));
    OBL_CHECK(guest->m_attachedSprite == nullptr);
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 300.0f); // not refreshed
    OBL_CHECK_EQ(env.posted.type, MT_STATECHANGE);
    OBL_CHECK_EQ(env.posted.sender, 77);
    OBL_CHECK_EQ(env.posted.receiver, 77);
    OBL_CHECK_EQ(env.posted.data, 12);
    OBL_CHECK_EQ(env.posted.flags, 0);
    // without a sprite there is nothing to release
    env.log.clear();
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.log, std::string("Day Post Human Script"));
}

OBL_TEST_CASE("guest_update.mood_icon_fades_and_floors_on_the_double_difference")
{
    auto guest = MakeGuest();
    UpdateEnv env;
    env.delta = 0.5f;
    guest->m_moodIconTime = 2.0f;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_moodIconTime, 1.5f);
    guest->m_moodIconTime = 0.25f;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_moodIconTime, 0.0f);
    guest->m_moodIconTime = 0.5f; // exactly spent: stays 0 without the floor
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_moodIconTime, 0.0f);
    guest->m_moodIconTime = -3.0f; // negative: untouched
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_moodIconTime, -3.0f);
}

OBL_TEST_CASE("guest_update.a_nan_frame_time_floors_the_mood_icon_like_the_fpu_flags")
{
    auto guest = MakeGuest();
    guest->m_moodIconTime = 1.0f;
    UpdateEnv env;
    env.delta = std::numeric_limits<float>::quiet_NaN();
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_moodIconTime, 0.0f);
}

OBL_TEST_CASE("guest_update.a_failing_base_update_is_reported_with_the_name_and_the_frame_goes_on")
{
    auto guest = MakeGuest();
    UpdateEnv env;
    env.human = false;
    OBL_CHECK(guest_update::UpdateGuest(*guest, env));
    OBL_CHECK_EQ(env.log, std::string("Day Human Fatal Script"));
    OBL_CHECK_EQ(std::string(env.fatalName), std::string("Gail"));
}

OBL_TEST_CASE("guest_update.a_drunk_guest_heading_for_a_desire_is_sick_after_a_random_delay")
{
    auto guest = MakeGuest();
    guest->m_state = 13;
    guest->m_drunk = 0.9f;
    UpdateEnv env;
    env.rand = 32767;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.log, std::string("Day Human Rand PostDelayed Script"));
    OBL_CHECK_EQ(guest->m_drunk, 0.7f);
    OBL_CHECK_EQ(env.delayedMessage.type, MT_THROWUP);
    OBL_CHECK_EQ(env.delayedMessage.sender, 77);
    OBL_CHECK_EQ(env.delayedMessage.receiver, 77);
    // 32767 * (1/32767 as a float, a little above) * 10 + 10, every step in double
    OBL_CHECK_EQ(env.delay, static_cast<float>(32767.0 * static_cast<double>(std::bit_cast<float>(0x38000100u)) * 10.0 + 10.0));
    env.rand = 0;
    guest->m_drunk = 0.9f;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.delay, 10.0f);
    // exactly the threshold, another state: nothing
    env.log.clear();
    guest->m_drunk = 0.75f;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(env.log, std::string("Day Human Script"));
    guest->m_state = 12;
    guest->m_drunk = 0.99f;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_drunk, 0.99f);
}

OBL_TEST_CASE("guest_update.the_littered_flag_and_the_offence_expire_after_ten_and_twenty_seconds")
{
    auto guest = MakeGuest();
    UpdateEnv env;
    guest->m_justLittered = 1;
    guest->m_timeLittered = 490.0f; // exactly ten seconds ago: not above
    guest->m_OffenceState = 2;
    guest->m_timeOfOffence = 480.0f; // exactly twenty
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_justLittered, 1);
    OBL_CHECK_EQ(guest->m_OffenceState, 2u);
    env.now = 500.5f;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_justLittered, 0);
    OBL_CHECK_EQ(guest->m_OffenceState, 0u);
}

OBL_TEST_CASE("guest_update.the_debug_break_flag_is_cleared")
{
    auto guest = MakeGuest();
    guest->m_debugBreak = 1;
    UpdateEnv env;
    guest_update::UpdateGuest(*guest, env);
    OBL_CHECK_EQ(guest->m_debugBreak, 0);
}

namespace {

struct ReceiveEnv {
    std::string log;
    float now = 321.0f;
    bool receipt = false;
    bool movement = true;
    object::Guest* guest = nullptr;
    Message seen{};
    Message seenArgument{};
    void* seenPointer = nullptr;

    float GameTime() { return now; }
    bool RunOnReceipt(const Message& message)
    {
        add("Receipt(" + std::to_string(message.type) + ")");
        seenPointer = guest->m_currentMessage;
        if (seenPointer) std::memcpy(&seen, seenPointer, sizeof(Message));
        seenArgument = message;
        return receipt;
    }
    bool Movement(const Message& message)
    {
        add("Movement(" + std::to_string(message.type) + ")");
        return movement;
    }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

Message Of(int type)
{
    Message message{};
    message.type = type;
    message.sender = 7;
    message.receiver = 8;
    message.data = 99;
    message.floatData = 1.5f;
    return message;
}

} // namespace

OBL_TEST_CASE("guest_receive.script_first_then_movement_and_the_current_message_is_put_back")
{
    auto guest = MakeGuest();
    int marker = 0;
    guest->m_currentMessage = &marker;
    ReceiveEnv env;
    env.guest = guest.get();
    const Message message = Of(0x2c);
    OBL_CHECK(guest_update::ReceiveMessage(*guest, message, env));
    OBL_CHECK_EQ(env.log, std::string("Receipt(44) Movement(44)"));
    OBL_CHECK(guest->m_currentMessage == &marker);
    OBL_CHECK_EQ(env.seen.sender, 7);
    OBL_CHECK_EQ(env.seen.data, 99);
    OBL_CHECK_EQ(env.seen.floatData, 1.5f);
    OBL_CHECK(env.seenPointer != static_cast<const void*>(&message)); // a copy, not the caller's message
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 321.0f);
}

OBL_TEST_CASE("guest_receive.a_message_the_script_handles_skips_the_movement_code_and_answers_true")
{
    auto guest = MakeGuest();
    ReceiveEnv env;
    env.guest = guest.get();
    env.receipt = true;
    env.movement = false;
    OBL_CHECK(guest_update::ReceiveMessage(*guest, Of(0x2c), env));
    OBL_CHECK_EQ(env.log, std::string("Receipt(44)"));
    env.receipt = false;
    env.log.clear();
    OBL_CHECK(!guest_update::ReceiveMessage(*guest, Of(0x2c), env));
}

OBL_TEST_CASE("guest_receive.background_messages_do_not_refresh_the_activity_clock")
{
    for (int type : {MT_UPDATEOPINIONS, MT_SPOT_OPINION, MT_BUILDING_CLOSING}) {
        auto guest = MakeGuest();
        guest->m_lastUpdateHappened = 5.0f;
        ReceiveEnv env;
        env.guest = guest.get();
        guest_update::ReceiveMessage(*guest, Of(type), env);
        OBL_CHECK_EQ(guest->m_lastUpdateHappened, 5.0f);
    }
    auto guest = MakeGuest();
    guest->m_lastUpdateHappened = 5.0f;
    ReceiveEnv env;
    env.guest = guest.get();
    guest_update::ReceiveMessage(*guest, Of(MT_STATECHANGE), env);
    OBL_CHECK_EQ(guest->m_lastUpdateHappened, 321.0f);
}

OBL_TEST_CASE("guest_receive.a_new_day_resets_the_best_and_worst_spot")
{
    auto guest = MakeGuest();
    guest->m_BestSpotOpinion = 1;
    guest->m_WorstSpotOpinion = 1;
    guest->m_Best = 5;
    guest->m_Worst = 6;
    guest->m_BestType = 9;
    guest->m_bestScore = 0.5f;
    guest->m_worstScore = 0.25f;
    ReceiveEnv env;
    env.guest = guest.get();
    guest_update::ReceiveMessage(*guest, Of(MT_NEW_DAY), env);
    OBL_CHECK_EQ(guest->m_BestSpotOpinion, 0);
    OBL_CHECK_EQ(guest->m_WorstSpotOpinion, 0);
    OBL_CHECK_EQ(guest->m_Best, -2);
    OBL_CHECK_EQ(guest->m_Worst, -2);
    OBL_CHECK_EQ(guest->m_BestType, 9); // untouched
    OBL_CHECK_EQ(guest->m_bestScore, 0.0f);
    OBL_CHECK_EQ(guest->m_worstScore, 0.0f);
}

OBL_TEST_CASE("guest_receive.the_copy_of_the_message_has_a_signalling_nan_quieted_and_the_script_gets_the_signalling_value")
{
    auto guest = MakeGuest();
    ReceiveEnv env;
    env.guest = guest.get();
    Message message = Of(0x2c);
    const uint32_t signalling = 0x7fa00000u;
    std::memcpy(&message.floatData, &signalling, sizeof(signalling));
    guest_update::ReceiveMessage(*guest, message, env);
    uint32_t copy, argument;
    std::memcpy(&copy, &env.seen.floatData, sizeof(copy));
    std::memcpy(&argument, &env.seenArgument.floatData, sizeof(argument));
    OBL_CHECK_EQ(copy, 0x7fe00000u);
    OBL_CHECK_EQ(argument, signalling);
}

namespace {

struct ScriptEnv {
    std::string log;
    void RunOnEntry(int32_t state) { log += "Entry(" + std::to_string(state) + ")"; }
    void RunOnExit(int32_t state) { log += "Exit(" + std::to_string(state) + ")"; }
    void Send(const Message& message) { log += "Send(" + std::to_string(message.type) + ")"; }
    void SendDelayed(const Message& message, float delay)
    {
        log += "Delayed(" + std::to_string(message.type) + "," + std::to_string(delay) + ")";
    }
};

} // namespace

OBL_TEST_CASE("guest_script_wrappers.enter_exit_always_answer_true_and_exit_passes_the_current_state")
{
    auto guest = MakeGuest();
    guest->m_state = 17;
    ScriptEnv env;
    OBL_CHECK(guest_update::EnterState(*guest, 5, env));
    OBL_CHECK(guest_update::ExitState(*guest, env));
    OBL_CHECK_EQ(env.log, std::string("Entry(5)Exit(17)"));
}

OBL_TEST_CASE("guest_script_wrappers.senders_do_nothing_without_a_script")
{
    auto guest = MakeGuest();
    ScriptEnv env;
    guest_update::SendMessage(*guest, Of(3), env);
    guest_update::SendMessageDelayed(*guest, Of(3), 1.5f, env);
    OBL_CHECK_EQ(env.log, std::string());
    static obl::script::ScriptProgram script{};
    guest->m_script = &script;
    guest_update::SendMessage(*guest, Of(3), env);
    guest_update::SendMessageDelayed(*guest, Of(4), 1.5f, env);
    OBL_CHECK_EQ(env.log, std::string("Send(3)Delayed(4,1.500000)"));
}
