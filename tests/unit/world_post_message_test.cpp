#include "unit_test.h"
#include "game/objects/object_base.h"
#include "game/messages/message_state.h"
#include "game/world/world_state.h"

#include "game/world/world_post_message.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_post;

namespace {

struct FakeEnv {
    // A deque: game objects are not copyable, and an object added during the walk must not move the ones already listed.
    std::map<int, std::deque<object::Base>> classes;
    std::vector<std::string> calls;
    int listReads = 0;
    // Called after each Post: lets a test change the class list while it is being walked.
    void (*afterPost)(FakeEnv&) = nullptr;
    std::vector<object::Base*> pointers;

    void Add(int cls, uint32_t id) { classes[cls].emplace_back().m_id = id; }
    List GetList(int cls)
    {
        ++listReads;
        calls.push_back("list" + std::to_string(cls));
        auto it = classes.find(cls);
        pointers.clear();
        if (it == classes.end()) return {};
        for (auto& object : it->second) pointers.push_back(&object);
        return {pointers.data(), static_cast<uint32_t>(pointers.size())};
    }
    void Post(obl::messages::Message& message)
    {
        calls.push_back("post to " + std::to_string(message.receiver) + " type " + std::to_string(message.type));
        if (afterPost) afterPost(*this);
    }
};

obl::messages::Message Make(int type)
{
    obl::messages::Message message{};
    message.sender = -2;
    message.receiver = -1;
    message.type = type;
    return message;
}

} // namespace

OBL_TEST_CASE("world_post_message.class_posts_once_per_object_in_list_order")
{
    FakeEnv env;
    env.Add(3, 11);
    env.Add(3, 7);
    env.Add(3, 9);
    env.Add(4, 100);
    obl::messages::Message message = Make(38);
    PostToClass(env, 3, message);
    const std::vector<std::string> expected = {"list3", "post to 11 type 38", "post to 7 type 38", "post to 9 type 38"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK_EQ(message.receiver, 9); // the shared record keeps the last receiver
    OBL_CHECK_EQ(message.sender, -2);
}

OBL_TEST_CASE("world_post_message.empty_class_posts_nothing_and_leaves_the_message")
{
    FakeEnv env;
    obl::messages::Message message = Make(5);
    PostToClass(env, 6, message);
    const std::vector<std::string> expected = {"list6"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK_EQ(message.receiver, -1);
}

OBL_TEST_CASE("world_post_message.buildings_are_class_two")
{
    FakeEnv env;
    env.Add(2, 21);
    env.Add(2, 22);
    env.Add(3, 5);
    obl::messages::Message message = Make(45);
    PostToBuildings(env, message);
    const std::vector<std::string> expected = {"list2", "post to 21 type 45", "post to 22 type 45"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_post_message.humans_walk_personnel_six_to_twelve_then_guests")
{
    FakeEnv env;
    for (int cls = 2; cls <= 12; ++cls) env.Add(cls, 100 + cls);
    obl::messages::Message message = Make(7);
    PostToHumans(env, message);
    const std::vector<std::string> expected = {"list6", "post to 106 type 7", "list7", "post to 107 type 7", "list8", "post to 108 type 7",
            "list9", "post to 109 type 7", "list10", "post to 110 type 7", "list11", "post to 111 type 7", "list12", "post to 112 type 7",
            "list3", "post to 103 type 7"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_post_message.list_and_length_are_read_once_per_class")
{
    FakeEnv env;
    env.Add(3, 1);
    env.Add(3, 2);
    env.afterPost = [](FakeEnv& e) {
        if (e.classes[3].size() == 2) e.Add(3, 3);
    };
    obl::messages::Message message = Make(9);
    PostToClass(env, 3, message);
    OBL_CHECK_EQ(env.listReads, 1);
    int posts = 0;
    for (const std::string& call : env.calls) posts += call.rfind("post", 0) == 0;
    OBL_CHECK_EQ(posts, 2);
}
