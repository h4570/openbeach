#include "unit_test.h"

#include "game/ui/loop_sounds.h"

#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace obl;
using audio::LoopRecord;
using audio::LoopSounds;
using audio::SpriteSoundRecord;

namespace {
// Stands in for the game: malloc heap with live sets (leaks and double frees), scripted Sound results, a logged call list.
struct FakeEnv {
    std::set<void*> records;
    FakeEnv() = default;
    FakeEnv(const FakeEnv&) = delete;
    // Whatever the code under test did not delete by the end of a case (most cases never run the destructor).
    ~FakeEnv()
    {
        for (void* p : records) std::free(p);
    }
    std::vector<std::string> calls;
    loop_sounds::Listener listener{100, 100, 0, 50, 150};
    float music = 0;
    bool stream = false;
    std::map<int, std::pair<float, float>> objects; // id -> position (y already includes the overlay offset)
    int nextChannel = 10;
    bool play3dSucceeds = true;
    std::map<int, int> owners;

    template<class T> T* NewRecord()
    {
        auto* p = static_cast<T*>(std::calloc(1, sizeof(T)));
        records.insert(p);
        return p;
    }
    void DeleteRecord(void* p)
    {
        OBL_CHECK(records.erase(p) == 1);
        std::free(p);
    }
    loop_sounds::Listener Listener() const { return listener; }
    float MusicVolume() const { return music; }
    bool StreamActive() const { return stream; }
    bool ObjectPosition(int id, float& x, float& y) const
    {
        auto it = objects.find(id);
        if (it == objects.end()) return false;
        x = it->second.first;
        y = it->second.second;
        return true;
    }
    int PlayScaled(const LoopRecord& r, float volume)
    {
        calls.push_back("play " + std::string(r.name) + " " + std::to_string(volume));
        return nextChannel++;
    }
    void FadeTo(int channel, float volume) { calls.push_back("to " + std::to_string(channel) + " " + std::to_string(volume)); }
    void FadeOut(int channel) { calls.push_back("out " + std::to_string(channel)); }
    int Play3D(const SpriteSoundRecord& r)
    {
        calls.push_back(std::string("3d ") + r.name);
        return play3dSucceeds ? 1 : -1;
    }
    int CountOwner(int group) { return owners[group]; }
};

struct Fixture {
    FakeEnv env;
    LoopSounds sounds{};
    Fixture(bool enabled = true) { sounds.enabled = enabled; }
    template<class T> std::vector<T*> Values(const std::list<T*>& list) { return {list.begin(), list.end()}; }
    std::vector<LoopRecord*> Loops() { return Values(sounds.loops); }
    std::vector<SpriteSoundRecord*> Sprites() { return Values(sounds.sprites); }
};
const float kHere[3] = {100, 100, 0};
} // namespace

OBL_TEST_CASE("loop_sounds.disabled_object_ignores_everything")
{
    Fixture f(false);
    loop_sounds::Start(f.sounds, "flies", 1, 1.0f, true, f.env);
    loop_sounds::PlaySpriteSound(f.sounds, "gull", kHere, 1.0f, 1, -1, f.env);
    loop_sounds::UpdateLoops(f.sounds, f.env);
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env);
    loop_sounds::Stop(f.sounds, 1, f.env);
    OBL_CHECK(f.Loops().empty());
    OBL_CHECK(f.Sprites().empty());
    OBL_CHECK(f.env.calls.empty());
    OBL_CHECK(f.env.records.empty());
}

OBL_TEST_CASE("loop_sounds.start_appends_one_record_per_object_id")
{
    Fixture f;
    loop_sounds::Start(f.sounds, "flies", 7, 0.5f, true, f.env);
    loop_sounds::Start(f.sounds, "ferryhoot", 9, 0.25f, false, f.env);
    loop_sounds::Start(f.sounds, "other", 7, 1.0f, false, f.env); // same id: ignored
    auto loops = f.Loops();
    OBL_REQUIRE(loops.size() == 2);
    OBL_CHECK_EQ(loops[0]->objectId, 7);
    OBL_CHECK_EQ(loops[0]->volume, 0.5f);
    OBL_CHECK(std::string(loops[0]->name) == "flies");
    OBL_CHECK_EQ(loops[0]->playing, 0);
    OBL_CHECK_EQ(loops[0]->loop, 1);
    OBL_CHECK_EQ(loops[0]->channel, -1);
    OBL_CHECK_EQ(loops[1]->objectId, 9);
    OBL_CHECK_EQ(loops[1]->loop, 0);
    OBL_CHECK_EQ(f.env.records.size(), 2u);
    OBL_CHECK_EQ(f.sounds.loops.size(), 2u);
}

OBL_TEST_CASE("loop_sounds.stop_fades_only_a_playing_loop_and_frees_it")
{
    Fixture f;
    loop_sounds::Start(f.sounds, "a", 1, 1.0f, true, f.env);
    loop_sounds::Start(f.sounds, "b", 2, 1.0f, true, f.env);
    f.env.objects[2] = {100, 100};
    loop_sounds::UpdateLoops(f.sounds, f.env); // object 1 is missing: only 2 starts
    f.env.calls.clear();
    loop_sounds::Stop(f.sounds, 1, f.env);
    OBL_CHECK(f.env.calls.empty());
    loop_sounds::Stop(f.sounds, 2, f.env);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"out 10"});
    loop_sounds::Stop(f.sounds, 2, f.env); // already gone
    OBL_CHECK(f.Loops().empty());
    OBL_CHECK(f.env.records.empty());
}

OBL_TEST_CASE("loop_sounds.update_loops_starts_scales_and_fades_by_distance")
{
    Fixture f;
    loop_sounds::Start(f.sounds, "a", 1, 0.8f, true, f.env);
    f.env.objects[1] = {100, 100}; // at the listener: inside the minimum radius
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK(f.Loops()[0]->playing == 1);
    OBL_CHECK(f.Loops()[0]->channel == 10);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"play a " + std::to_string(0.8f)});
    f.env.calls.clear();
    f.env.objects[1] = {200, 100}; // distance 100: halfway between 50 and 150
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"to 10 " + std::to_string(0.5f * 0.8f)});
    f.env.calls.clear();
    f.env.objects[1] = {300, 100}; // 200 > maximum: stops
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"out 10"});
    OBL_CHECK(f.Loops()[0]->playing == 0);
    f.env.calls.clear();
    loop_sounds::UpdateLoops(f.sounds, f.env); // out of range and silent: nothing
    OBL_CHECK(f.env.calls.empty());
    f.env.objects[1] = {250, 100}; // exactly the maximum: not strictly inside, no start
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK(f.env.calls.empty());
}

OBL_TEST_CASE("loop_sounds.update_loops_failed_start_keeps_the_record_silent")
{
    Fixture f;
    loop_sounds::Start(f.sounds, "a", 1, 1.0f, true, f.env);
    f.env.objects[1] = {100, 100};
    f.env.nextChannel = -1;
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK_EQ(f.Loops()[0]->playing, 0);
    OBL_CHECK_EQ(f.Loops()[0]->channel, -1);
    OBL_CHECK_EQ(f.env.calls.size(), 1u);
}

OBL_TEST_CASE("loop_sounds.update_loops_object_height_is_part_of_the_distance")
{
    Fixture f;
    loop_sounds::Start(f.sounds, "a", 1, 1.0f, true, f.env);
    f.env.objects[1] = {100, 100 + 100}; // the caller folded the overlay offset into y
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"play a " + std::to_string(0.5f)});
}

OBL_TEST_CASE("loop_sounds.one_shot_loops_yield_to_an_audible_music_stream")
{
    Fixture f;
    loop_sounds::Start(f.sounds, "once", 1, 1.0f, false, f.env);
    loop_sounds::Start(f.sounds, "loop", 2, 1.0f, true, f.env);
    f.env.objects[1] = f.env.objects[2] = {100, 100};
    f.env.stream = true;
    f.env.music = 0.5f;
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"play loop " + std::to_string(1.0f)}); // only the looping one starts
    f.env.stream = false;
    f.env.calls.clear();
    loop_sounds::UpdateLoops(f.sounds, f.env); // no stream: the one-shot starts, the loop keeps fading
    OBL_CHECK(f.env.calls.size() == 2);
    OBL_CHECK(f.env.calls[0].rfind("play once", 0) == 0);
    f.env.stream = true;
    f.env.calls.clear();
    loop_sounds::UpdateLoops(f.sounds, f.env); // the stream is back: the one-shot is faded out
    OBL_CHECK(f.env.calls[0].rfind("out ", 0) == 0);
    OBL_CHECK_EQ(f.Loops()[0]->playing, 0);
    OBL_CHECK_EQ(f.Loops()[1]->playing, 1);
    f.env.music = 0.05f; // not above the threshold
    f.env.calls.clear();
    loop_sounds::UpdateLoops(f.sounds, f.env);
    OBL_CHECK_EQ(f.Loops()[0]->playing, 1);
}

OBL_TEST_CASE("loop_sounds.play_sprite_sound_queues_and_purges_activated_records")
{
    Fixture f;
    const float position[3] = {1, 2, 3};
    loop_sounds::PlaySpriteSound(f.sounds, "gull", position, 0.7f, 3, 42, f.env);
    auto sprites = f.Sprites();
    OBL_REQUIRE(sprites.size() == 1);
    OBL_CHECK(std::string(sprites[0]->name) == "gull");
    OBL_CHECK_EQ(sprites[0]->position[2], 3.0f);
    OBL_CHECK_EQ(sprites[0]->volume, 0.7f);
    OBL_CHECK_EQ(sprites[0]->limit, 3);
    OBL_CHECK_EQ(sprites[0]->group, 42);
    OBL_CHECK_EQ(sprites[0]->activated, 0);
    sprites[0]->activated = 1; // already tried once
    loop_sounds::PlaySpriteSound(f.sounds, "wave", position, 1.0f, 1, -1, f.env);
    sprites = f.Sprites();
    OBL_REQUIRE(sprites.size() == 1); // the activated one was dropped, the new one stays
    OBL_CHECK(std::string(sprites[0]->name) == "wave");
    OBL_CHECK_EQ(f.env.records.size(), 1u);
}

OBL_TEST_CASE("loop_sounds.update_sprite_sounds_play_expire_or_wait")
{
    Fixture f;
    f.env.listener = {0, 0, 0, 50, 100};
    const float near[3] = {30, 40, 0}, far[3] = {100, 100, 0};
    loop_sounds::PlaySpriteSound(f.sounds, "near", near, 1.0f, 1, -1, f.env);
    f.env.play3dSucceeds = true;
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"3d near"});
    OBL_CHECK(f.Sprites().empty()); // played: gone

    loop_sounds::PlaySpriteSound(f.sounds, "far", far, 1.0f, 1, -1, f.env);
    f.env.calls.clear();
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env); // out of range: dropped without trying
    OBL_CHECK(f.env.calls.empty());
    OBL_CHECK(f.Sprites().empty());

    loop_sounds::PlaySpriteSound(f.sounds, "busy", near, 1.0f, 1, -1, f.env);
    f.env.play3dSucceeds = false; // no free channel: kept for the grace period
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env);
    OBL_REQUIRE(f.Sprites().size() == 1);
    OBL_CHECK_EQ(f.Sprites()[0]->activated, 1);
    OBL_CHECK_EQ(f.Sprites()[0]->timer, 0.25f);
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env);
    OBL_CHECK_EQ(f.Sprites()[0]->timer, 0.25f - 0.1f);
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env);
    OBL_REQUIRE(f.Sprites().size() == 1);
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env); // 0.25 - 0.3 <= 0: expired
    OBL_CHECK(f.Sprites().empty());
    OBL_CHECK(f.env.records.empty());
}

OBL_TEST_CASE("loop_sounds.update_sprite_sounds_respect_the_group_limit")
{
    Fixture f;
    f.env.listener = {0, 0, 0, 50, 100};
    const float near[3] = {1, 1, 0};
    loop_sounds::PlaySpriteSound(f.sounds, "a", near, 1.0f, 2, 5, f.env);
    f.env.owners[5] = 2; // at the limit: dropped without playing
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env);
    OBL_CHECK(f.env.calls.empty());
    OBL_CHECK(f.Sprites().empty());
    loop_sounds::PlaySpriteSound(f.sounds, "b", near, 1.0f, 3, 5, f.env);
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env); // 2 < 3: plays
    OBL_CHECK(f.env.calls == std::vector<std::string>{"3d b"});
    f.env.calls.clear();
    loop_sounds::PlaySpriteSound(f.sounds, "c", near, 1.0f, 0, -1, f.env); // group -1: no limit even with limit 0
    loop_sounds::UpdateSpriteSounds(f.sounds, 0.1f, f.env);
    OBL_CHECK(f.env.calls == std::vector<std::string>{"3d c"});
}

OBL_TEST_CASE("loop_sounds.destruct_releases_every_record")
{
    Fixture f;
    f.env.objects[1] = {100, 100};
    loop_sounds::Start(f.sounds, "a", 1, 1.0f, true, f.env);
    loop_sounds::Start(f.sounds, "b", 2, 1.0f, true, f.env);
    loop_sounds::PlaySpriteSound(f.sounds, "gull", kHere, 1.0f, 1, -1, f.env);
    loop_sounds::PlaySpriteSound(f.sounds, "wave", kHere, 1.0f, 1, -1, f.env);
    loop_sounds::Destruct(f.sounds, f.env);
    OBL_CHECK(f.env.records.empty());
    OBL_CHECK(f.sounds.loops.empty());
    OBL_CHECK(f.sounds.sprites.empty());
}
