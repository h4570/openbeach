#include "core/pack_path.h"
#include "game/ui/sprite_bank.h"

#include "game/game/game_accessors.h"
#include "core/ascii_case.h"
#include "core/config.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "game/ui/sprite_anim.h"
#include "formats/sprite_pack.h"
#include "game/ui/sprite_observer.h"
#include "core/call_counter.h"
#include "core/os.h"

#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <fcntl.h>

namespace obl::ui {
namespace {

using Bank = ui::SpriteBank;
using Sprite = ui::Sprite;
using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
test::CallCounter g_calls("ui.sprite-bank");

std::filesystem::path g_spritesDirectory; // <game dir>/packs/base/sprites, where the converted sprites are (sprite_pack.h)

int& StaleCursor()
{
    return game::GetContext().spriteStaleCursor;
}

[[noreturn]] void Failure(const char* detail)
{
    OBL_LOG("FatalError: sprite bank: %s", detail);
    std::_Exit(1); // the log is unbuffered: nothing to flush
}

// DestructSprite/DestructInstance and FreeBank release these typed arrays.
template<class T> T* Copy(const T* data, size_t count)
{
    auto* memory = new T[count];
    if (count) std::memcpy(memory, data, count * sizeof(T));
    return memory;
}

void ConstructSprite(Sprite* s);
void DestructSpriteInGame(Sprite* s);

void Apply(Sprite* instance, sprite_pack::Sprite& h, bool metadataOnly)
{
    instance->m_name = Copy(h.name.c_str(), h.name.size() + 1);
    instance->m_type = static_cast<int32_t>(h.type);
    if (h.type == 5) std::memcpy(instance->m_buildingExtra, h.depthExtents.data(), 4);
    instance->m_paletteModifierId = h.toneGroup;
    instance->m_paletteCount = h.paletteCount;
    instance->m_palettes = new uint16_t*[h.paletteCount];
    const std::vector<uint16_t> blank(metadataOnly ? sprite_pack::PaletteSize(h.type) : 0);
    for (int p = 0; p < h.paletteCount; ++p) {
        const auto& colors = metadataOnly ? blank : h.palettes[p];
        instance->m_palettes[p] = Copy(colors.data(), colors.size());
    }
    instance->m_animCount = static_cast<int32_t>(h.anims.size());
    instance->m_anims = new Anim*[h.anims.size()];
    for (size_t a = 0; a < h.anims.size(); ++a) {
        auto* anim = new Anim;
        anim->Construct();
        AttachPackAnim(anim, std::move(h.anims[a]), metadataOnly);
        instance->m_anims[a] = anim;
    }
}

// The bank from the converted sprites, in key order (lookups match names regardless of case and keys are unique, so the
int Load(Bank* b, sprite_pack::ReadMode mode)
{
    std::vector<std::string> keys;
    std::string error;
    if (!sprite_pack::ListSprites(g_spritesDirectory, keys, error)) Failure(error.c_str());
    const bool is555 = game::GetRenderer()->m_rgb555 != 0;
    b->m_headerFile = nullptr;
    b->m_datFile = -1;
    b->m_spriteCount = static_cast<int32_t>(keys.size());
    b->m_sprites = new Sprite*[keys.size()];
    for (size_t i = 0; i < keys.size(); ++i) {
        sprite_pack::Sprite converted;
        if (!sprite_pack::ReadSprite(g_spritesDirectory, keys[i], is555, converted, error, mode)) Failure(error.c_str());
        auto* s = new Sprite;
        ConstructSprite(s);
        Apply(s, converted, mode == sprite_pack::ReadMode::Metadata);
        b->m_sprites[i] = s;
    }
    return 0;
}

void ConstructInstance(Instance* i)
{
    i->m_name = nullptr;
    i->m_startTime = 0.0f;
    i->m_animType = 0;
    i->m_fps = 0x12;
    i->m_animIndex = i->m_frameIndex = i->m_direction = 0;
    i->m_paletteIndex = 0;
    i->m_lit = i->m_keyline = i->m_triggeredSound = 0;
    i->m_overridePalette = nullptr;
    i->m_sprite = nullptr;
    i->m_actorRenderOption = 0;
}

template<class Delete> void DestructInstance(Instance* i, Delete del)
{
    if (i->m_name) del(i->m_name);
    i->m_overridePalette = nullptr;
}

void ReleaseInstance(Instance* i);

Instance* Create(Bank* b, const char* name)
{
    Sprite* s = nullptr;
    for (int i = 0; i < b->m_spriteCount; ++i) {
        if (ascii::EqualsNoCase(b->m_sprites[i]->m_name, name)) {
            s = b->m_sprites[i];
            break;
        }
    }
    if (!s) {
        if (ascii::EqualsNoCase(name, "dummy")) Failure("bank has no dummy fallback");
        return Create(b, "dummy");
    }
    auto* result = new Instance;
    ConstructInstance(result);
    result->m_sprite = s;
    ++s->m_refCount;
    size_t size = std::strlen(name) + 1;
    result->m_name = new char[size];
    std::memcpy(result->m_name, name, size);
    return result;
}

void Release(Bank* b, Instance* i)
{
    ReleaseInstance(i);
}

void ConstructSprite(Sprite* s)
{
    s->m_name = nullptr;
    s->m_animCount = 0;
    s->m_anims = nullptr;
    s->m_refCount = 0;
    s->m_paletteCount = 0;
    s->m_palettes = nullptr;
    s->m_type = 1;
    s->m_paletteModifierId = 0;
}

// Deleter and anim destructor are parameters so the driver can record the exact order of releases.
template<class Delete, class DestructAnim> void DestructSprite(Sprite* s, Delete del, DestructAnim destructAnim)
{
    if (s->m_name) del(s->m_name);
    for (int i = 0; i < s->m_paletteCount; ++i) del(s->m_palettes[i]);
    del(s->m_palettes);
    for (int i = 0; i < s->m_animCount; ++i) {
        if (Anim* a = s->m_anims[i]) {
            destructAnim(a);
            delete a;
        }
    }
    del(s->m_anims);
}

void DestructSpriteInGame(Sprite* s)
{
    DestructSprite(s, [](auto* p) { delete[] p; }, [](Anim* a) { a->Destruct(); });
}

void ReleaseInstance(Instance* i)
{
    --i->m_sprite->m_refCount;
    DestructInstance(i, [](auto* p) { delete[] p; });
    delete i;
}
// both operands are floats, so the sum is exact for any realistic game time.
template<class Unload> void SweepStale(Bank* b, float now, int& cursor, Unload unload)
{
    for (int swept = 0; cursor < b->m_spriteCount && swept < 10; ++swept, ++cursor) {
        Sprite* s = b->m_sprites[cursor];
        for (int i = 0; i < s->m_animCount; ++i) {
            Anim* a = s->m_anims[i];
            if (a->m_loaded && static_cast<double>(a->m_lastUsedTime) + 5.0 < static_cast<double>(now)) unload(a);
        }
    }
    if (cursor == b->m_spriteCount) cursor = 0;
}

void UnloadStale(Bank* b, float now)
{
    SweepStale(b, now, StaleCursor(), [](Anim* a) { a->Unload(); });
}

uint32_t Hash(uint32_t value, const void* data, size_t size)
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) value = (value ^ p[i]) * 16777619u;
    return value;
}

uint32_t BankHash(const Bank& b)
{
    uint32_t h = Hash(2166136261u, &b.m_spriteCount, 4);
    for (int i = 0; i < b.m_spriteCount; ++i) {
        const Sprite& s = *b.m_sprites[i];
        h = Hash(h, s.m_name, std::strlen(s.m_name) + 1);
        h = Hash(h, &s.m_refCount, 4);
        h = Hash(h, &s.m_type, 4);
        h = Hash(h, &s.m_paletteModifierId, 4);
        if (s.m_type == 5) h = Hash(h, s.m_buildingExtra, 4);
        h = Hash(h, &s.m_paletteCount, 4);
        for (int p = 0; p < s.m_paletteCount; ++p) h = Hash(h, s.m_palettes[p], sprite_pack::PaletteSize(s.m_type) * 2);
        h = Hash(h, &s.m_animCount, 4);
        for (int a = 0; a < s.m_animCount; ++a) {
            const auto& anim = *s.m_anims[a];
            h = Hash(h, anim.m_name, std::strlen(anim.m_name) + 1);
            h = Hash(h, &anim.m_frames, 2);
            h = Hash(h, &anim.m_directions, 1);
            h = Hash(h, &anim.m_width, 8);
            h = Hash(h, &anim.m_dataSize, 4);
            h = Hash(h, &anim.m_uniqueFrames, 2);
        }
    }
    return h;
}

void FreeBank(Bank& b)
{
    for (int i = 0; i < b.m_spriteCount; ++i) {
        DestructSpriteInGame(b.m_sprites[i]);
        delete b.m_sprites[i];
    }
    delete[] b.m_sprites;
}

void Drive(int)
{
    int saved555 = game::GetRenderer()->m_rgb555;
    unsigned assertions = 0, instances = 0, animations = 0, cachePairs = 0, sprites = 0;
    auto check = [&](bool same) {
        ++assertions;
        if (!same) Failure("driver assertion failed");
    };
    // The incremental sweep keeps its position across calls in the context and wraps at the bank end.
    const int savedCursor = game::GetContext().spriteStaleCursor;
    Sprite empty[12]{};
    Sprite* pointers[12];
    for (int i = 0; i < 12; ++i) pointers[i] = &empty[i];
    Bank sweep{};
    sweep.m_sprites = pointers;
    sweep.m_spriteCount = 12;
    game::GetContext().spriteStaleCursor = 0;
    UnloadStale(&sweep, 0);
    check(game::GetContext().spriteStaleCursor == 10);
    UnloadStale(&sweep, 0);
    check(game::GetContext().spriteStaleCursor == 0);
    game::GetContext().spriteStaleCursor = savedCursor;
    uint32_t hashes[2] = {};
    for (int is555 = 0; is555 < 2; ++is555) {
        game::GetRenderer()->m_rgb555 = is555;
        Bank b{};
        check(Load(&b, sprite_pack::ReadMode::Palettes) == 0);
        check(b.m_spriteCount > 0 && !b.m_headerFile && b.m_datFile == -1);
        hashes[is555] = BankHash(b);
        sprites += b.m_spriteCount;
        bool cachedWhole = false, cachedDirectional = false;
        for (int s = 0; s < b.m_spriteCount; ++s) {
            Sprite* sprite = b.m_sprites[s];
            animations += sprite->m_animCount;
            std::string name = sprite->m_name;
            std::transform(name.begin(), name.end(), name.begin(), [](char c) { return ascii::Lower(c); });
            Instance* a = (static_cast<ui::SpriteBank*>(&b))->CreateInstance(sprite->m_name);
            Instance* copy = (static_cast<ui::SpriteBank*>(&b))->CreateObservedInstance(name.c_str());
            check(a != copy && a->m_name != copy->m_name && a->m_sprite == sprite && copy->m_sprite == sprite);
            check(std::strcmp(copy->m_name, name.c_str()) == 0 && sprite->m_refCount == 2);
            check(a->m_fps == 0x12 && !a->m_frameIndex && !a->m_direction);
            for (int n = 0; n < sprite->m_animCount; ++n)
                check(!sprite->m_anims[n]->m_loaded && !sprite->m_anims[n]->m_frameIndices && !sprite->m_anims[n]->m_frameOffsets &&
                        !sprite->m_anims[n]->m_allData);
            ui::SpriteAnim* cached = nullptr;
            if (sprite->m_animCount) {
                auto* anim = sprite->m_anims[0];
                bool wholeCache = anim->m_dataSize < 0x1f400;
                if (wholeCache ? !cachedWhole : !cachedDirectional) {
                    auto frame = [](ui::SpriteAnim* animation, int index, int direction) { return animation->GetFrame(index, direction); };
                    uint8_t* pixels = frame(a->m_sprite->m_anims[0], 0, 0);
                    check(frame(copy->m_sprite->m_anims[0], 0, 0) == pixels);
                    check(anim->m_loaded && anim->m_allLoaded == static_cast<uint8_t>(wholeCache));
                    cached = anim;
                    (wholeCache ? cachedWhole : cachedDirectional) = true;
                    ++cachePairs;
                }
            }
            Release(&b, a);
            check(sprite->m_refCount == 1);
            Release(&b, copy);
            check(sprite->m_refCount == 0);
            if (cached) {
                check(cached->m_loaded != 0);
                Instance* again = (static_cast<ui::SpriteBank*>(&b))->CreateInstance(sprite->m_name);
                check(again->m_sprite->m_anims[0] == cached && cached->m_loaded);
                Release(&b, again);
                ++instances;
                cached->Unload();
                check(!cached->m_loaded);
            }
            instances += 2;
        }
        for (const char* name : {"__missing_sprite__", "", "DuMmY"}) {
            Instance* i = (static_cast<ui::SpriteBank*>(&b))->CreateInstance(name);
            check(ascii::EqualsNoCase(i->m_sprite->m_name, "dummy") && i->m_sprite->m_refCount == 1);
            check(std::strcmp(i->m_name, *name && ascii::EqualsNoCase(name, "dummy") ? name : "dummy") == 0);
            Release(&b, i);
            ++instances;
        }
        // A second bank reads the same pack into the same state, apart from the registry index of each animation.
        Bank second{};
        check(Load(&second, sprite_pack::ReadMode::Palettes) == 0 && second.m_spriteCount == b.m_spriteCount);
        FreeBank(second);
        FreeBank(b);
    }
    // Only the palettes depend on the renderer format, and they differ between the two.
    check(hashes[0] != hashes[1]);
    game::GetRenderer()->m_rgb555 = saved555;
    OBL_LOG("autotest: sprite-bank-drive sprites=%u anims=%u instances=%u cache-pairs=%u assertions=%u", sprites, animations, instances,
            cachePairs, assertions);
}
} // namespace

ui::SpriteBank* GetSpriteBank()
{
    return game::GetContext().spriteBank;
}
void SetSpriteBank(ui::SpriteBank* bank)
{
    game::GetContext().spriteBank = bank;
}

int SpriteBank::Load()
{
    g_calls.Count();
    return ::obl::ui::Load(this, config::Get().headless ? sprite_pack::ReadMode::Metadata : sprite_pack::ReadMode::Palettes);
}

ui::SpriteInstance* SpriteBank::CreateInstance(const char* name)
{
    g_calls.Count();
    Instance* result = ::obl::ui::Create(static_cast<Bank*>(this), name);
    NotifySpriteCreate(false, name, result);
    return result;
}

ui::SpriteInstance* SpriteBank::CreateObservedInstance(const char* name)
{
    g_calls.Count();
    Instance* result = ::obl::ui::Create(static_cast<Bank*>(this), name);
    NotifySpriteCreate(true, name, result);
    return result;
}

void SpriteBank::ReleaseInstance(void* instance)
{
    g_calls.Count();
    ::obl::ui::ReleaseInstance(static_cast<Instance*>(instance));
}

void SpriteBank::UnloadStale(float now)
{
    g_calls.Count();
    ::obl::ui::UnloadStale(static_cast<Bank*>(this), now);
}

void Sprite::Construct()
{
    g_calls.Count();
    ConstructSprite(this);
}

void Sprite::Destruct()
{
    g_calls.Count();
    DestructSpriteInGame(this);
}

bool ConfigureSpriteBank(const std::filesystem::path& gameDirectory)
{
    g_spritesDirectory = gameDirectory / pack_path::Selected() / "sprites";
    g_calls.SetDriver(Drive);
    return true;
}
} // namespace obl::ui
