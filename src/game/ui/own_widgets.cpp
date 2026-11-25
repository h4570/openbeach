#include "game/ui/own_widgets.h"

#include "game/game/game_accessors.h"
#include "game/ui/blit.h"
#include "game/ui/font.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/gui_controls_types.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_script.h"
#include "game/ui/gui_script_state.h"
#include "game/ui/gui_script_types.h"
#include "game/ui/mods_screen.h"
#include "game/ui/renderer_api.h"
#include "game/ui/renderer_ops.h"
#include "game/ui/renderer_types.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/text_renderer.h"
#include "game/ui/text_table.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace obl::gui::own_widgets {

namespace {

constexpr size_t kTokenBytes = 512;

uint16_t Pack(uint32_t rgb)
{
    return ui::renderer::PackColor(game::GetRenderer(), (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

ui::TextRenderer* Renderer(int font)
{
    ui::GuiManager* gui = game::GetGuiManager();
    if (!gui) return nullptr;
    if (font > 0 && font < 10 && gui->m_textRenderers[font]) return gui->m_textRenderers[font];
    return gui->m_textRenderers[0];
}

void SkipBlanks(ui::GuiScript& s)
{
    while (s.m_text[s.m_cursor] == ' ' || s.m_text[s.m_cursor] == '\t' || s.m_text[s.m_cursor] == '\r' || s.m_text[s.m_cursor] == '\n')
        ++s.m_cursor;
}

// The reader stops before the delimiter that follows the fourth number.
void CloseAfterBounds(ui::GuiScript& s)
{
    SkipBlanks(s);
    if (s.m_text[s.m_cursor] == ')' || s.m_text[s.m_cursor] == ',') ++s.m_cursor;
}

} // namespace

void Fill(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    const int rect[] = {x0, y0, x1, y1};
    ui::renderer::FillRect(game::GetRenderer(), rect, Pack(rgb));
}

void Frame(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    Fill(x0, y0, x1, y0 + 1, rgb);
    Fill(x0, y1 - 1, x1, y1, rgb);
    Fill(x0, y0, x0 + 1, y1, rgb);
    Fill(x1 - 1, y0, x1, y1, rgb);
}

int InkCentre(int font)
{
    ui::GuiManager* gui = game::GetGuiManager();
    const int fallback = LineHeight(font) / 2;
    if (!gui) return fallback;
    const int slot = font > 0 && font < 10 && gui->m_fonts[font] ? font : 0;
    const auto* bitmap = static_cast<const ui::BitmapFont*>(gui->m_fonts[slot]);
    const ui::RendererImage* image = bitmap ? bitmap->m_image : nullptr;
    if (!image || (!image->m_pixels && image->m_pixels16.empty())) return fallback;
    // The ink rows of a capital H inside its glyph cell: the text line is drawn from the cell's top, so the centre of those rows is
    // where the label's visible middle sits (the cell also holds room for descenders and accents).
    const ui::GlyphRect& g = bitmap->m_glyphs['H'];
    if (g.bottom <= g.top || g.right <= g.left || g.bottom > image->m_height || g.right > image->m_width) return fallback;
    const auto pixel = [&](int x, int y) -> uint32_t {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(image->m_width) + static_cast<size_t>(x);
        return image->m_bpp == 16 && !image->m_pixels16.empty() ? image->m_pixels16[i]
                                                                : image->m_pixels[i * static_cast<size_t>(image->m_bytesPerPixel)];
    };
    const uint32_t background = pixel(g.left, g.top);
    int first = -1, last = -1;
    for (int y = g.top; y < g.bottom; ++y)
        for (int x = g.left; x < g.right; ++x)
            if (pixel(x, y) != background) {
                if (first < 0) first = y;
                last = y;
                break;
            }
    return first < 0 ? fallback : first - g.top + (last - first + 1) / 2;
}

int LineHeight(int font)
{
    ui::GuiManager* gui = game::GetGuiManager();
    if (!gui) return 12;
    const int slot = font > 0 && font < 10 && gui->m_fonts[font] ? font : 0;
    const auto* bitmap = static_cast<const ui::BitmapFont*>(gui->m_fonts[slot]);
    return bitmap ? static_cast<int>(bitmap->m_size) + 2 : 12;
}

void Text(int font, int x, int y, int width, const Char16* text, uint32_t rgb, int align)
{
    ui::TextRenderer* renderer = Renderer(font);
    if (!renderer || !text || width <= 0) return;
    size_t length = 0;
    while (text[length]) ++length;
    // Room for the ellipsis the truncation may add.
    std::vector<Char16> line(length + 8, u'\0');
    std::memcpy(line.data(), text, length * sizeof(Char16));
    ui::font_text::TruncateWithEllipsis(renderer, line.data(), width);
    ui::font_text::SetRect(renderer, x, y, width, LineHeight(font));
    ui::font_text::SetColor(renderer, rgb);
    ui::font_text::SetOutlined(renderer, 0);
    ui::font_text::SetAlign(renderer, align);
    ui::font_text::DrawLine(renderer, 0, 0, line.data());
}

void TextWrapped(int font, int x, int y, int width, int height, const Char16* text, uint32_t rgb, int align)
{
    ui::TextRenderer* renderer = Renderer(font);
    if (!renderer || !text || width <= 0 || height <= 0) return;
    ui::font_text::SetRect(renderer, x, y, width, height);
    ui::font_text::SetColor(renderer, rgb);
    ui::font_text::SetOutlined(renderer, 0);
    ui::font_text::SetAlign(renderer, align);
    ui::font_text::DrawWrapped(renderer, 0, 0, text);
}

bool Parse(ui::GuiScript& s, const char* name)
{
    const bool button = std::strcmp(name, "OwnButton") == 0;
    if (!button && std::strcmp(name, "ModsPanel") != 0) return false;
    if (s.m_text[s.m_cursor] != '(') return true;
    ++s.m_cursor;
    SkipBlanks(s);
    gui_script::Bounds b{};
    GuiScriptReadBounds(s, b, s.m_parent);
    ui::GuiWidget* widget = nullptr;
    if (button) {
        SkipBlanks(s);
        if (s.m_text[s.m_cursor] == ',') ++s.m_cursor;
        SkipBlanks(s);
        char text[kTokenBytes], dialog[kTokenBytes];
        GuiScriptReadToken(s, text);
        SkipBlanks(s);
        GuiScriptReadToken(s, dialog);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the dialog's widget tree owns its children (gui_widget_virtual.h)
        widget = new ui::GuiOwnButton(b.x0, b.y0, b.x1, b.y1, text, dialog);
    } else {
        CloseAfterBounds(s);
        widget = mods_screen::CreatePanel(b.x0, b.y0, b.x1, b.y1);
    }
    GuiScriptAddWidget(s, widget, false);
    return true;
}

} // namespace obl::gui::own_widgets

namespace obl::ui {

namespace own = gui::own_widgets;

namespace {

void Unpack16(uint16_t pixel, bool rgb555, int& r, int& g, int& b)
{
    if (rgb555) {
        r = ((pixel >> 10) & 31) * 255 / 31;
        g = ((pixel >> 5) & 31) * 255 / 31;
        b = (pixel & 31) * 255 / 31;
    } else {
        r = ((pixel >> 11) & 31) * 255 / 31;
        g = ((pixel >> 5) & 63) * 255 / 63;
        b = (pixel & 31) * 255 / 31;
    }
}

uint16_t Blend16(uint16_t dst, uint16_t src, int alpha, bool rgb555)
{
    int dr, dg, db, sr, sg, sb;
    Unpack16(dst, rgb555, dr, dg, db);
    Unpack16(src, rgb555, sr, sg, sb);
    const int inv = 255 - alpha;
    const uint32_t r = static_cast<uint32_t>((sr * alpha + dr * inv) / 255);
    const uint32_t g = static_cast<uint32_t>((sg * alpha + dg * inv) / 255);
    const uint32_t b = static_cast<uint32_t>((sb * alpha + db * inv) / 255);
    return renderer_ops::PackColor(rgb555, r, g, b);
}

// Samples the label text color from the centre of a native menu button sprite (the opaque pixel farthest from its clean interior).
uint32_t SampleLabelColor(const uint8_t* frame, const uint16_t* pal, int w, int h, int capW, bool rgb555)
{
    const uint8_t bgIdx = frame[(h / 2) * w + capW];
    int br = 200, bg = 210, bb = 250;
    if (bgIdx != 0) Unpack16(pal[bgIdx], rgb555, br, bg, bb);
    int bestDist = -1;
    uint32_t bestRgb = own::kWhite;
    const int xStart = w / 3, xEnd = (w * 2) / 3;
    const int yStart = h / 4, yEnd = (h * 3) / 4;
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            const uint8_t idx = frame[y * w + x];
            if (idx == 0) continue;
            int r, g, b;
            Unpack16(pal[idx], rgb555, r, g, b);
            const int dr = r - br, dg = g - bg, db = b - bb;
            const int dist = dr * dr + dg * dg + db * db;
            if (dist > bestDist) {
                bestDist = dist;
                bestRgb = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
            }
        }
    }
    return bestRgb;
}

bool IsBorderPixel(const uint8_t* frame, int w, int h, int x, int y)
{
    if (y >= h - 3 || x < 3 || x >= w - 3) return true;
    for (int dy = -2; dy <= 2; ++dy) {
        const int ny = y + dy;
        if (ny < 0 || ny >= h) continue;
        for (int dx = -2; dx <= 2; ++dx) {
            const int nx = x + dx;
            if (nx < 0 || nx >= w || frame[ny * w + nx] == 0) return true;
        }
    }
    return false;
}

// The graphic of the first sprite button of the MainMenu dialog, or null when the pack has none.
const char* FindMenuButtonGraphic()
{
    GuiManager* gui = game::GetGuiManager();
    if (!gui) return nullptr;
    for (const auto* def = gui->m_definitions; def; def = def->m_next) {
        if (!def->m_dialog || std::strcmp(def->m_dialog->m_name, "MainMenu") != 0) continue;
        const auto* sentinel = def->m_dialog->m_children;
        if (!sentinel) return nullptr;
        for (const auto* node = sentinel->m_next; node && node != sentinel; node = node->m_next) {
            if (!node->m_widget || node->m_widget->m_type != static_cast<int32_t>(GuiType::Button)) continue;
            const auto* button = static_cast<const GuiButtonData*>(node->m_widget);
            if (button->m_graphic) return button->m_graphic;
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

GuiOwnButton::GuiOwnButton(int x0, int y0, int x1, int y1, std::string text, std::string dialog)
    : GuiLeaf(x0, y0, x1, y1), m_text(std::move(text)), m_dialog(std::move(dialog))
{
    m_type = gui::mods_screen::kOwnButtonType;
}

GuiOwnButton::~GuiOwnButton()
{
    ReleasePackSprite();
}

void GuiOwnButton::ReleasePackSprite()
{
    if (m_sprite) {
        if (auto* bank = static_cast<SpriteBank*>(GetSpriteBank())) bank->ReleaseInstance(m_sprite);
        m_sprite = nullptr;
    }
    m_spriteResolved = false;
    m_backdrop.clear();
    m_backdropW = m_backdropH = 0;
}

void GuiOwnButton::ResolvePackSprite()
{
    if (m_spriteResolved) return;
    m_spriteResolved = true;
    auto* bank = static_cast<SpriteBank*>(GetSpriteBank());
    if (!bank) return;
    // Adopt the first sibling button sprite of MainMenu so the Mods tab shares the exact pill shape, outline, hover/press frames
    // and label colour of the pack's own main menu buttons without shipping any pack art.
    const char* graphic = FindMenuButtonGraphic();
    if (!graphic) return;
    m_sprite = bank->CreateObservedInstance(graphic);
    if (!m_sprite || !m_sprite->m_sprite || !m_sprite->m_sprite->m_anims) return;
    const SpriteAnim* anim = m_sprite->m_sprite->m_anims[m_sprite->m_animIndex];
    if (anim && anim->m_width > 0 && anim->m_height > 0) {
        // Clip the top fifth so the pill hangs flush from the top screen edge with its rounded bottom corners intact.
        const int clipTop = anim->m_height / 5;
        const int cx = (m_baseX0 + m_baseX1) / 2;
        m_x0 = cx - anim->m_width / 2;
        m_x1 = m_x0 + anim->m_width;
        m_y0 = m_baseY0;
        m_y1 = m_baseY0 + anim->m_height - clipTop;
    }
}

void GuiOwnButton::SetVisible(uint32_t visible)
{
    if (visible)
        ResolvePackSprite();
    else
        ReleasePackSprite();
    m_drawnState = -1;
    VisibleInvalidate(visible);
}

bool GuiOwnButton::HitTest(int x, int y)
{
    const bool hit = HitTestBase(x, y);
    const int state = m_held ? 2 : (hit ? 1 : 0);
    if (state != m_drawnState) m_dirty = 1;
    return hit;
}

void GuiOwnButton::Draw()
{
    if (!m_visible) return;
    ResolvePackSprite();
    const int state = m_held ? 2 : (m_hovered ? 1 : 0);
    if (!m_dirty && state == m_drawnState) return;

    uint32_t labelRgb = own::kWhite;
    bool drewNative = false;
    Renderer* renderer = game::GetRenderer();
    if (renderer && m_sprite && m_sprite->m_sprite && m_sprite->m_sprite->m_anims && m_sprite->m_sprite->m_palettes) {
        SpriteAnim* anim = m_sprite->m_sprite->m_anims[m_sprite->m_animIndex];
        const uint16_t* pal = m_sprite->m_sprite->m_palettes[0];
        if (anim && pal && anim->m_width > 0 && anim->m_height > 0) {
            const int dirs = anim->m_directions > 0 ? anim->m_directions : 1;
            const int dir = std::min(state, dirs - 1);
            const uint8_t* rle = anim->GetFrame(0, dir);
            if (rle) {
                const int w = anim->m_width;
                const int h = anim->m_height;
                // Decode the RLE sprite frame into linear palette indices (0 = transparent) via the engine's own UI blitter.
                std::vector<uint16_t> decoded(static_cast<size_t>(w * h), 0);
                uint16_t identityPal[256];
                for (int i = 0; i < 256; ++i) identityPal[i] = static_cast<uint16_t>(i);
                ui::pixel_primitives::Ui16(decoded.data(), rle, identityPal, w, 0, 0, w, h, 0, 0, w, h);
                std::vector<uint8_t> frameBuf(static_cast<size_t>(w * h), 0);
                for (size_t i = 0; i < decoded.size(); ++i) frameBuf[i] = static_cast<uint8_t>(decoded[i]);
                const uint8_t* frame = frameBuf.data();
                const int clipTop = h / 5;
                const int tabH = h - clipTop;
                const int capW = std::min(w / 4, 28);
                const bool rgb555 = renderer->m_rgb555 != 0;

                // Light-interior sprites (translucent pills) frost the saved sky backdrop smoothly inside the sprite's exact
                // outline; dark opaque sprites draw their own pixels solid.
                int cr = 0, cg = 0, cb = 0;
                const uint8_t cleanIdx = frame[(h / 2) * w + capW];
                if (cleanIdx != 0) Unpack16(pal[cleanIdx], rgb555, cr, cg, cb);
                const bool translucent = (cr + cg + cb) > 450;
                const uint16_t frost16 = renderer_ops::PackColor(rgb555, 255, 255, 255);
                const int fillAlpha = state == 0 ? 45 : (state == 1 ? 90 : 120);

                const bool direct = renderer->m_directMode != 0;
                uint16_t* bits16 = direct ? renderer::GetBackBufferBits(renderer) : nullptr;
                uint32_t* bits32 = direct ? nullptr : renderer->m_workBuffer;
                if (bits16 || bits32) {
                    int pitch = renderer->m_width, ox = 0, oy = 0;
                    int surfW = renderer->m_width, surfH = renderer->m_height;
                    if (direct) {
                        const auto* surface = SurfaceAt(*renderer, renderer->m_activeSurface);
                        pitch = surface->m_pitch / 2;
                        ox = surface->m_originX;
                        oy = surface->m_originY;
                        surfW = surface->m_width;
                        surfH = surface->m_height;
                    }
                    if (m_backdropW != w || m_backdropH != tabH) {
                        m_backdrop.assign(static_cast<size_t>(w * tabH), 0);
                        m_backdropW = w;
                        m_backdropH = tabH;
                        for (int y = 0; y < tabH; ++y) {
                            const int dy = m_y0 + y - oy;
                            if (dy < 0 || dy >= surfH) continue;
                            for (int x = 0; x < w; ++x) {
                                const int dx = m_x0 + x - ox;
                                if (dx < 0 || dx >= surfW) continue;
                                m_backdrop[static_cast<size_t>(y * w + x)] =
                                        direct ? bits16[dy * pitch + dx] : static_cast<uint16_t>(bits32[dy * pitch + dx]);
                            }
                        }
                    }
                    for (int y = 0; y < tabH; ++y) {
                        const int dy = m_y0 + y - oy;
                        if (dy < 0 || dy >= surfH) continue;
                        const int py = clipTop + y;
                        for (int x = 0; x < w; ++x) {
                            const int dx = m_x0 + x - ox;
                            if (dx < 0 || dx >= surfW) continue;
                            const int sx = (x < capW) ? x : (x >= w - capW ? x : capW);
                            const uint8_t idx = frame[py * w + sx];
                            const uint16_t bg = m_backdrop[static_cast<size_t>(y * w + x)];
                            uint16_t out = bg;
                            if (idx != 0) {
                                const uint16_t src = pal[idx];
                                if (!translucent || IsBorderPixel(frame, w, h, sx, py))
                                    out = src;
                                else
                                    out = Blend16(bg, frost16, fillAlpha, rgb555);
                            }
                            if (direct)
                                bits16[dy * pitch + dx] = out;
                            else
                                bits32[dy * pitch + dx] = (bits32[dy * pitch + dx] & 0xFFFF0000u) | out;
                        }
                    }
                    if (direct) renderer::UnlockSurface(renderer);
                    labelRgb = SampleLabelColor(frame, pal, w, h, capW, rgb555);
                    drewNative = true;
                }
            }
        }
    }

    if (!drewNative) {
        const uint32_t fill = state == 2 ? own::kSeaDark : (state == 1 ? 0x2a82c4 : own::kSea);
        own::Fill(m_x0, m_y0, m_x1, m_y1, fill);
        own::Frame(m_x0, m_y0, m_x1, m_y1, own::kFoam);
    }

    const int font = 2;
    const int y = m_y0 + (m_y1 - m_y0 - own::LineHeight(font)) / 2 + (state == 2 ? 1 : 0);
    const Char16* label = text_table::Lookup(m_text.c_str());
    own::Text(font, m_x0 + 8, y, m_x1 - m_x0 - 16, label, labelRgb, 2);
    // Double-pass horizontal bolding matches the heavier weight of the native MainMenu button labels.
    if (drewNative) own::Text(font, m_x0 + 9, y, m_x1 - m_x0 - 16, label, labelRgb, 2);
    m_dirty = 0;
    m_drawnState = state;
}

bool GuiOwnButton::MouseLeft(int x, int y, int state)
{
    if (!m_visible || !HitTestBase(x, y)) return false;
    if (state == 1) {
        // A tap of a touch screen is a press too (platform/touch_gestures.h), so the press acts.
        m_held = 1;
        m_dirty = 1;
        m_pressed = true;
        return true;
    }
    if (state == 0 && m_held) {
        m_held = 0;
        m_dirty = 1;
    }
    return false;
}

void GuiOwnButton::Update(float)
{
    const int state = m_held ? 2 : (m_hovered ? 1 : 0);
    if (m_visible && state != m_drawnState) m_dirty = 1;
    m_hovered = 0;
    if (!m_pressed) return;
    m_pressed = false;
    m_held = 0;
    GuiManager* gui = game::GetGuiManager();
    if (!gui) return;
    gui->CloseAllDialogs();
    gui->ShowDialog(m_dialog.c_str(), nullptr, false);
}

} // namespace obl::ui

namespace obl::gui::own_widgets {

namespace {
bool g_lookSampled = false;
PackLook g_look;

uint32_t Mix(uint32_t a, uint32_t b, int percentB)
{
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const int ca = static_cast<int>((a >> shift) & 255), cb = static_cast<int>((b >> shift) & 255);
        out |= static_cast<uint32_t>((ca * (100 - percentB) + cb * percentB) / 100) << shift;
    }
    return out;
}

// Rows of a rounded rectangle: each row is inset by what the circle of radius `r` cuts off at that distance from the edge.
int Inset(int r, int dist)
{
    int inset = 0;
    while (inset < r && (r - inset - 1) * (r - inset - 1) + (r - dist - 1) * (r - dist - 1) >= r * r) ++inset;
    return inset;
}

void RoundFill(int x0, int y0, int x1, int y1, int r, uint32_t rgb)
{
    r = std::min({r, (x1 - x0) / 2, (y1 - y0) / 2});
    if (r <= 0) {
        Fill(x0, y0, x1, y1, rgb);
        return;
    }
    for (int d = 0; d < r; ++d) {
        const int inset = Inset(r, d);
        Fill(x0 + inset, y0 + d, x1 - inset, y0 + d + 1, rgb);
        Fill(x0 + inset, y1 - d - 1, x1 - inset, y1 - d, rgb);
    }
    if (y1 - y0 > 2 * r) Fill(x0, y0 + r, x1, y1 - r, rgb);
}
} // namespace

void ResetPackLook()
{
    g_lookSampled = false;
    g_look = PackLook{};
}

const PackLook& GetPackLook()
{
    if (g_lookSampled) return g_look;
    g_lookSampled = true;
    ui::Renderer* renderer = game::GetRenderer();
    auto* bank = static_cast<ui::SpriteBank*>(ui::GetSpriteBank());
    const char* graphic = ui::FindMenuButtonGraphic();
    if (!renderer || !bank || !graphic) return g_look;
    ui::SpriteInstance* instance = bank->CreateObservedInstance(graphic);
    if (instance && instance->m_sprite && instance->m_sprite->m_anims && instance->m_sprite->m_palettes) {
        ui::SpriteAnim* anim = instance->m_sprite->m_anims[instance->m_animIndex];
        const uint16_t* pal = instance->m_sprite->m_palettes[0];
        const uint8_t* rle = anim && pal && anim->m_width > 0 && anim->m_height > 0 ? anim->GetFrame(0, 0) : nullptr;
        if (rle) {
            const int w = anim->m_width, h = anim->m_height;
            std::vector<uint16_t> decoded(static_cast<size_t>(w * h), 0);
            uint16_t identityPal[256];
            for (int i = 0; i < 256; ++i) identityPal[i] = static_cast<uint16_t>(i);
            ui::pixel_primitives::Ui16(decoded.data(), rle, identityPal, w, 0, 0, w, h, 0, 0, w, h);
            std::vector<uint8_t> frame(decoded.size(), 0);
            for (size_t i = 0; i < decoded.size(); ++i) frame[i] = static_cast<uint8_t>(decoded[i]);
            const bool rgb555 = renderer->m_rgb555 != 0;
            const uint32_t accent = ui::SampleLabelColor(frame.data(), pal, w, h, std::min(w / 4, 28), rgb555);
            // A white label means a dark button: the flat palette already suits it. A coloured one belongs to a light pack whose
            // menu is coloured text on frosted pills.
            if (accent != kWhite && accent != 0) {
                g_look.available = true;
                g_look.accent = accent;
            }
        }
    }
    if (instance) bank->ReleaseInstance(instance);
    if (g_look.available) {
        const uint32_t a = g_look.accent;
        Palette& p = g_look.palette;
        p.sea = Mix(a, 0xffffff, 22);
        p.seaDark = Mix(a, 0x000000, 20);
        p.sand = Mix(a, 0xffffff, 90);
        p.foam = Mix(a, 0xffffff, 95);
        p.ink = Mix(a, 0x000000, 65);
        p.grey = Mix(a, 0x707080, 55);
        p.border = Mix(a, 0xffffff, 55);
    }
    return g_look;
}

void Box(int x0, int y0, int x1, int y1, int radius, uint32_t fill, uint32_t border)
{
    if (radius <= 0) {
        Fill(x0, y0, x1, y1, fill);
        Frame(x0, y0, x1, y1, border);
        return;
    }
    RoundFill(x0, y0, x1, y1, radius, border);
    RoundFill(x0 + 1, y0 + 1, x1 - 1, y1 - 1, radius - 1, fill);
}

} // namespace obl::gui::own_widgets
