#pragma once

#include "game/ui/map_view.h"
#include "game/ui/map_math.h"

#include <cstdint>

namespace obl::map_camera {

struct Screen {
    uint32_t width;
    uint32_t height;
    int zoomLevel;
    float zoomScale;
    int guiBarHeight;
};

double SmoothStep(float t);

void Clamp(ui::MapView& view, int zoomLevel);

void AddVelocity(ui::MapView& view, float x, float y);

void UpdateTween(ui::MapView& view, float gameTime);

void SetPosition(ui::MapView& view, int zoomLevel, int x, int y);

bool ScrollTo(ui::MapView& view, const Screen& screen, const map_math::Terrain& terrain, float x, float y, float seconds, float gameTime);

struct Frame {
    bool redraw;
    bool listener;
    int centre[2];
};
Frame Update(ui::MapView& view, const Screen& screen, float gameTime);

void EndFrame(ui::MapView& view);

void Stop(ui::MapView& view);
void Lock(ui::MapView& view);
void Unlock(ui::MapView& view);

void SetViewportSize(ui::MapView& view, const Screen& screen, int width, int height);

void ResetViewport(ui::MapView& view, const Screen& screen);

} // namespace obl::map_camera
