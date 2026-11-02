#pragma once

#include "core/image_decode.h"

namespace obl::ui::images {

bool RegisterDrivers();

void* LoadTiff(const char* path);
void* LoadTga(const char* path);
void BuildPalette16(void* image, bool is555);
// Destructs and deletes a LoadTiff/LoadTga image (`new GameImage`); null is ignored.
void Free(void* image);

image::Image Read(const void* object);

} // namespace obl::ui::images

namespace obl::ui::images {

void* Construct(void* image, int width, int height, int bpp, char hasPalette);
void* Construct16(void* image, int width, int height);
void Destruct(void* image);
void ReleaseSlot(void** slot);

} // namespace obl::ui::images
