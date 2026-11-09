// The settings window's view (src/launcher/settings_view.h): it speaks the language selected in it, picks a window size
// from the display scale and draws without a display. OBL_SETTINGS_IMAGES=<dir> also writes each language's image as PNG
// at 1x, 1.5x and 2x.

#include "unit_test.h"

#include "launcher/settings_view.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <string>

using namespace obl;
namespace view = obl::launcher::settings_view;
namespace settings = obl::game::start_settings;

namespace {

settings::Values SixLanguages(unsigned language)
{
    settings::Values values;
    values.languages = {0, 1, 2, 3, 4, 5};
    values.language = language;
    return values;
}

bool SamePixels(const SDL_Surface* a, const SDL_Surface* b)
{
    if (a->w != b->w || a->h != b->h) return false;
    for (int y = 0; y < a->h; ++y)
        if (std::memcmp(static_cast<const Uint8*>(a->pixels) + y * a->pitch, static_cast<const Uint8*>(b->pixels) + y * b->pitch,
                    static_cast<size_t>(a->w) * 4) != 0)
            return false;
    return true;
}

} // namespace

OBL_TEST_CASE("settings view: the window speaks the language selected in it")
{
    view::Model model(SixLanguages(0));
    OBL_CHECK_EQ(std::string(model.Text(view::Language).label), std::string("Language"));
    OBL_CHECK_EQ(model.Text(view::Language).value, std::string("English"));
    model.Change(view::Language, -1);
    OBL_CHECK_EQ(model.Values().language, 5u);
    OBL_CHECK_EQ(std::string(model.Text(view::Language).label), std::string("Jezyk"));
    OBL_CHECK_EQ(model.Text(view::Language).value, std::string("Polski"));
    OBL_CHECK_EQ(model.Text(view::Display).value, std::string("Okno"));
    model.Change(view::Language, -1);
    OBL_CHECK_EQ(std::string(model.Text(view::Display).label), std::string("Pantalla"));
    OBL_CHECK_EQ(model.Text(view::FrameCap).value, std::string("60 fps"));
}

OBL_TEST_CASE("settings view: a copy with one language speaks it, a copy without a table English")
{
    settings::Values german;
    german.languages = {3};
    german.language = 3;
    const view::Model one(german);
    OBL_CHECK_EQ(std::string(one.Text(view::Language).label), std::string("Sprache"));
    OBL_CHECK(!one.Text(view::Language).enabled);

    settings::Values none;
    none.language = 3;
    const view::Model empty(none);
    OBL_CHECK_EQ(std::string(empty.Text(view::Language).label), std::string("Language"));
    OBL_CHECK_EQ(empty.Text(view::Language).value, std::string("-"));
}

OBL_TEST_CASE("settings view: the window is one and a half times the desktop scale and fits the screen")
{
    OBL_CHECK_EQ(view::WindowScale(1.0f, 1920, 1040), 1.5f);
    OBL_CHECK_EQ(view::WindowScale(1.25f, 1920, 1040), 2.0f);
    OBL_CHECK_EQ(view::WindowScale(1.5f, 2560, 1400), 2.5f);
    OBL_CHECK_EQ(view::WindowScale(2.0f, 3840, 2100), 3.0f);
    OBL_CHECK_EQ(view::WindowScale(2.0f, 1920, 1040), 2.5f); // 3x would not fit
    OBL_CHECK_EQ(view::WindowScale(1.0f, 640, 480), 1.0f);
    OBL_CHECK_EQ(view::WindowScale(0.0f, 0, 0), 1.5f);
}

OBL_TEST_CASE("settings view: each language draws its own window")
{
    // The Linux unit build compiles SDL without its renderer (tests/unit/linux/CMakeLists.txt); Windows draws.
    if (SDL_GetNumRenderDrivers() <= 0) OBL_SKIP("SDL is built without its renderer");
    SDL_Surface* english = view::RenderImage(view::Model(SixLanguages(0)), view::kPlayFocus, 1.0f);
    OBL_REQUIRE(english != nullptr);
    OBL_CHECK_EQ(english->w, view::kWidth);
    OBL_CHECK_EQ(english->h, view::kHeight);
    const char* dir = std::getenv("OBL_SETTINGS_IMAGES");
    for (unsigned language = 1; language < 6; ++language) {
        SDL_Surface* other = view::RenderImage(view::Model(SixLanguages(language)), view::kPlayFocus, 1.0f);
        OBL_REQUIRE(other != nullptr);
        OBL_CHECK(!SamePixels(english, other));
        SDL_DestroySurface(other);
    }
    SDL_Surface* between = view::RenderImage(view::Model(SixLanguages(0)), view::kPlayFocus, 1.5f);
    OBL_REQUIRE(between != nullptr);
    OBL_CHECK_EQ(between->w, 648);
    OBL_CHECK_EQ(between->h, 474);
    SDL_DestroySurface(between);
    if (dir && *dir) {
        for (unsigned language = 0; language < 6; ++language) {
            for (const float scale : {1.0f, 1.5f, 2.0f}) {
                SDL_Surface* image = view::RenderImage(view::Model(SixLanguages(language)), view::kPlayFocus, scale);
                OBL_REQUIRE(image != nullptr);
                const std::string path = std::string(dir) + "/settings_" + std::to_string(language) + "_" +
                        std::to_string(static_cast<int>(scale * 10)) + ".png";
                OBL_CHECK(SDL_SavePNG(image, path.c_str()));
                SDL_DestroySurface(image);
            }
        }
    }
    SDL_DestroySurface(english);
}
