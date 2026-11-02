#pragma once

namespace obl::ui {

// Registers the bitmap font test driver.
bool RegisterBitmapFontTestDriver();

// Construct the native bitmap font and record its code page.
void* ConstructBitmapFont(void* font, const char* name);
void* ConstructGdiFont(void* font, const char* faceName, int pointSize, unsigned flags);

} // namespace obl::ui
