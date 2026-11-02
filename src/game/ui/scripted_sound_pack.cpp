#include "game/ui/scripted_sound.h"

#include "core/ascii_case.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

// `sounds/sound_script.json` (assets.md#sounds). Sprite and building names match without case, so the records keep them as they are.
// Groups keep the document's order (ordered_json): a group's index is its position, used as the owner id of its playing samples.
namespace obl::scripted_sound {
namespace {
using json = nlohmann::ordered_json;

void Check(bool valid, const char* reason)
{
    if (!valid) throw std::runtime_error(reason);
}

// Names go to host memory with their NUL, as Load's Parser copies them.
char* Name(Host& host, const std::string& text)
{
    auto* p = static_cast<char*>(host.Allocate(text.size() + 1));
    Check(p != nullptr, "allocation failed");
    std::memcpy(p, text.c_str(), text.size() + 1);
    return p;
}

template<class T> T* Array(Host& host, size_t count)
{
    Check(count <= 100000, "invalid array count");
    auto* p = static_cast<T*>(host.Allocate(count * sizeof(T)));
    Check(p != nullptr, "allocation failed");
    return p;
}

// A reference to a group of the document; absent means none.
int GroupIndex(const json& entry, const json& groups)
{
    if (!entry.contains("group")) return -1;
    const std::string name = entry.at("group").get<std::string>();
    int index = 0;
    for (const auto& [group, limit] : groups.items()) {
        if (group == name) return index;
        ++index;
    }
    throw std::runtime_error("sound script names a group it does not define");
}

// Each sample is named, checked against the sample bank, then has its volume.
void Samples(Host& host, const json& list, char** sounds, float* volumes)
{
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string sample = list[i].at("sample").get<std::string>();
        sounds[i] = Name(host, sample);
        Check(host.HasSound(sample.c_str()), "sound not found");
        volumes[i] = list[i].at("volume").get<float>();
    }
}

void Sprite(audio::ScriptedSound& s, const std::string& name, const json& animations, const json& groups, Host& host)
{
    Check(s.m_numSprites >= 0 && s.m_numSprites < 30, "too many sprites");
    auto& r = s.m_sprites[s.m_numSprites];
    r.m_name = Name(host, name);
    std::vector<std::string> anims;
    Check(host.AcquireSprite(name.c_str(), anims), "sprite not found");
    struct Release {
        Host& host;
        ~Release() { host.ReleaseSprite(); }
    } release{host};
    const int n = static_cast<int>(anims.size());
    r.m_sounds = Array<char**>(host, n);
    r.m_counts = Array<int>(host, n);
    r.m_next = Array<int>(host, n);
    r.m_groups = Array<int>(host, n);
    r.m_volumes = Array<float*>(host, n);
    std::fill_n(r.m_counts, n, 0);
    std::fill_n(r.m_groups, n, -1);
    for (const auto& [animation, entry] : animations.items()) {
        const auto found =
                std::find_if(anims.begin(), anims.end(), [&](const std::string& a) { return ascii::EqualsNoCase(a, animation); });
        Check(found != anims.end(), "animation not found");
        const int a = static_cast<int>(found - anims.begin());
        const json& list = entry.at("sounds");
        const int count = static_cast<int>(list.size());
        Check(count > 0, "zero sound count");
        r.m_groups[a] = GroupIndex(entry, groups);
        r.m_sounds[a] = Array<char*>(host, count);
        r.m_counts[a] = count;
        r.m_next[a] = 0;
        r.m_volumes[a] = Array<float>(host, count);
        Samples(host, list, r.m_sounds[a], r.m_volumes[a]);
    }
    ++s.m_numSprites;
}

void Building(audio::ScriptedSound& s, const std::string& name, const json& entry, Host& host)
{
    Check(s.m_numBuildings >= 0 && s.m_numBuildings < 20, "too many buildings");
    auto& r = s.m_buildings[s.m_numBuildings];
    r.m_name = Name(host, name);
    const std::string sample = entry.at("sample").get<std::string>();
    r.m_sound = Name(host, sample);
    Check(host.HasSound(sample.c_str()), "building sound not found");
    r.m_volume = entry.at("volume").get<float>();
    r.m_loop = entry.at("loop").get<bool>() ? 1 : 0;
    ++s.m_numBuildings;
}

void Events(audio::ScriptedSound& s, const json& events, const json& groups, Host& host)
{
    s.m_events = Array<audio::SoundEvent>(host, events.size());
    for (const auto& [name, entry] : events.items()) {
        auto& r = s.m_events[s.m_numEvents];
        const json& list = entry.at("sounds");
        r.m_name = Name(host, name);
        r.m_group = GroupIndex(entry, groups);
        r.m_count = static_cast<int>(list.size());
        r.m_volumes = Array<float>(host, list.size());
        r.m_sounds = Array<char*>(host, list.size());
        Samples(host, list, r.m_sounds, r.m_volumes);
        ++s.m_numEvents;
    }
}

void Crowd(audio::ScriptedSound& s, const json& crowd)
{
    if (crowd.contains("max_number")) s.m_maxNumber = crowd.at("max_number").get<float>();
    if (crowd.contains("max_volume")) s.m_maxVolume = crowd.at("max_volume").get<float>();
    if (crowd.contains("fade_time")) s.m_fadeTime = crowd.at("fade_time").get<float>();
}

void Groups(audio::ScriptedSound& s, const json& groups, Host& host)
{
    s.m_groupLimits = Array<int>(host, groups.size());
    for (const auto& [name, group] : groups.items()) s.m_groupLimits[s.m_numGroups++] = group.at("limit").get<int>();
}

} // namespace

bool LoadDocument(audio::ScriptedSound& out, std::string_view text, Host& host, std::string& error)
{
    try {
        const json doc = json::parse(text.data(), text.data() + text.size());
        Check(doc.at("format").get<std::string>() == "obl.sound_script" && doc.at("version").get<int>() == 2,
                "not a sound script document");
        if (doc.contains("crowd")) Crowd(out, doc.at("crowd"));
        const json groups = doc.value("groups", json::object());
        Groups(out, groups, host);
        for (const auto& [name, animations] : doc.at("sprite_sounds").items()) Sprite(out, name, animations, groups, host);
        for (const auto& [name, entry] : doc.at("building_sounds").items()) Building(out, name, entry, host);
        Events(out, doc.at("events"), groups, host);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::scripted_sound
