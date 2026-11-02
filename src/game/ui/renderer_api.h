#pragma once

#include <cstdint>
#include "game/ui/renderer_types.h"
#include "core/rect.h"

namespace obl::ui::renderer {
// Opaque backend handles preserve layout-based ownership while callers move to native APIs.
void DownscaleSurface(void* r, int source, int destination);
void CaptureRectToJpeg(void* r, const void* rect);
void CaptureSurfaceToJpeg(void* r, int surface, const char* path);
int CaptureRect(void* r, const void* rect);
void Present(void* r);
bool IsModeSupported(void* r, int width);
bool ChangeResolution(void* r, int width, int height);
void PresentWorld(void* r);
void DrawImage(void* r, const int* pos, const int* rect, const void* image, int mode, uint32_t flags, uint32_t mask);
void DrawLoadingBar(void* r, float progress);
void InitPixelFormatGlobals(void* scene, int width, int format);
void SetViewport(void* renderer, const int* rect);
void SetGuiBarHeight(void* renderer, int height);
void SetZoomLevel(void* renderer, int level);
int GetZoomLevel();
void ClearLightBuffer(void* renderer);
uint16_t PackColor(void* renderer, uint32_t r, uint32_t g, uint32_t b);
void PutPixel(void* renderer, int x, int y, uint32_t color);
void FillRect(void* renderer, const int* rect, uint32_t color);
void FillRectBySize(void* renderer, const int* rect, uint32_t color);
char ClipRect(void* renderer, int* x, int* y, int* left, int* top, int* right, int* bottom);
void AddDirtyRect(void* renderer, int left, int top, int right, int bottom);
bool PopDirtyRect(void* renderer, screen::Rect* out);
void ApplyFade(uint16_t* pixels, int width, int rows, uint32_t mode);
uint16_t* GetBackBufferBits(void* renderer);
int CreateSurface(void* renderer, int width, int height, int flags);
void AllocSurface(void* renderer, int width, int height, void* record, int flags);
void ReleaseSurface(void* renderer, int index);
bool ResizeSurface(void* renderer, int index, int width, int height);
void RecreateSurface(void* renderer, int index);
void RestoreLostSurfaces(void* renderer);
void SetActiveSurface(void* renderer, int index);
uint32_t GetSurfacePitch(void* renderer);
void UnlockSurface(void* renderer);
void GetSurfaceOrigin(void* renderer, int* origin);
void SetSurfaceOrigin(void* renderer, int index, int x, int y);
bool IsSurfaceLost(void* renderer, int index);
void RestoreSurface(void* renderer, int index);
void ClearSurface(void* renderer, int index);
void BlitSurface(void* renderer, int x, int y, int source);
void BlitSurfaceKeyed(void* renderer, int x, int y, int destination, int source);
void BlitSurfaceRect(void* renderer, const int* dstXY, const int* rect, int source);
uint32_t LoadWaterAnim(void* renderer, const char* path);
void UpdateWaterAnim(void* renderer, float seconds);
void SetSeaEffects(void* renderer, uint32_t enabled);
void DrawWaterBlock(void* renderer, int column, int row, int* pos, uint32_t* rect, void* image, int unused1, int unused2, int useMask);
bool CreateDisplay(void* renderer, void* window, bool windowed, int width, int height, int bpp);
} // namespace obl::ui::renderer
