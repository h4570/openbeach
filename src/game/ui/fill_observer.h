#pragma once
#include <cstdint>

namespace obl::ui {
struct FillObserver {
    void (*fill)(const void* renderer, bool xywh, const int* rect, uint16_t color);
};
void SetFillObserver(const FillObserver* observer);
const FillObserver* GetFillObserver();
void NotifyFill(const void* renderer, bool xywh, const int* rect, uint16_t color);
// ui::renderer::FillRect / FillRectBySize call NotifyFill; they live in renderer_helpers.cpp.
} // namespace obl::ui
