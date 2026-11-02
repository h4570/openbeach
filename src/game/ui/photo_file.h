#pragma once

// Implemented by the platform's 16-bit renderer capture (src/platform/renderer_capture.cpp).
namespace obl::ui {
// Saves surface `surface` as the next free photos/<name>.jpg.
void PhotoAlbumSavePhotoFile(int surface);
} // namespace obl::ui
