#pragma once

#include <cstdint>
#include <list>

// g_LoopSounds: world sounds that follow objects (loops) and one-shot positional sounds from Sounds.dat sprites.
// Both lists hold heap records in arrival order.
namespace obl::audio {

// Pending positional sound (`LoopSounds::PlaySpriteSound`): played as soon as the listener is in range and the owner group has room.
struct SpriteSoundRecord {
    char name[0x20];
    float position[3];
    float volume; // scale of Sound::Play3DScaled
    int32_t limit; // most samples of `group` that may play at once
    int32_t group; // owner id for Sound::CountOwnerSamples; -1 = unlimited
    uint8_t activated; // set on the first failed attempt; the record is then retried until `timer` runs out (or a later PlaySpriteSound purges it)
    float timer; // seconds left of the grace period
};

// Looped sound of one object (`LoopSounds::StartLoop`), keyed by the object id.
struct LoopRecord {
    int32_t objectId;
    float volume;
    char name[0x20];
    uint8_t playing;
    uint8_t loop; // 0 = one-shot ambience that yields to the music stream
    int32_t channel; // Sound channel while `playing`, -1 before the first start
};

struct LoopSounds {
    uint8_t enabled; // Sound enabled at startup; 0 turns every method into a no-op
    std::list<SpriteSoundRecord*> sprites;
    std::list<LoopRecord*> loops;

    // Binds this object as the current sounds (audio::CurrentLoopSounds) and empties both lists.
    LoopSounds* Construct(bool soundEnabled);
    void Destruct();
    void StartLoop(const char* name, int objectId, float volume, bool loop);
    void StopLoop(int objectId);
    void PlaySpriteSound(const char* name, float* position, float volume, int limit, int group);
    void UpdateLoops(float seconds);
    void UpdateSpriteSounds(float seconds);
    // One frame: loops first, then the pending positional sounds.
    void UpdateFrame(float seconds);
};

} // namespace obl::audio
