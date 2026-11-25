#include "game/mods/overlay_draw.h"

#include "core/code_page.h"
#include "core/log.h"
#include "core/pack_path.h"
#include "core/png_read.h"
#include "game/ui/font.h"
#include "game/ui/font_pack.h"
#include "game/ui/image.h"
#include "game/ui/renderer_api.h"
#include "game/ui/renderer_ops.h"
#include "game/ui/renderer_types.h"
#include "game/ui/text_encoding.h"
#include "game/ui/text_renderer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

namespace obl::mods {

namespace {

// The largest overlay image: a mod's icon or panel, not a picture of the screen.
constexpr int kMaxImageSide = 1024;

struct OverlayFont {
    ui::BitmapFont font{};
    int height = 0; // the tallest glyph
    ~OverlayFont()
    {
        if (!font.m_image) return;
        ui::images::Destruct(font.m_image);
        delete font.m_image;
    }
};

struct OverlayImage {
    int width = 0, height = 0;
    std::vector<uint32_t> argb;
};

// Null entries remember a font or image that failed, so it is not read again every frame.
std::map<std::string, std::unique_ptr<OverlayFont>> g_fonts;
std::map<std::filesystem::path, std::unique_ptr<OverlayImage>> g_images;

OverlayFont* FindFont(const std::string& name)
{
    if (const auto it = g_fonts.find(name); it != g_fonts.end()) return it->second.get();
    auto& slot = g_fonts[name];
    font_pack::Font loaded;
    std::string error;
    // Read directly instead of ConstructBitmapFont, which ends the game on a missing font: a script's typo is its own error.
    if (!font_pack::ReadFont(pack_path::Selected(), name, loaded, error)) return nullptr;
    auto font = std::make_unique<OverlayFont>();
    font->font.m_size = loaded.size;
    font->font.m_bold = loaded.bold;
    std::memcpy(font->font.m_glyphs, loaded.glyphs.data(), sizeof(font->font.m_glyphs));
    std::memcpy(font->font.m_kerning, loaded.kerning.data(), sizeof(font->font.m_kerning));
    font->font.m_image = static_cast<ui::RendererImage*>(ui::images::Construct(new ui::RendererImage, loaded.width, loaded.height, 8, 1));
    std::memcpy(font->font.m_image->m_pixels, loaded.pixels.data(), loaded.pixels.size());
    for (const ui::GlyphRect& glyph : font->font.m_glyphs) font->height = std::max(font->height, glyph.bottom - glyph.top);
    // The text encoding (Polish fonts take CP1250) follows the font's name.
    ui::TrackBitmapFont(&font->font, name.c_str());
    slot = std::move(font);
    return slot.get();
}

const OverlayImage* FindImage(const std::filesystem::path& path)
{
    if (const auto it = g_images.find(path); it != g_images.end()) return it->second.get();
    auto& slot = g_images[path];
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    png_read::Image png;
    std::string error;
    if (!in.good() && !in.eof()) {
        error = "cannot be read";
    } else if (png_read::Decode(bytes, png, error)) { // a file that is no PNG gets Decode's own reason
        if (png.bitDepth != 8 || (png.channels != 3 && png.channels != 4) || png.indexed)
            error = "is not an 8-bit RGB or RGBA PNG";
        else if (png.width > kMaxImageSide || png.height > kMaxImageSide)
            error = "is larger than " + std::to_string(kMaxImageSide) + " pixels a side";
    }
    if (!error.empty()) {
        OBL_LOG("presentation scripts: overlay image %s %s", path.generic_string().c_str(), error.c_str());
        return nullptr;
    }
    auto image = std::make_unique<OverlayImage>();
    image->width = png.width;
    image->height = png.height;
    image->argb.resize(static_cast<size_t>(png.width) * png.height);
    for (size_t i = 0; i < image->argb.size(); ++i) {
        const uint8_t* p = &png.bytes[i * png.channels];
        const uint32_t alpha = png.channels == 4 ? p[3] : 255;
        image->argb[i] = alpha << 24 | uint32_t{p[0]} << 16 | uint32_t{p[1]} << 8 | p[2];
    }
    slot = std::move(image);
    return slot.get();
}

// The back buffer of one drawing call: locked while it lives.
class Pixels {
public:
    explicit Pixels(ui::Renderer& renderer) : m_renderer(renderer)
    {
        m_bits = ui::renderer::GetBackBufferBits(&renderer);
        if (!m_bits) return;
        m_pitch = ui::SurfaceAt(renderer, renderer.m_activeSurface)->m_pitch / 2;
        m_rgb555 = renderer.m_rgb555 != 0;
    }
    ~Pixels()
    {
        if (m_bits) ui::renderer::UnlockSurface(&m_renderer);
    }
    Pixels(const Pixels&) = delete;
    Pixels& operator=(const Pixels&) = delete;

    bool Valid() const { return m_bits != nullptr; }
    int Width() const { return m_renderer.m_width; }
    int Height() const { return m_renderer.m_height; }
    uint16_t Get(int x, int y) const { return m_bits[static_cast<size_t>(y) * m_pitch + x]; }

    // Blends a 0xAARRGGBB color over the pixel; outside the frame nothing happens.
    void Blend(int x, int y, uint32_t argb)
    {
        if (x < 0 || y < 0 || x >= Width() || y >= Height()) return;
        const uint32_t alpha = argb >> 24;
        if (alpha == 0) return;
        uint16_t& pixel = m_bits[static_cast<size_t>(y) * m_pitch + x];
        uint32_t r = argb >> 16 & 255, g = argb >> 8 & 255, b = argb & 255;
        if (alpha < 255) {
            uint32_t dr, dg, db;
            Unpack(pixel, dr, dg, db);
            r = (r * alpha + dr * (255 - alpha)) / 255;
            g = (g * alpha + dg * (255 - alpha)) / 255;
            b = (b * alpha + db * (255 - alpha)) / 255;
        }
        pixel = renderer_ops::PackColor(m_rgb555, r, g, b);
    }
    void Set(int x, int y, uint16_t value)
    {
        if (x >= 0 && y >= 0 && x < Width() && y < Height()) m_bits[static_cast<size_t>(y) * m_pitch + x] = value;
    }

private:
    // 5 or 6 bits a channel, widened by repeating the top bits (as the PNG writer of `shot` does).
    void Unpack(uint16_t pixel, uint32_t& r, uint32_t& g, uint32_t& b) const
    {
        if (m_rgb555) {
            r = pixel >> 10 & 31;
            g = pixel >> 5 & 31;
            b = pixel & 31;
            g = g << 3 | g >> 2;
        } else {
            r = pixel >> 11 & 31;
            g = pixel >> 5 & 63;
            b = pixel & 31;
            g = g << 2 | g >> 4;
        }
        r = r << 3 | r >> 2;
        b = b << 3 | b >> 2;
    }

    ui::Renderer& m_renderer;
    uint16_t* m_bits = nullptr;
    int m_pitch = 0;
    bool m_rgb555 = true;
};

void FillRect(Pixels& pixels, int x, int y, int w, int h, uint32_t color)
{
    const int left = std::max(x, 0), top = std::max(y, 0);
    const int right = std::min(x + w, pixels.Width()), bottom = std::min(y + h, pixels.Height());
    for (int py = top; py < bottom; ++py)
        for (int px = left; px < right; ++px) pixels.Blend(px, py, color);
}

void DrawFrame(Pixels& pixels, const OverlayOp& op)
{
    if (op.w <= 0 || op.h <= 0) return;
    FillRect(pixels, op.x, op.y, op.w, 1, op.color);
    if (op.h > 1) FillRect(pixels, op.x, op.y + op.h - 1, op.w, 1, op.color);
    FillRect(pixels, op.x, op.y + 1, 1, op.h - 2, op.color);
    if (op.w > 1) FillRect(pixels, op.x + op.w - 1, op.y + 1, 1, op.h - 2, op.color);
}

void DrawImage(Pixels& pixels, const OverlayOp& op)
{
    const OverlayImage* image = FindImage(op.image);
    if (!image) return;
    const int w = op.w > 0 ? op.w : image->width, h = op.h > 0 ? op.h : image->height;
    for (int y = std::max(0, -op.y); y < h && op.y + y < pixels.Height(); ++y)
        for (int x = std::max(0, -op.x); x < w && op.x + x < pixels.Width(); ++x) {
            const int sx = static_cast<int>(static_cast<int64_t>(x) * image->width / w);
            const int sy = static_cast<int>(static_cast<int64_t>(y) * image->height / h);
            pixels.Blend(op.x + x, op.y + y, image->argb[static_cast<size_t>(sy) * image->width + sx]);
        }
}

// The frame as it stood before the overlay, which View copies from (so a magnifier does not magnify itself).
struct FrameCopy {
    int width = 0, height = 0;
    std::vector<uint16_t> pixels;
};

void DrawView(Pixels& pixels, const OverlayOp& op, const FrameCopy& frame)
{
    if (op.w <= 0 || op.h <= 0 || op.sw <= 0 || op.sh <= 0) return;
    for (int y = std::max(0, -op.y); y < op.h && op.y + y < pixels.Height(); ++y) {
        const int sy = op.sy + static_cast<int>(static_cast<int64_t>(y) * op.sh / op.h);
        if (sy < 0 || sy >= frame.height) continue;
        for (int x = std::max(0, -op.x); x < op.w && op.x + x < pixels.Width(); ++x) {
            const int sx = op.sx + static_cast<int>(static_cast<int64_t>(x) * op.sw / op.w);
            if (sx >= 0 && sx < frame.width) pixels.Set(op.x + x, op.y + y, frame.pixels[static_cast<size_t>(sy) * frame.width + sx]);
        }
    }
}

void DrawText(ui::Renderer& renderer, const OverlayOp& op)
{
    if ((op.color >> 24) == 0) return;
    OverlayFont* font = FindFont(op.font);
    if (!font) return;
    std::u16string text = code_page::Utf8ToUtf16(op.text);
    ui::TextRenderer writer{};
    ui::font_text::Construct(&writer, &font->font, nullptr);
    ui::font_text::SetRect(&writer, 0, 0, renderer.m_width, renderer.m_height);
    ui::font_text::SetColor(&writer, op.color & 0xFFFFFFu);
    ui::font_text::DrawLine(&writer, op.x, op.y, text.c_str());
}

} // namespace

void DrawOverlayOps(ui::Renderer& renderer, std::span<const OverlayOp> ops)
{
    if (ops.empty()) return;
    const int savedSurface = renderer.m_activeSurface;
    const uint8_t savedDirect = renderer.m_directMode;
    int savedView[4];
    std::memcpy(savedView, renderer.m_view, sizeof(savedView));
    ui::renderer::SetActiveSurface(&renderer, 0);
    renderer.m_directMode = 1; // the text drawer writes to the back buffer, not the work buffer
    const int full[4] = {0, 0, renderer.m_width, renderer.m_height};
    ui::renderer::SetViewport(&renderer, full);
    FrameCopy frame;
    if (std::any_of(ops.begin(), ops.end(), [](const OverlayOp& op) { return op.kind == OverlayOp::Kind::View; })) {
        Pixels pixels(renderer);
        if (pixels.Valid()) {
            frame.width = pixels.Width();
            frame.height = pixels.Height();
            frame.pixels.resize(static_cast<size_t>(frame.width) * frame.height);
            for (int y = 0; y < frame.height; ++y)
                for (int x = 0; x < frame.width; ++x) frame.pixels[static_cast<size_t>(y) * frame.width + x] = pixels.Get(x, y);
        }
    }
    for (const OverlayOp& op : ops) {
        if (op.kind == OverlayOp::Kind::Text) {
            DrawText(renderer, op);
            continue;
        }
        Pixels pixels(renderer);
        if (!pixels.Valid()) break;
        switch (op.kind) {
        case OverlayOp::Kind::Rect: FillRect(pixels, op.x, op.y, op.w, op.h, op.color); break;
        case OverlayOp::Kind::Frame: DrawFrame(pixels, op); break;
        case OverlayOp::Kind::Image: DrawImage(pixels, op); break;
        case OverlayOp::Kind::View: DrawView(pixels, op, frame); break;
        case OverlayOp::Kind::Text: break;
        }
    }
    ui::renderer::SetViewport(&renderer, savedView);
    renderer.m_directMode = savedDirect;
    ui::renderer::SetActiveSurface(&renderer, savedSurface);
}

std::optional<std::pair<int, int>> MeasureOverlayText(const std::string& text, const std::string& font)
{
    OverlayFont* loaded = FindFont(font);
    if (!loaded) return std::nullopt;
    const std::u16string wide = code_page::Utf8ToUtf16(text);
    ui::TextRenderer writer{};
    writer.m_font = &loaded->font;
    return std::pair<int, int>{ui::font_text::MeasureWidth(&writer, wide.c_str()), loaded->height};
}

void ReleaseOverlayResources()
{
    g_fonts.clear();
    g_images.clear();
}

} // namespace obl::mods
