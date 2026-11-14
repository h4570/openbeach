#pragma once

#include "game/ui/sprite.h"
#include "formats/sprite_pack.h"

#include <filesystem>

namespace obl::ui {
// The animation loaders read the frames from `<gameDirectory>/packs/base/sprites/` (sprite_pack.h).
bool ConfigureSpriteAnimations(const std::filesystem::path& gameDirectory);
// Fills the header fields of `anim` (constructed, in the game heap) from the converted animation its frames come from.
// Metadata-only animations provide transparent frames for presentation calls that still run headless.
void AttachPackAnim(ui::SpriteAnim* anim, sprite_pack::Anim pack, bool metadataOnly = false);

// Background decoding of animation frames, for hosts whose CPU takes a visible pause to decode a sheet (the handheld build; off
// elsewhere). Once enabled, a worker thread decodes whole animations ahead of time and GetFrame takes them; the frames stay byte for
// byte the same, only the moment they are decoded changes.
void EnableSpritePrefetch();
bool SpritePrefetchEnabled();
// Asks the worker for every direction of `anim` not loaded yet (an object about to scroll into view).
void PrefetchSpriteAnim(const ui::SpriteAnim* anim);
} // namespace obl::ui
