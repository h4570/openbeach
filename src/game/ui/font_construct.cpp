#include "core/pointer_width.h"
#include "game/ui/image.h"
#include "core/pack_path.h"
#include "game/ui/font_construct.h"
#include "game/ui/text_encoding.h"

#include "game/ui/font_pack.h"
#include "game/game/game_accessors.h"
#include "game/ui/font.h"
#include "game/ui/renderer_types.h"
#include "core/call_counter.h"
#include "game/game/diagnostics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace obl::ui {
namespace {

using ui::BitmapFont;
using ui::GdiFont;
using ui::RendererImage;

test::CallCounter g_bitmapCounter("ui.bitmap-font-construct");
test::CallCounter g_gdiCounter("ui.gdi-font-construct");

void FatalErrorf(const char* format, const char* argument)
{
    game::FatalError(format, argument);
}

void FreeGameImage(RendererImage* image)
{
    if (!image) return;
    ui::images::Destruct(image);
    delete image;
}

// ---- BitmapFont ---------------------------------------------------------------------------------------------------------

// Converted metrics and atlas are the only source of bitmap fonts.
void* BuildBitmapFont(BitmapFont* font, const char* name)
{
    font_pack::Font loaded;
    std::string error;
    if (!font_pack::ReadFont(pack_path::Selected(), name, loaded, error)) {
        FatalErrorf("Font: %s", error.c_str());
        return font;
    }
    font->m_size = loaded.size;
    font->m_bold = loaded.bold;
    std::memcpy(font->m_glyphs, loaded.glyphs.data(), sizeof(font->m_glyphs));
    std::memcpy(font->m_kerning, loaded.kerning.data(), sizeof(font->m_kerning));
    font->m_image = static_cast<RendererImage*>(ui::images::Construct(new RendererImage, loaded.width, loaded.height, 8, 1));
    std::memcpy(font->m_image->m_pixels, loaded.pixels.data(), loaded.pixels.size());
    return font;
}

// ---- GdiFont ------------------------------------------------------------------------------------------------------------

// The atlas side is square: 0x100 up to 20 pt, 0x400 above 40 pt. The 21..40 pt range is a square 0x200 atlas.
int AtlasSize(int pointSize)
{
    return static_cast<unsigned>(pointSize) < 0x29 ? (static_cast<unsigned>(pointSize) < 0x15 ? 0x100 : 0x200) : 0x400;
}

// No system font is rasterized on any host: the debug font only draws debug text, which nothing renders. It keeps a blank
// atlas with zero glyph rectangles and no kerning, so any measured text is zero wide.
void* BuildGdiFont(GdiFont* font, int pointSize, unsigned flags)
{
    font->m_size = pointSize;
    font->m_flags = flags;
    const int side = AtlasSize(pointSize);
    font->m_image = static_cast<RendererImage*>(ui::images::Construct(new RendererImage, side, side, 8, 1));
    std::memset(font->m_glyphs, 0, sizeof(font->m_glyphs));
    std::memset(font->m_kerning, 0, sizeof(font->m_kerning));
    return font;
}

// Exercise every converted bitmap font through the engine constructor.
void DriveBitmap(int count)
{
    // The fonts of every layer (mod/ and the pack), so a mod font is driven like a converted one.
    const std::vector<std::string> names = pack_path::ListStems(pack_path::Selected(), "fonts", ".json");
    if (names.empty()) return;
    auto construct = &ConstructBitmapFont;
    for (int i = 0; i < count; ++i) {
        const std::string& name = names[i % names.size()];
        auto* font = static_cast<BitmapFont*>(std::calloc(1, sizeof(BitmapFont)));
        construct(font, name.c_str());
        FreeGameImage(font->m_image);
        std::free(font);
    }
}

} // namespace

void* ConstructBitmapFont(void* font, const char* name)
{
    g_bitmapCounter.Count();
    void* result = BuildBitmapFont(static_cast<BitmapFont*>(font), name);
    ui::TrackBitmapFont(font, name);
    return result;
}

void* ConstructGdiFont(void* font, const char*, int pointSize, unsigned flags)
{
    g_gdiCounter.Count();
    return BuildGdiFont(static_cast<GdiFont*>(font), pointSize, flags);
}

bool RegisterBitmapFontTestDriver()
{
    g_bitmapCounter.SetDriver(&DriveBitmap);
    return true;
}

} // namespace obl::ui
