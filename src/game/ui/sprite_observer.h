#pragma once

#include "game/ui/sprite.h"

#include <cstdint>

namespace obl::ui {

struct SpriteObserver {
    // `viaBank`: the instance came from the sprite bank rather than a direct sprite creation. `name` is what the caller asked for.
    void (*create)(bool viaBank, const char* name, ui::SpriteInstance* instance);
    void (*drawClipped)(ui::SpriteInstance* instance, int x, int y, uint32_t depth, float time);
    void (*draw)(ui::SpriteInstance* instance, int x, int y, int left, int top, int right, int bottom, uint32_t depth);
};

void SetSpriteObserver(const SpriteObserver* observer);
// The one installed now (null when none), so a second tap can pass events on and put the first back (test/gui_capture).
const SpriteObserver* GetSpriteObserver();

void NotifySpriteCreate(bool viaBank, const char* name, ui::SpriteInstance* instance);
void NotifySpriteDrawClipped(ui::SpriteInstance* instance, int x, int y, uint32_t depth, float time);
void NotifySpriteDraw(ui::SpriteInstance* instance, int x, int y, int left, int top, int right, int bottom, uint32_t depth);

// SpriteInstance::DrawClipped ends in SpriteInstance::Draw; that inner draw is not reported.
class SpriteDrawClippedScope {
public:
    SpriteDrawClippedScope();
    ~SpriteDrawClippedScope();
    SpriteDrawClippedScope(const SpriteDrawClippedScope&) = delete;
    SpriteDrawClippedScope& operator=(const SpriteDrawClippedScope&) = delete;
};

} // namespace obl::ui
