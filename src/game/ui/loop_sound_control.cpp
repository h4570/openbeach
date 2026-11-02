#include "game/ui/sound_api.h"
#include "game/ui/loop_sound_control.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/sound_types.h"
#include "game/objects/object_base.h"
#include "core/log.h"
#include "game/ui/loop_sounds.h"
#include "core/call_counter.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <vector>

namespace obl::audio {
namespace {
using audio::LoopRecord;
using audio::LoopSounds;
using audio::SpriteSoundRecord;
using Listener = loop_sounds::Listener;

// `call-report ui.loop-sounds` counts the operations.
test::CallCounter g_counter("ui.loop-sounds");

float OverlayOffset(object::Base* object)
{
    return static_cast<float>(object->GetOverlayOffset());
}

// The game's Sound and object manager behind the pure logic.
class GameEnv {
public:
    template<class T> T* NewRecord() { return new T; }
    template<class T> void DeleteRecord(T* record) { delete record; }
    Listener Listener() const
    {
        const auto* sound = audio::CurrentSound();
        return {sound->position[0], sound->position[1], sound->position[2], sound->minDistance, sound->maxDistance};
    }
    float MusicVolume() const { return audio::CurrentSound()->musicVolume; }
    bool StreamActive() const { return audio::CurrentSound()->IsStreamActive() != 0; }
    bool ObjectPosition(int id, float& x, float& y) const
    {
        auto* object = object::object_manager::ObjectById(game::GetObjectManager(), id);
        if (!object) return false;
        x = object->m_WorldXYZ[0];
        y = object->m_WorldXYZ[1] + OverlayOffset(object);
        return true;
    }
    int PlayScaled(const LoopRecord& record, float volume) { return audio::CurrentSound()->PlayScaled(record.name, volume, 2, 0.1f); }
    void FadeTo(int channel, float volume) { audio::CurrentSound()->FadeChannelTo(channel, volume, 0.1f); }
    void FadeOut(int channel) { audio::CurrentSound()->FadeChannelOut(channel, 0.2f); }
    int Play3D(const SpriteSoundRecord& record)
    {
        return audio::CurrentSound()->Play3DScaled(record.name, record.volume, const_cast<float*>(record.position), 1, record.group);
    }
    int CountOwner(int group) { return audio::CurrentSound()->CountOwnerSamples(group); }
};

} // namespace

audio::LoopSounds* CurrentLoopSounds()
{
    return game::GetContext().loopSounds;
}

void SetCurrentLoopSounds(LoopSounds* sound)
{
    game::GetContext().loopSounds = sound;
}

} // namespace obl::audio

namespace obl::audio {
void LoopSounds::StartLoop(const char* name, int objectId, float volume, bool loop)
{
    GameEnv env;
    loop_sounds::Start(*this, name, objectId, volume, loop, env);
    g_counter.Count();
}

void LoopSounds::StopLoop(int objectId)
{
    GameEnv env;
    loop_sounds::Stop(*this, objectId, env);
    g_counter.Count();
}

void LoopSounds::PlaySpriteSound(const char* name, float* position, float volume, int limit, int group)
{
    GameEnv env;
    loop_sounds::PlaySpriteSound(*this, name, position, volume, limit, group, env);
    g_counter.Count();
}

void LoopSounds::UpdateLoops(float)
{
    GameEnv env;
    loop_sounds::UpdateLoops(*this, env);
    g_counter.Count();
}

void LoopSounds::UpdateSpriteSounds(float seconds)
{
    GameEnv env;
    loop_sounds::UpdateSpriteSounds(*this, seconds, env);
    g_counter.Count();
}

void LoopSounds::Destruct()
{
    GameEnv env;
    loop_sounds::Destruct(*this, env);
    audio::SetCurrentLoopSounds(nullptr);
    g_counter.Count();
}

LoopSounds* LoopSounds::Construct(bool soundEnabled)
{
    audio::SetCurrentLoopSounds(this);
    enabled = soundEnabled;
    sprites.clear();
    loops.clear();
    g_counter.Record(sprites.empty() && loops.empty() && enabled == soundEnabled && audio::CurrentLoopSounds() == this, "Construct");
    return this;
}

void LoopSounds::UpdateFrame(float seconds)
{
    UpdateLoops(seconds);
    UpdateSpriteSounds(seconds);
    g_counter.Count();
}
} // namespace obl::audio

namespace obl::audio {
namespace {
// `call-drive ui.loop-sounds`: runs every operation on a private LoopSounds object, covering what a short scenario does not reach
// (duplicate ids, stopping playing loops, the grace period and the group limit of positional sounds, the destructor).
void DriveLoopSounds(int count)
{
    auto* const saved = CurrentLoopSounds();
    auto* manager = game::GetObjectManager();
    const Listener listener = GameEnv().Listener();
    for (int iteration = 0; iteration < count; ++iteration) {
        auto sounds = std::make_unique<LoopSounds>();
        sounds->Construct(iteration % 2 == 0);
        SetCurrentLoopSounds(saved); // the constructor made the scratch object the current one
        std::vector<int> ids;
        for (uint32_t i = 0; manager && i < manager->m_buildings.size() && ids.size() < 6; ++i)
            if (manager->m_buildings[i]) ids.push_back(static_cast<int>(manager->m_buildings[i]->m_id));
        ids.push_back(-12345); // no such object: skipped by the update
        for (size_t i = 0; i < ids.size(); ++i) {
            sounds->StartLoop("flies", ids[i], 0.5f + 0.1f * static_cast<float>(i), i % 2 == 0);
            sounds->StartLoop("flies", ids[i], 1.0f, true); // duplicate id: ignored
        }
        sounds->UpdateFrame(0.1f);
        sounds->UpdateFrame(0.1f);
        sounds->StopLoop(-999); // unknown id
        for (size_t i = 0; i < ids.size(); i += 2) sounds->StopLoop(ids[i]);
        float nearby[3] = {listener.x, listener.y, 0}, far[3] = {listener.x + listener.maxDistance * 4, listener.y, 0};
        for (int i = 0; i < 4; ++i) sounds->PlaySpriteSound("gull", i % 2 ? nearby : far, 1.0f, 1, i < 2 ? -1 : 77);
        for (int step = 0; step < 6; ++step) sounds->UpdateSpriteSounds(0.1f);
        sounds->PlaySpriteSound("gull", nearby, 1.0f, 2, 5);
        sounds->UpdateFrame(0.1f);
        sounds->Destruct();
    }
    SetCurrentLoopSounds(saved);
}
} // namespace

void ConfigureLoopSounds()
{
    g_counter.SetDriver(DriveLoopSounds);
}
} // namespace obl::audio
