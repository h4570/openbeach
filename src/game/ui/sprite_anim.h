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
} // namespace obl::ui
