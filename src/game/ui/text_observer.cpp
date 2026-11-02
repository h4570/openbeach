#include "core/char16.h"
#include "game/ui/text_observer.h"

namespace obl::ui {
namespace {

const TextObserver* g_observer = nullptr;
thread_local int g_wrappedDepth = 0;

} // namespace

void SetTextObserver(const TextObserver* observer)
{
    g_observer = observer;
}

const TextObserver* GetTextObserver()
{
    return g_observer;
}

void NotifyTextDrawWrapped(const void* renderer, int x, int y, const Char16* text)
{
    if (g_observer && g_observer->drawWrapped) g_observer->drawWrapped(renderer, x, y, text);
}

void NotifyTextDrawLine(const void* renderer, int x, int y, const Char16* text)
{
    if (g_observer && g_observer->drawLine && g_wrappedDepth == 0) g_observer->drawLine(renderer, x, y, text);
}

TextDrawWrappedScope::TextDrawWrappedScope()
{
    ++g_wrappedDepth;
}

TextDrawWrappedScope::~TextDrawWrappedScope()
{
    --g_wrappedDepth;
}

} // namespace obl::ui
