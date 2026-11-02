#include "game/ui/sprite_observer.h"

namespace obl::ui {
namespace {

const SpriteObserver* g_observer = nullptr;
thread_local int g_clippedDepth = 0;

} // namespace

void SetSpriteObserver(const SpriteObserver* observer)
{
    g_observer = observer;
}

const SpriteObserver* GetSpriteObserver()
{
    return g_observer;
}

void NotifySpriteCreate(bool viaBank, const char* name, ui::SpriteInstance* instance)
{
    if (g_observer && g_observer->create) g_observer->create(viaBank, name, instance);
}

void NotifySpriteDrawClipped(ui::SpriteInstance* instance, int x, int y, uint32_t depth, float time)
{
    if (g_observer && g_observer->drawClipped) g_observer->drawClipped(instance, x, y, depth, time);
}

void NotifySpriteDraw(ui::SpriteInstance* instance, int x, int y, int left, int top, int right, int bottom, uint32_t depth)
{
    if (g_observer && g_observer->draw && g_clippedDepth == 0) g_observer->draw(instance, x, y, left, top, right, bottom, depth);
}

SpriteDrawClippedScope::SpriteDrawClippedScope()
{
    ++g_clippedDepth;
}

SpriteDrawClippedScope::~SpriteDrawClippedScope()
{
    --g_clippedDepth;
}

} // namespace obl::ui
