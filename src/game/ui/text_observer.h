#pragma once

#include "core/char16.h"

// A tap on the native text draw API for GUI tests, the text twin of sprite_observer.h.
// Wrapped text reports the caller's whole string once; internal line draws are not reported.
// Costs one null test per call when nobody listens.
namespace obl::ui {

struct TextObserver {
    // Arguments of the call as the caller made it (text before the code-page conversion). The renderer object already holds the
    // rectangle, colour, outline and alignment the draw will use: the setters only store.
    void (*drawWrapped)(const void* renderer, int x, int y, const Char16* text);
    void (*drawLine)(const void* renderer, int x, int y, const Char16* text);
};

void SetTextObserver(const TextObserver* observer);
const TextObserver* GetTextObserver();

void NotifyTextDrawWrapped(const void* renderer, int x, int y, const Char16* text);
void NotifyTextDrawLine(const void* renderer, int x, int y, const Char16* text);

// Held around DrawWrapped so nested line draws stay unreported.
class TextDrawWrappedScope {
public:
    TextDrawWrappedScope();
    ~TextDrawWrappedScope();
    TextDrawWrappedScope(const TextDrawWrappedScope&) = delete;
    TextDrawWrappedScope& operator=(const TextDrawWrappedScope&) = delete;
};

} // namespace obl::ui
