#pragma once
#include <cstdint>

namespace obl::audio {
struct SpriteSound {
    char* m_name;
    char*** m_sounds;
    int* m_counts;
    int* m_next;
    int* m_groups;
    float** m_volumes;
};
struct BuildingSound {
    char* m_name;
    char* m_sound;
    float m_volume;
    uint8_t m_loop;
    uint8_t pad[3];
};
struct SoundEvent {
    char* m_name;
    int m_count;
    int m_group;
    float* m_volumes;
    char** m_sounds;
};
struct ScriptedSound {
    SpriteSound m_sprites[30];
    int m_numSprites;
    BuildingSound m_buildings[20];
    int m_numBuildings;
    SoundEvent* m_events;
    int m_numEvents;
    int* m_groupLimits; // by group index: the position in the document's `groups`
    int m_numGroups;
    float m_maxVolume, m_maxNumber, m_fadeTime;

    // Construct also makes this the scenario's scripted sound (game::GetScriptedSound); Destruct clears it again.
    ScriptedSound* Construct();
    void Destruct();
    // Reads the converted sound script of the selected pack; the path argument is not used.
    void Load(const char* unusedPath);
    bool PlaySpriteSound(const char* name, int animation, const float* position);
    void StartBuildingSound(int id);
    // Static because a building may stop its sound after the scenario's sounds are gone.
    static void StopBuildingSound(int id);
    // One random sample of the event; an event the script does not bind, or binds to no samples, is silent and draws nothing.
    void PlayEvent(const char* event, const float* position);
};
} // namespace obl::audio
