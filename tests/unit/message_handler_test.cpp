#include "unit_test.h"

#include "game/messages/message_logic.h"
#include "game/messages/message_fill.h"

#include <cstdint>
#include <cstring>

#include <limits>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::messages;

namespace {

Message Msg(int receiver, int type, float time, int data = 0)
{
    Message m{};
    m.sender = 1;
    m.receiver = receiver;
    m.type = type;
    m.data = data;
    m.time = time;
    return m;
}

// The world side of the logic: a clock, a pause flag and a log of deliveries. `onDeliver` lets a test post from inside a delivery.
struct FakeEnv {
    float now = 0;
    bool paused = false;
    std::vector<Message> delivered;
    Handler* handler = nullptr;
    bool postWhileDelivering = false;

    float Now() { return now; }
    bool Paused() { return paused; }
    void Deliver(Message& message)
    {
        delivered.push_back(message);
        if (postWhileDelivering && handler) {
            postWhileDelivering = false;
            Message reply = Msg(99, MT_CHAT, 0);
            PostDelayed(*handler, *this, reply, 0.0f); // already due, but waits for the next Update
        }
    }
};

std::vector<int> Receivers(const MessageList& list)
{
    std::vector<int> out;
    for (const Message& m : list) out.push_back(m.receiver);
    return out;
}

} // namespace

OBL_TEST_CASE("message_handler.post_defers_only_when_paused")
{
    OBL_CHECK(DeliversAtOnce(false, 5));
    OBL_CHECK(DeliversAtOnce(false, kMessageSenderSilent));
    OBL_CHECK(!DeliversAtOnce(true, 5));
    OBL_CHECK(!DeliversAtOnce(true, 0));
    OBL_CHECK(!DeliversAtOnce(true, -2));
    OBL_CHECK(DeliversAtOnce(true, kMessageSenderSilent));
}

OBL_TEST_CASE("message_handler.times_are_float_sums")
{
    OBL_CHECK_EQ(PostedTime(12.5f, false), 12.5f);
    OBL_CHECK_EQ(PostedTime(12.5f, true), 12.5f + 0.0001f);
    OBL_CHECK(PostedTime(12.5f, true) > 12.5f);
    OBL_CHECK_EQ(DelayedTime(3.0f, 2.5f), 5.5f);
    OBL_CHECK_EQ(DelayedTime(1.0e7f, 0.25f), 1.0e7f);
    OBL_CHECK_EQ(kPausedDelay, 0.0001f);
}

OBL_TEST_CASE("message_handler.due_boundary_and_nan")
{
    OBL_CHECK(IsDue(4.0f, 5.0f));
    OBL_CHECK(IsDue(5.0f, 5.0f));
    OBL_CHECK(!IsDue(5.0001f, 5.0f));
    OBL_CHECK(IsDue(std::numeric_limits<float>::quiet_NaN(), 5.0f)); // an unordered compare reports "not greater"
    OBL_CHECK(IsDue(-1.0f, 0.0f));
}

OBL_TEST_CASE("message_handler.post_delivers_at_once_or_queues_when_paused")
{
    Handler h;
    FakeEnv env;
    env.now = 7.0f;
    Message a = Msg(1, MT_STATECHANGE, -1.0f);
    Post(h, env, a);
    OBL_CHECK_EQ(a.time, 7.0f); // the caller's message gets the time
    OBL_CHECK_EQ(env.delivered.size(), size_t(1));
    OBL_CHECK(h.delayed.empty());

    env.paused = true;
    Message b = Msg(2, MT_STATECHANGE, 0);
    Post(h, env, b);
    OBL_CHECK_EQ(env.delivered.size(), size_t(1));
    OBL_CHECK_EQ(h.delayed.size(), size_t(1));
    OBL_CHECK_EQ(h.delayed.front().time, 7.0f + kPausedDelay);

    Message silent = Msg(3, MT_STATECHANGE, 0);
    silent.sender = kMessageSenderSilent;
    Post(h, env, silent);
    OBL_CHECK_EQ(env.delivered.size(), size_t(2));
    OBL_CHECK_EQ(env.delivered.back().receiver, 3);
}

OBL_TEST_CASE("message_handler.update_delivers_due_in_order_and_defers_new_posts")
{
    Handler h;
    FakeEnv env;
    env.handler = &h;
    for (const Message& m : {Msg(1, 10, 9.0f), Msg(2, 11, 3.0f), Msg(3, 12, 5.0f), Msg(4, 13, 5.5f), Msg(5, 14, 1.0f)})
        h.delayed.push_back(m);
    env.now = 5.0f;
    env.postWhileDelivering = true;
    Update(h, env);
    OBL_CHECK_EQ(env.delivered.size(), size_t(3));
    OBL_CHECK_EQ(env.delivered[0].receiver, 2);
    OBL_CHECK_EQ(env.delivered[1].receiver, 3);
    OBL_CHECK_EQ(env.delivered[2].receiver, 5);
    OBL_CHECK(h.working.empty());
    // The reply posted during the delivery is due but waits behind the rest of the delayed list.
    OBL_CHECK(Receivers(h.delayed) == std::vector<int>({1, 4, 99}));
    OBL_CHECK_EQ(h.delayed.back().time, 5.0f);
    Update(h, env);
    OBL_CHECK_EQ(env.delivered.back().receiver, 99);
    OBL_CHECK(Receivers(h.delayed) == std::vector<int>({1, 4}));
}

OBL_TEST_CASE("message_handler.post_delayed_queues_at_now_plus_delay")
{
    Handler h;
    FakeEnv env;
    env.now = 3.0f;
    Message m = Msg(8, MT_LOOK_AROUND, 0);
    PostDelayed(h, env, m, 2.5f);
    OBL_CHECK(env.delivered.empty());
    OBL_CHECK_EQ(h.delayed.size(), size_t(1));
    OBL_CHECK_EQ(h.delayed.front().time, 5.5f);
    OBL_CHECK_EQ(m.time, 5.5f);
}

OBL_TEST_CASE("message_handler.clear_keeps_surviving_types")
{
    for (int type : {MT_UPDATEOPINIONS, MT_LOOK_AROUND, MT_FINISH_ACTION, MT_MOVE2QUEUEPOINT, MT_MOVEIMHERE})
        OBL_CHECK(SurvivesClear(type));
    OBL_CHECK_EQ(MT_UPDATEOPINIONS, 0x3f);
    OBL_CHECK_EQ(MT_LOOK_AROUND, 0x54);
    OBL_CHECK_EQ(MT_FINISH_ACTION, 0x38);
    OBL_CHECK_EQ(MT_MOVE2QUEUEPOINT, 0x1f);
    OBL_CHECK_EQ(MT_MOVEIMHERE, 0x16);
    OBL_CHECK(!SurvivesClear(MT_STATECHANGE));

    Handler h;
    for (const Message& m : {Msg(7, MT_STATECHANGE, 1), Msg(7, MT_LOOK_AROUND, 2), Msg(8, MT_STATECHANGE, 3), Msg(7, MT_CHAT, 4)})
        h.delayed.push_back(m);
    OBL_CHECK_EQ(ClearDelayedForReceiver(h, 7), size_t(2));
    OBL_CHECK(Receivers(h.delayed) == std::vector<int>({7, 8}));
    OBL_CHECK_EQ(h.delayed.front().type, int(MT_LOOK_AROUND));
}

OBL_TEST_CASE("message_handler.init_fields_leave_the_suffix_bytes_untouched")
{
    obl::messages::Message m;
    std::memset(&m, 0xa5, sizeof(m));
    const auto before = m;
    OBL_CHECK(message_fill::Fill(&m, 1, 2, 3, 4, 5) == &m);
    OBL_CHECK_EQ(m.sender, 1);
    OBL_CHECK_EQ(m.receiver, 2);
    OBL_CHECK_EQ(m.type, 3);
    OBL_CHECK_EQ(m.flags, 4);
    OBL_CHECK_EQ(m.data, 5);
    OBL_CHECK(std::memcmp(&m.extra, &before.extra, 12) == 0);
    OBL_CHECK(message_fill::FillWithFloat(&m, -1, -2, -3, -4, -5, 1.25f, 17) == &m);
    OBL_CHECK_EQ(m.extra, 17);
    OBL_CHECK_EQ(m.floatData, 1.25f);
    OBL_CHECK(std::memcmp(&m.time, &before.time, 4) == 0);
}
