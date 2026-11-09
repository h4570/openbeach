#pragma once

// What the settings window shows and where (docs/build/settings-window.md): the rows and their values, the drawing and the
// hit test, apart from the window and its event loop (settings_window.cpp), so a test can draw it without a display.

#include "game/game/start_settings.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace obl::launcher::settings_view {

// The layout in units of the built-in 8x8 font; LayoutTexture and Present scale it to the window.
constexpr int kWidth = 432;
constexpr int kHeight = 316;

enum Row { Language, Display, Size, Mouse, FrameCap, Movie, ShowAtStart, kRowCount };
// Keyboard focus: a row, then the two buttons.
constexpr int kQuitFocus = kRowCount;
constexpr int kPlayFocus = kRowCount + 1;

struct RowText {
    const char* label;
    const char* hint;
    std::string value;
    bool enabled;
};

// The settings as the window edits them.
class Model {
public:
    explicit Model(game::start_settings::Values values);

    const game::start_settings::Values& Values() const { return m_values; }
    // The game language the window speaks: the one selected in it.
    unsigned TextLanguage() const;
    RowText Text(int row) const;
    void Change(int row, int delta);

private:
    game::start_settings::Values m_values;
    std::vector<double> m_scales;
    std::vector<int> m_frameCaps;
};

// Draws the window into the renderer's 432x316 area at its current render scale.
void Draw(SDL_Renderer* renderer, const Model& model, int focus);

// The texture the layout is drawn into at a whole multiple, the window scale rounded up: the 8x8 font stays crisp there, and
// Present scales it down with linear filtering, so a window at 1.5x shows even letters instead of uneven pixel columns.
SDL_Texture* LayoutTexture(SDL_Renderer* renderer, float scale);
// Draws the window into `layout`, then `layout` into the renderer's logical 432x316 area; the caller presents.
void Present(SDL_Renderer* renderer, SDL_Texture* layout, const Model& model, int focus);

// The row or button under a point of the layout, -1 for none.
int HitTest(float x, float y);
// The value box of a row: its left half steps back.
SDL_FRect ValueBox(int row);
bool Inside(const SDL_FRect& rect, float x, float y);

// The window's scale of the layout for a display, in steps of 0.5: one and a half times the desktop scale (1.5x at 100 %,
// 3x at 200 %), smaller when the window would not fit in nine tenths of the usable area, at least 1x.
float WindowScale(float contentScale, int usableWidth, int usableHeight);

// The window drawn at `scale` with the software renderer, the same way as on screen; nullptr on failure. The caller
// destroys it.
SDL_Surface* RenderImage(const Model& model, int focus, float scale);

} // namespace obl::launcher::settings_view
