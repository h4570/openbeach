#include "game/ui/scripted_sound.h"
#include "core/ascii_case.h"
#include <cstring>

namespace obl::scripted_sound {
audio::ScriptedSound* Construct(audio::ScriptedSound& s)
{
    s.m_numSprites = s.m_numBuildings = s.m_numEvents = s.m_numGroups = 0;
    s.m_events = nullptr;
    s.m_groupLimits = nullptr;
    s.m_maxVolume = 0.75f;
    s.m_maxNumber = 50.0f;
    s.m_fadeTime = 0.2f;
    return &s;
}
void Destruct(audio::ScriptedSound& s, RuntimeHost& h)
{
    for (int i = 0; i < s.m_numSprites; ++i) {
        auto& r = s.m_sprites[i];
        const int animations = h.AnimationCount(r.m_name);
        for (int a = 0; a < animations; ++a) {
            for (int j = 0; j < r.m_counts[a]; ++j) h.Free(r.m_sounds[a][j]);
            // Empty animation slots contain uninitialized pointers.
            if (r.m_counts[a] != 0) {
                h.Free(r.m_sounds[a]);
                h.Free(r.m_volumes[a]);
            }
        }
        h.ReleaseSprite();
        h.Free(r.m_sounds);
        h.Free(r.m_counts);
        h.Free(r.m_next);
        h.Free(r.m_groups);
        h.Free(r.m_volumes);
        h.Free(r.m_name);
    }
    s.m_numSprites = 0;
    for (int i = 0; i < s.m_numBuildings; ++i) {
        h.Free(s.m_buildings[i].m_name);
        h.Free(s.m_buildings[i].m_sound);
    }
    s.m_numBuildings = 0;
    for (int i = 0; i < s.m_numEvents; ++i) {
        auto& r = s.m_events[i];
        h.Free(r.m_name);
        h.Free(r.m_volumes);
        for (int j = 0; j < r.m_count; ++j) h.Free(r.m_sounds[j]);
        h.Free(r.m_sounds);
    }
    s.m_numEvents = 0;
    if (s.m_events) h.Free(s.m_events);
    s.m_events = nullptr;
    s.m_numGroups = 0;
    if (s.m_groupLimits) h.Free(s.m_groupLimits);
    s.m_groupLimits = nullptr;
}
bool PlaySpriteSound(audio::ScriptedSound& s, const char* name, int animation, const float* position, RuntimeHost& h)
{
    for (int i = 0; i < s.m_numSprites; ++i) {
        auto& r = s.m_sprites[i];
        if (!ascii::EqualsNoCase(name, r.m_name)) continue;
        const int count = r.m_counts[animation];
        if (count == 0) return false;
        float copy[3] = {position[0], position[1], position[2]};
        const int group = r.m_groups[animation], next = r.m_next[animation];
        h.Play(r.m_sounds[animation][next], copy, r.m_volumes[animation][next], group == -1 ? -1 : s.m_groupLimits[group], group);
        r.m_next[animation] = (next + 1) % count;
        return true;
    }
    return false;
}
void StartBuildingSound(const audio::ScriptedSound& s, int objectId, RuntimeHost& h)
{
    const char* name = h.ObjectName(objectId);
    if (!name) return;
    for (int i = 0; i < s.m_numBuildings; ++i) {
        const auto& r = s.m_buildings[i];
        if (ascii::EqualsNoCase(name, r.m_name)) {
            h.Start(r.m_sound, objectId, r.m_volume, r.m_loop != 0);
            return;
        }
    }
}
void StopBuildingSound(int objectId, RuntimeHost& h)
{
    h.Stop(objectId);
}
const audio::SoundEvent* FindEvent(const audio::ScriptedSound& s, const char* name)
{
    for (int i = 0; i < s.m_numEvents; ++i)
        if (std::strcmp(s.m_events[i].m_name, name) == 0) return &s.m_events[i];
    return nullptr;
}
} // namespace obl::scripted_sound
