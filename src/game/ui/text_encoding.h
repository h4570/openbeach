#pragma once
#include "core/char16.h"
#include <string>
namespace obl::ui {
void TrackBitmapFont(const void* font, const char* name);
const Char16* EncodedText(const void* renderer, const Char16* text, Text16& encoded);
} // namespace obl::ui
