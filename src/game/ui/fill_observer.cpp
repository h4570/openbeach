#include "game/ui/fill_observer.h"

namespace obl::ui {
namespace {
const FillObserver* g_observer = nullptr;
}
void SetFillObserver(const FillObserver* observer)
{
    g_observer = observer;
}
const FillObserver* GetFillObserver()
{
    return g_observer;
}
void NotifyFill(const void* renderer, bool xywh, const int* rect, uint16_t color)
{
    if (g_observer && g_observer->fill) g_observer->fill(renderer, xywh, rect, color);
}
} // namespace obl::ui
