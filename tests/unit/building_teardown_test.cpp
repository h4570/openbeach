#include "core/char16.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/objects/building_teardown.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using object::Building;
using object::ThoughtEntry;

namespace {

// A building with `count` thoughts (heap entries, the odd ones with a text) and `animSprites` animation sprites in its definition.
struct Fixture {
    Building object{};
    tables::BuildingDef definition{};

    explicit Fixture(size_t count, uint32_t animSprites = 0)
    {
        definition.m_NumAnimSprites = static_cast<int>(animSprites);
        object.m_definition = &definition;
        for (size_t i = 0; i < count; ++i) {
            auto* entry = static_cast<ThoughtEntry*>(::operator new(sizeof(ThoughtEntry)));
            entry->m_objectId = static_cast<int32_t>(i);
            entry->m_text = i % 2 ? static_cast<Char16*>(::operator new(4 * sizeof(Char16))) : nullptr;
            object.m_ThoughtList.push_back(entry);
        }
    }
};

// The order of the Env's calls is what the tests pin down.
struct Env {
    std::vector<std::string> calls;

    static std::string Hex(const void* pointer)
    {
        char text[24];
        std::snprintf(text, sizeof(text), "%p", pointer);
        return text;
    }
    void ReleaseSprite(void* sprite) { calls.push_back("sprite " + Hex(sprite)); }
    void DeleteQueue(void* queue) { calls.push_back("queue " + Hex(queue)); }
    void DeleteEvents(object::BuildingEvents* events) { calls.push_back("events " + Hex(events)); }
    void StopSound(uint32_t id) { calls.push_back("sound " + std::to_string(id)); }
};

std::string P(const void* pointer)
{
    return Env::Hex(pointer);
}

} // namespace

OBL_TEST_CASE("building_teardown.bare_building")
{
    Fixture f(0, 2);
    f.object.m_id = 77;
    f.object.m_soundPlaying = 1;
    f.object.m_animSprites[0] = reinterpret_cast<ui::SpriteInstance*>(0x100);
    f.object.m_animSprites[1] = reinterpret_cast<ui::SpriteInstance*>(0x200);
    f.object.m_animSprites[2] = reinterpret_cast<ui::SpriteInstance*>(0x300); // beyond the definition: untouched
    f.object.m_shortageSprite = reinterpret_cast<ui::SpriteInstance*>(0x400);
    f.object.m_spannerSprite = reinterpret_cast<ui::SpriteInstance*>(0x500);
    Env env;
    building_teardown::Destruct(f.object, env);
    const std::vector<std::string> expected = {"sprite " + P(reinterpret_cast<void*>(0x100)), "sprite " + P(reinterpret_cast<void*>(0x200)),
            "sprite " + P(reinterpret_cast<void*>(0x400)), "sprite " + P(reinterpret_cast<void*>(0x500)), "sound 77"};
    OBL_CHECK_EQ(env.calls.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < env.calls.size(); ++i) OBL_CHECK_EQ(env.calls[i], expected[i]);
    OBL_CHECK_EQ(f.object.m_soundPlaying, 0);
}

OBL_TEST_CASE("building_teardown.members_in_order")
{
    Fixture f(0);
    f.object.m_queue = reinterpret_cast<void*>(0x1110);
    f.object.m_events = reinterpret_cast<object::BuildingEvents*>(0x2220);
    f.object.m_hasBall = 1;
    f.object.m_ballSprite = reinterpret_cast<ui::SpriteInstance*>(0x3330);
    f.object.m_shortageSprite = reinterpret_cast<ui::SpriteInstance*>(0x4440);
    f.object.m_spannerSprite = reinterpret_cast<ui::SpriteInstance*>(0x5550);
    f.object.m_constructionSprite = reinterpret_cast<void*>(0x6660);
    Env env;
    building_teardown::Destruct(f.object, env);
    const std::vector<std::string> expected = {"queue " + P(reinterpret_cast<void*>(0x1110)),
            "events " + P(reinterpret_cast<void*>(0x2220)), "sprite " + P(reinterpret_cast<void*>(0x3330)),
            "sprite " + P(reinterpret_cast<void*>(0x4440)), "sprite " + P(reinterpret_cast<void*>(0x5550)), "sound 0",
            "sprite " + P(reinterpret_cast<void*>(0x6660))};
    OBL_CHECK_EQ(env.calls.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < env.calls.size(); ++i) OBL_CHECK_EQ(env.calls[i], expected[i]);
    OBL_CHECK(f.object.m_constructionSprite == nullptr);
    // The pointers of the queue and of the events stay (the memory is gone with the object).
    OBL_CHECK(f.object.m_queue != nullptr);
    OBL_CHECK(f.object.m_events != nullptr);
}

OBL_TEST_CASE("building_teardown.thoughts_are_deleted")
{
    // Seventy entries span several deque blocks; all of them (and their texts) go back to the heap and the list is empty.
    Fixture f(70);
    Env env;
    building_teardown::Destruct(f.object, env);
    OBL_CHECK(f.object.m_ThoughtList.empty());
}
