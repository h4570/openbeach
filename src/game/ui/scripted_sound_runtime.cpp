#include <new>

#include "core/char16.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/sound_api.h"
#include "game/ui/loop_sound_control.h"
#include "game/ui/scripted_sound_runtime.h"
#include "game/ui/sprite.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/sound_types.h"
#include "game/objects/object_base.h"
#include "core/log.h"
#include "core/pack_path.h"
#include "game/ui/scripted_sound.h"
#include "core/ascii_case.h"
#include "core/call_counter.h"
#include "core/wide_path.h"
#include "game/game/diagnostics.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace obl::audio {
namespace {
using audio::ScriptedSound;
// `call-report ui.scripted-sound-load` and `ui.scripted-sound-runtime` count the operations.
test::CallCounter g_counter("ui.scripted-sound-load");
test::CallCounter g_runtime("ui.scripted-sound-runtime"), g_runtimeCheck("ui.scripted-sound-checks");
std::vector<int> AnimationCounts(const ScriptedSound& s);

// <game dir>/packs/base, where the converted sound script is (sounds/sound_script.json, converter/sounds.md).
std::filesystem::path g_packDirectory;

class RuntimeHost : public scripted_sound::RuntimeHost {
public:
    explicit RuntimeHost(bool silent = false) : m_silent(silent) {}
    int AnimationCount(const char* name) override
    {
        auto* bank = ui::GetSpriteBank();
        m_sprite = (static_cast<ui::SpriteBank*>(bank))->CreateObservedInstance(name);
        return m_sprite->m_sprite->m_animCount;
    }
    void ReleaseSprite() override
    {
        (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(m_sprite);
        m_sprite = nullptr;
    }
    void Free(void* memory) override { ::operator delete(memory); }
    void Play(const char* name, float* position, float volume, int limit, int group) override
    {
        if (!m_silent) CurrentLoopSounds()->PlaySpriteSound(name, position, volume, limit, group);
    }
    const char* ObjectName(int id) override
    {
        auto* object = object::object_manager::ObjectById(game::GetObjectManager(), id);
        return object ? object->m_Name : nullptr;
    }
    void Start(const char* name, int id, float volume, bool loop) override { CurrentLoopSounds()->StartLoop(name, id, volume, loop); }
    void Stop(int id) override { CurrentLoopSounds()->StopLoop(id); }

private:
    bool m_silent;
    ui::SpriteInstance* m_sprite = nullptr;
};
} // namespace

} // namespace obl::audio

namespace obl::audio {
ScriptedSound* ScriptedSound::Construct()
{
    game::SetScriptedSound(this);
    g_runtime.Count();
    return scripted_sound::Construct(*this);
}
void ScriptedSound::Destruct()
{
    RuntimeHost host;
    scripted_sound::Destruct(*this, host);
    game::SetScriptedSound(nullptr);
    g_runtime.Count();
}
bool ScriptedSound::PlaySpriteSound(const char* name, int animation, const float* position)
{
    RuntimeHost host;
    bool result = scripted_sound::PlaySpriteSound(*this, name, animation, position, host);
    g_runtime.Count();
    return result;
}
void ScriptedSound::StartBuildingSound(int id)
{
    RuntimeHost host;
    scripted_sound::StartBuildingSound(*this, id, host);
    g_runtime.Count();
}
void ScriptedSound::StopBuildingSound(int id)
{
    RuntimeHost host;
    scripted_sound::StopBuildingSound(id, host);
    g_runtime.Count();
}
} // namespace obl::audio

namespace obl::audio {
namespace {
class Host : public scripted_sound::Host {
public:
    explicit Host(bool scratch) : m_scratch(scratch) {}
    ~Host()
    {
        ReleaseSprite();
        for (auto* p : m_allocations) std::free(p);
    }
    void* Allocate(size_t bytes) override
    {
        // Untyped script tables and names are released by RuntimeHost::Free; scratch buffers by this host.
        void* p = m_scratch ? std::malloc(bytes ? bytes : 1) : ::operator new(bytes);
        if (m_scratch && p) m_allocations.push_back(p);
        return p;
    }
    bool AcquireSprite(const char* name, std::vector<std::string>& animations) override
    {
        auto* bank = ui::GetSpriteBank();
        m_sprite = (static_cast<ui::SpriteBank*>(bank))->CreateObservedInstance(name);
        if (!m_sprite || !m_sprite->m_sprite) return false;
        const auto* sprite = m_sprite->m_sprite;
        for (int i = 0; i < sprite->m_animCount; ++i) animations.emplace_back(sprite->m_anims[i]->m_name);
        return true;
    }
    void ReleaseSprite() override
    {
        if (!m_sprite) return;
        (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(m_sprite);
        m_sprite = nullptr;
    }
    bool HasSound(const char* name) override { return audio::CurrentSound()->HasSample(name); }

private:
    bool m_scratch;
    ui::SpriteInstance* m_sprite = nullptr;
    std::vector<void*> m_allocations;
};

std::vector<int> AnimationCounts(const ScriptedSound& s)
{
    std::vector<int> result;
    const auto* bank = ui::GetSpriteBank();
    for (int i = 0; i < s.m_numSprites; ++i) {
        int count = -1;
        for (int j = 0; bank && j < bank->m_spriteCount; ++j)
            if (ascii::EqualsNoCase(bank->m_sprites[j]->m_name, s.m_sprites[i].m_name)) {
                count = bank->m_sprites[j]->m_animCount;
                break;
            }
        if (count < 0) return {};
        result.push_back(count);
    }
    return result;
}
void Fatal(const std::string& error)
{
    game::FatalError("sound script: %s", error.c_str());
}
} // namespace
} // namespace obl::audio

namespace obl::audio {
void ScriptedSound::Load(const char*)
{
    Host host(false);
    const auto path = pack_path::Resolve(g_packDirectory, "sounds/sound_script.json");
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Fatal("cannot read " + path.string());
        return;
    }
    std::string text(std::istreambuf_iterator<char>(file), {}), error;
    if (!scripted_sound::LoadDocument(*this, text, host, error)) {
        Fatal(path.string() + ": " + error);
        return;
    }
    g_counter.Count();
}
} // namespace obl::audio

namespace obl::audio {
namespace {
void DriveRuntime(int count)
{
    auto* saved = game::GetScriptedSound();
    for (int iteration = 0; iteration < count; ++iteration) {
        ScriptedSound local;
        std::memset(&local, 0x5a, sizeof(local));
        bool ok = local.Construct() == &local && game::GetScriptedSound() == &local;
        local.Load("");
        auto counts = AnimationCounts(local);
        float position[3] = {10, 20, 0};
        ok &= !local.PlaySpriteSound("__missing_sprite__", 0, position);
        for (int i = 0; i < local.m_numSprites; ++i) {
            auto& r = local.m_sprites[i];
            for (int a = 0; a < counts[i]; ++a) {
                const int sounds = r.m_counts[a];
                // Exercise every sample and wrap to the first again, including empty slots.
                for (int j = 0; j <= sounds; ++j) {
                    bool played = local.PlaySpriteSound(r.m_name, a, position);
                    ok &= played == (sounds != 0);
                    if (sounds != 0) ok &= r.m_next[a] == (j + 1) % sounds;
                }
            }
        }
        local.StartBuildingSound(-1);
        local.StopBuildingSound(-1);
        auto* manager = game::GetObjectManager();
        for (uint32_t i = 0; manager && i < manager->m_buildings.size(); ++i) {
            auto* object = manager->m_buildings[i];
            if (!object) continue;
            local.StartBuildingSound(object->m_id);
            local.StopBuildingSound(object->m_id);
        }
        local.Destruct();
        ok &= local.m_numSprites == 0 && local.m_numBuildings == 0 && local.m_numEvents == 0 && local.m_numGroups == 0 &&
                game::GetScriptedSound() == nullptr;
        // Repeated destruction must not follow the stale pointers retained in the records.
        local.Destruct();
        g_runtimeCheck.Record(ok, "loaded lifecycle/all animation cycles/building dispatch/global");
        game::SetScriptedSound(saved);
    }
}
} // namespace
void ConfigureScriptedSound(const Text16& gameDirectory)
{
    g_packDirectory = WidePath(gameDirectory) / pack_path::Selected();
    g_runtimeCheck.SetDriver(DriveRuntime);
}
} // namespace obl::audio
