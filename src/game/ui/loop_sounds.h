#pragma once

#include "game/ui/loop_sound_types.h"

#include <cmath>
#include <cstddef>
#include <cstring>

// LoopSounds::StartLoop, StopLoop, PlaySpriteSound, Destruct and the two per-frame updates UpdateLoops and
// UpdateSpriteSounds, both called with the frame seconds by LoopSounds::UpdateFrame from the game's update (the loops one ignores
// the argument).
// No game memory here: the glue in src/game/ui/loop_sound_control.cpp supplies the Env and the unit tests a fake.
//   T* NewRecord<T>()                  typed record allocation          void DeleteRecord(T*)       typed record release
//   Listener()                          Sound::position / minDistance / maxDistance      MusicVolume(), StreamActive()
//   bool ObjectPosition(id, x, y)       the object's position, y plus its GetOverlayOffset(); false = no such object
//   int PlayScaled(record, volume)      Sound::PlayScaled(record.name, volume, 2, 0.1)          void FadeTo(channel, volume)     Sound::FadeChannelTo(.., 0.1)
//   void FadeOut(channel)               Sound::FadeChannelOut(.., 0.2)
//   int Play3D(record)                  Sound::Play3DScaled(name, volume, position, 1, group)   int CountOwner(group)
namespace obl::loop_sounds {

using audio::LoopRecord;
using audio::LoopSounds;
using audio::SpriteSoundRecord;

inline constexpr float kGraceSeconds = 0.25f; // retry window of a positional sound that could not start yet
inline constexpr float kStreamMusicVolume = 0.05f; // one-shot loops yield to the music stream above this volume

struct Listener {
    float x, y, z, minDistance, maxDistance;
};

// Into a record's name field, cut to its size; no sample name comes near the limit.
inline void CopyName(char* destination, const char* name)
{
    std::strncpy(destination, name, 0x1f);
    destination[0x1f] = '\0';
}

template<class Env> void Start(LoopSounds& sounds, const char* name, int32_t objectId, float volume, bool loop, Env& env)
{
    if (!sounds.enabled) return;
    for (const LoopRecord* record : sounds.loops)
        if (record->objectId == objectId) return;
    auto* record = env.template NewRecord<LoopRecord>();
    record->channel = -1;
    record->volume = volume;
    record->objectId = objectId;
    record->playing = 0;
    record->loop = loop;
    CopyName(record->name, name);
    sounds.loops.push_back(record);
}

template<class Env> void Stop(LoopSounds& sounds, int32_t objectId, Env& env)
{
    if (!sounds.enabled) return;
    for (auto it = sounds.loops.begin(); it != sounds.loops.end(); ++it) {
        LoopRecord* record = *it;
        if (record->objectId != objectId) continue;
        if (record->playing) env.FadeOut(record->channel);
        env.DeleteRecord(record);
        sounds.loops.erase(it);
        return;
    }
}

template<class Env>
void PlaySpriteSound(LoopSounds& sounds, const char* name, const float* position, float volume, int32_t limit, int32_t group, Env& env)
{
    if (!sounds.enabled) return;
    auto* record = env.template NewRecord<SpriteSoundRecord>();
    record->activated = 0;
    CopyName(record->name, name);
    record->position[0] = position[0];
    record->position[1] = position[1];
    record->position[2] = position[2];
    record->volume = volume;
    record->limit = limit;
    record->group = group;
    sounds.sprites.push_back(record);
    // Records that already failed once are dropped whenever a new one arrives.
    for (auto it = sounds.sprites.begin(); it != sounds.sprites.end();) {
        if ((*it)->activated) {
            env.DeleteRecord(*it);
            it = sounds.sprites.erase(it);
        } else {
            ++it;
        }
    }
}

// Volume factor of a loop at `distance`: full inside the minimum radius, linear to zero at the maximum.
inline float Attenuation(float distance, const Listener& listener)
{
    if (listener.minDistance <= distance) return 1.0f - (distance - listener.minDistance) / (listener.maxDistance - listener.minDistance);
    return 1.0f;
}

template<class Env> void UpdateLoops(LoopSounds& sounds, Env& env)
{
    if (!sounds.enabled) return;
    const Listener listener = env.Listener();
    for (LoopRecord* record : sounds.loops) {
        float x, y;
        if (!env.ObjectPosition(record->objectId, x, y)) continue;
        const float dx = listener.x - x, dy = listener.y - y; // y already includes the object's height
        const float distance = std::sqrt(dx * dx + dy * dy);
        // A one-shot (non-loop) sound yields to a running music stream that is audible.
        auto yields = [&] { return !record->loop && env.StreamActive() && kStreamMusicVolume < env.MusicVolume(); };
        if (!record->playing) {
            if (distance < listener.maxDistance && !yields()) {
                const int channel = env.PlayScaled(*record, Attenuation(distance, listener) * record->volume);
                record->channel = channel;
                if (channel != -1) record->playing = 1;
            }
        } else if (yields() || listener.maxDistance < distance) {
            env.FadeOut(record->channel);
            record->playing = 0;
        } else {
            env.FadeTo(record->channel, Attenuation(distance, listener) * record->volume);
        }
    }
}

template<class Env> void UpdateSpriteSounds(LoopSounds& sounds, float seconds, Env& env)
{
    if (!sounds.enabled) return;
    const Listener listener = env.Listener();
    for (auto it = sounds.sprites.begin(); it != sounds.sprites.end();) {
        SpriteSoundRecord* record = *it;
        const float dx = listener.x - record->position[0], dy = listener.y - record->position[1];
        bool finished = listener.maxDistance * listener.maxDistance < dx * dx + dy * dy;
        if (!finished && record->group != -1 && record->limit <= env.CountOwner(record->group)) finished = true;
        if (!finished && env.Play3D(*record) != -1) finished = true;
        if (!finished) {
            if (!record->activated) {
                record->activated = 1;
                record->timer = kGraceSeconds;
            } else {
                const float before = record->timer;
                record->timer = before - seconds;
                if (before - seconds <= 0.0f) finished = true;
            }
        }
        if (finished) {
            env.DeleteRecord(record);
            it = sounds.sprites.erase(it);
        } else {
            ++it;
        }
    }
}

template<class Env> void Destruct(LoopSounds& sounds, Env& env)
{
    for (SpriteSoundRecord* record : sounds.sprites)
        if (record) env.DeleteRecord(record);
    for (LoopRecord* record : sounds.loops)
        if (record) env.DeleteRecord(record);
    sounds.sprites.clear();
    sounds.loops.clear();
}

} // namespace obl::loop_sounds
