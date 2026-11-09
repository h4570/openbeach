#include "settings_view.h"

#include "settings_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string_view>

namespace obl::launcher::settings_view {

namespace {

namespace settings = game::start_settings;

constexpr float kChar = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
constexpr float kHeaderHeight = 48;
constexpr float kRowTop = 60;
constexpr float kRowHeight = 28;
constexpr float kValueLeft = 236;
constexpr float kValueRight = 412;
constexpr float kButtonTop = 270;
constexpr float kButtonHeight = 28;
constexpr SDL_FRect kQuitButton{222, kButtonTop, 84, kButtonHeight};
constexpr SDL_FRect kPlayButton{316, kButtonTop, 96, kButtonHeight};

struct Color {
    Uint8 r, g, b;
};
constexpr Color kSand{245, 230, 196};
constexpr Color kSea{32, 110, 170};
constexpr Color kSeaLight{70, 150, 210};
constexpr Color kFoam{220, 236, 248};
constexpr Color kInk{40, 40, 40};
constexpr Color kHint{125, 112, 88};
constexpr Color kDisabled{170, 160, 140};
constexpr Color kFocus{252, 243, 222};
constexpr Color kBox{255, 252, 242};
constexpr Color kBorder{200, 180, 140};
constexpr Color kWhite{255, 255, 255};

std::string ScaleName(double scale)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%gx", scale);
    return text;
}

// `list` with `value` in it: a value from the file that the window does not offer stays selectable.
template<class T> std::vector<T> WithValue(std::vector<T> list, T value)
{
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
    return list;
}

template<class T> T Step(const std::vector<T>& list, T value, int delta)
{
    const auto at = static_cast<int>(std::find(list.begin(), list.end(), value) - list.begin());
    const int count = static_cast<int>(list.size());
    return list[static_cast<size_t>(((at + delta) % count + count) % count)];
}

void SetColor(SDL_Renderer* renderer, Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
}

void Fill(SDL_Renderer* renderer, const SDL_FRect& rect, Color color)
{
    SetColor(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

void Frame(SDL_Renderer* renderer, const SDL_FRect& rect, Color color)
{
    SetColor(renderer, color);
    SDL_RenderRect(renderer, &rect);
}

void Print(SDL_Renderer* renderer, float x, float y, std::string_view text, Color color, float size = 1)
{
    SetColor(renderer, color);
    float scaleX = 1, scaleY = 1;
    SDL_GetRenderScale(renderer, &scaleX, &scaleY);
    SDL_SetRenderScale(renderer, scaleX * size, scaleY * size);
    SDL_RenderDebugText(renderer, x / size, y / size, std::string(text).c_str());
    SDL_SetRenderScale(renderer, scaleX, scaleY);
}

float TextWidth(std::string_view text)
{
    return kChar * static_cast<float>(settings_text::CountColumns(text));
}

void Button(SDL_Renderer* renderer, const SDL_FRect& rect, const char* label, bool primary, bool focused)
{
    if (primary) {
        Fill(renderer, rect, focused ? kSeaLight : kSea);
    } else {
        Fill(renderer, rect, focused ? kFocus : kBox);
        Frame(renderer, rect, kBorder);
    }
    if (focused && !primary) Frame(renderer, SDL_FRect{rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4}, kSea);
    if (focused && primary) Frame(renderer, SDL_FRect{rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4}, kWhite);
    Print(renderer, rect.x + (rect.w - TextWidth(label)) / 2, rect.y + (rect.h - kChar) / 2, label, primary ? kWhite : kInk);
}

SDL_FRect RowRect(int row)
{
    return {10, kRowTop + row * kRowHeight, static_cast<float>(kWidth) - 20, kRowHeight};
}

} // namespace

Model::Model(settings::Values values) : m_values(std::move(values))
{
    m_scales = WithValue(std::vector<double>(std::begin(settings::kScales), std::end(settings::kScales)), m_values.scale);
    std::sort(m_scales.begin(), m_scales.end());
    m_frameCaps = WithValue(std::vector<int>(std::begin(settings::kFrameCaps), std::end(settings::kFrameCaps)), m_values.maxFps);
    // Unlimited (0) stays last.
    std::stable_partition(m_frameCaps.begin(), m_frameCaps.end(), [](int fps) { return fps > 0; });
    std::sort(m_frameCaps.begin(), m_frameCaps.end() - (m_frameCaps.back() == 0 ? 1 : 0));
}

unsigned Model::TextLanguage() const
{
    return m_values.languages.empty() ? 0 : m_values.language;
}

RowText Model::Text(int row) const
{
    const settings::Values& v = m_values;
    const settings_text::Texts& t = settings_text::ForLanguage(TextLanguage());
    const auto onOff = [&t](bool value) { return std::string(value ? t.on : t.off); };
    switch (row) {
    case Language: {
        const bool choice = v.languages.size() > 1;
        return {t.language, choice ? t.languageHint : t.onlyLanguageHint,
                v.languages.empty() ? std::string("-") : std::string(settings_text::LanguageName(v.language)), choice};
    }
#ifdef __APPLE__
    case Display: return {t.display, t.switchHintMac, v.windowed ? t.window : t.fullscreen, true};
#else
    case Display: return {t.display, t.switchHint, v.windowed ? t.window : t.fullscreen, true};
#endif
    case Size: return {t.size, t.sizeHint, ScaleName(v.scale), v.windowed};
    case Mouse: return {t.mouse, t.mouseHint, onOff(v.confineMouse), true};
    case FrameCap: return {t.frameCap, t.frameCapHint, v.maxFps > 0 ? std::to_string(v.maxFps) + " fps" : std::string(t.unlimited), true};
    case Movie: return {t.movie, t.movieHint, onOff(v.playMovie), true};
    default: return {t.show, t.showHint, onOff(v.showAtStart), true};
    }
}

void Model::Change(int row, int delta)
{
    if (row < 0 || row >= kRowCount || !Text(row).enabled) return;
    settings::Values& v = m_values;
    switch (row) {
    case Language: v.language = Step(v.languages, v.language, delta); break;
    case Display: v.windowed = !v.windowed; break;
    case Size: v.scale = Step(m_scales, v.scale, delta); break;
    case Mouse: v.confineMouse = !v.confineMouse; break;
    case FrameCap: v.maxFps = Step(m_frameCaps, v.maxFps, delta); break;
    case Movie: v.playMovie = !v.playMovie; break;
    default: v.showAtStart = !v.showAtStart; break;
    }
}

void Draw(SDL_Renderer* renderer, const Model& model, int focus)
{
    const settings_text::Texts& t = settings_text::ForLanguage(model.TextLanguage());
    SetColor(renderer, kSand);
    SDL_RenderClear(renderer);
    Fill(renderer, SDL_FRect{0, 0, static_cast<float>(kWidth), kHeaderHeight}, kSea);
    Print(renderer, 20, 9, "OpenBeach", kWhite, 2);
    Print(renderer, 20, 31, t.subtitle, kFoam);

    for (int row = 0; row < kRowCount; ++row) {
        const RowText text = model.Text(row);
        const SDL_FRect rect = RowRect(row);
        if (row == focus) Fill(renderer, rect, kFocus);
        Print(renderer, 20, rect.y + 5, text.label, kInk);
        Print(renderer, 20, rect.y + 16, text.hint, kHint);
        const SDL_FRect box = ValueBox(row);
        Fill(renderer, box, kBox);
        Frame(renderer, box, row == focus ? kSea : kBorder);
        const float textY = box.y + (box.h - kChar) / 2;
        Print(renderer, box.x + std::floor((box.w - TextWidth(text.value)) / 2), textY, text.value, text.enabled ? kInk : kDisabled);
        if (text.enabled) {
            Print(renderer, box.x + 6, textY, "<", kSea);
            Print(renderer, box.x + box.w - 6 - kChar, textY, ">", kSea);
        }
    }

    Print(renderer, 20, kButtonTop + (kButtonHeight - kChar) / 2, t.keys, kHint);
    Button(renderer, kQuitButton, t.quit, false, focus == kQuitFocus);
    Button(renderer, kPlayButton, t.play, true, focus == kPlayFocus);
}

bool Inside(const SDL_FRect& rect, float x, float y)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

int HitTest(float x, float y)
{
    if (Inside(kQuitButton, x, y)) return kQuitFocus;
    if (Inside(kPlayButton, x, y)) return kPlayFocus;
    for (int row = 0; row < kRowCount; ++row)
        if (Inside(RowRect(row), x, y)) return row;
    return -1;
}

SDL_FRect ValueBox(int row)
{
    return {kValueLeft, kRowTop + row * kRowHeight + 4, kValueRight - kValueLeft, kRowHeight - 8};
}

SDL_Texture* LayoutTexture(SDL_Renderer* renderer, float scale)
{
    const int multiple = std::max(1, static_cast<int>(std::ceil(scale)));
    SDL_Texture* texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, kWidth * multiple, kHeight * multiple);
    if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

void Present(SDL_Renderer* renderer, SDL_Texture* layout, const Model& model, int focus)
{
    float width = kWidth, height = kHeight;
    SDL_GetTextureSize(layout, &width, &height);
    SDL_SetRenderTarget(renderer, layout);
    SDL_SetRenderScale(renderer, width / kWidth, height / kHeight);
    Draw(renderer, model, focus);
    SDL_SetRenderScale(renderer, 1, 1);
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, layout, nullptr, nullptr);
}

float WindowScale(float contentScale, int usableWidth, int usableHeight)
{
    if (!(contentScale > 0)) contentScale = 1.0f;
    float scale = std::max(1.0f, std::round(contentScale * 3.0f) / 2.0f);
    if (usableWidth > 0 && usableHeight > 0) {
        while (scale > 1.0f && (kWidth * scale > usableWidth * 0.9f || kHeight * scale > usableHeight * 0.9f)) scale -= 0.5f;
    }
    return scale;
}

SDL_Surface* RenderImage(const Model& model, int focus, float scale)
{
    SDL_Surface* surface = SDL_CreateSurface(
            static_cast<int>(std::lround(kWidth * scale)), static_cast<int>(std::lround(kHeight * scale)), SDL_PIXELFORMAT_XRGB8888);
    if (!surface) return nullptr;
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    SDL_Texture* layout = renderer ? LayoutTexture(renderer, scale) : nullptr;
    if (!layout) {
        if (renderer) SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        return nullptr;
    }
    SDL_SetRenderLogicalPresentation(renderer, kWidth, kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    Present(renderer, layout, model, focus);
    SDL_RenderPresent(renderer);
    SDL_DestroyTexture(layout);
    SDL_DestroyRenderer(renderer);
    return surface;
}

} // namespace obl::launcher::settings_view
