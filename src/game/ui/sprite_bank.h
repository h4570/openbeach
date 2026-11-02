#pragma once

#include "game/ui/sprite.h"

#include <filesystem>

namespace obl::ui {
ui::SpriteBank* GetSpriteBank();
void SetSpriteBank(ui::SpriteBank* bank);
// The bank reads the converted sprites in `<gameDirectory>/packs/base/sprites/` (sprite_pack.h).
bool ConfigureSpriteBank(const std::filesystem::path& gameDirectory);
} // namespace obl::ui
