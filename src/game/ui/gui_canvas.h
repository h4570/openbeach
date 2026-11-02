#pragma once

#include "core/char16.h"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace obl::gui_canvas {

inline constexpr float kClippedTime = 999999.0f;

// GuiManager::m_textRenderers[0..9]: the font slots a widget picks by index.
inline constexpr int kTextRenderers = 10;

struct SpriteMetrics {
    int directions = 0;
    int width = 0;
    int height = 0;
    bool operator==(const SpriteMetrics&) const = default;
};

enum class CreateCall { Sprite, Bank };

using SpriteId = int;
inline constexpr SpriteId kNoSprite = -1;

// What a TextRenderer holds between its setters and the draw (ui::TextRenderer): SetRect, SetColor, SetOutlined, SetAlign
// only store, so this is everything a draw reads from the object.
struct TextState {
    int x = 0, y = 0, width = 0, height = 0;
    uint32_t color = 0;
    bool outlined = false;
    int align = 0; // 0 left, 1 centre, 2 right
    bool operator==(const TextState&) const = default;
};

class Canvas {
public:
    virtual ~Canvas() = default;

    virtual SpriteId CreateSprite(const char* name, CreateCall call) = 0;
    virtual SpriteMetrics Metrics(SpriteId sprite) const = 0; // animation 0 of the instance just created
    virtual void SetAnimType(SpriteId sprite, int type) = 0; // SpriteInstance::m_animType (3 = frame set from outside)
    virtual void SetFrame(SpriteId sprite, int frame) = 0; // SpriteInstance::m_frameIndex
    virtual void SetDirection(SpriteId sprite, int direction) = 0; // SpriteInstance::m_direction
    // Clipped draw of the instance at (x, y) with a depth and an animation time; widgets pass kClippedTime, a GuiMode passes the dt
    // of its last Update
    virtual void DrawClippedAt(SpriteId sprite, int x, int y, uint32_t depth, float time) = 0;
    void DrawClipped(SpriteId sprite, int x, int y, uint32_t depth) { DrawClippedAt(sprite, x, y, depth, kClippedTime); }
    // Partial draw of the current frame: [left, right) x [top, bottom) is the part of the frame that is drawn and (x, y) is where its first row
    // lands (a positive `top` does not move it). Keep `left` at 0: the blitters treat a positive one as a quirk.
    virtual void Draw(SpriteId sprite, int x, int y, int left, int top, int right, int bottom, uint32_t depth) = 0;
    virtual void ReleaseSprite(SpriteId sprite) = 0; // hands the instance back to the sprite bank

    // `renderer` is an index into GuiManager::m_textRenderers.
    virtual void SetTextRect(int renderer, int x, int y, int width, int height) = 0;
    virtual void SetTextColor(int renderer, uint32_t rgb) = 0;
    virtual void SetTextOutlined(int renderer, bool outlined) = 0;
    virtual void SetTextAlign(int renderer, int align) = 0;
    // Breaks the text into lines, then draws each like DrawLine.
    virtual void DrawWrapped(int renderer, int x, int y, const Char16* text) = 0;
    virtual void DrawLine(int renderer, int x, int y, const Char16* text) = 0; // one line of text, no wrapping

    // Packed 16-bit pixel colour; coordinates are captured before clipping. XYWH stays a distinct call.
    virtual void FillRect(int left, int top, int right, int bottom, uint16_t color) = 0;
    virtual void FillRectBySize(int x, int y, int width, int height, uint16_t color) = 0;
};

struct Call {
    enum Kind { Create, DrawClipped, Draw, DrawText, Fill } kind = Create;
    bool xywh = false; // Fill: right/bottom hold width/height for XYWH
    uint16_t color = 0; // Fill: already packed RGB555/RGB565
    CreateCall create = CreateCall::Sprite; // Create
    std::string name; // Create: requested name; sprite draws: the instance's name
    int animType = 0, frame = 0, direction = 0; // sprite draws: instance state at the draw
    int x = 0, y = 0; // draws
    uint32_t depth = 0; // sprite draws
    float time = 0; // DrawClipped: the time argument, always kClippedTime
    int left = 0, top = 0, right = 0, bottom = 0; // Draw
    bool wrapped = false; // DrawText: DrawWrapped rather than DrawLine
    int renderer = -1; // DrawText: slot in GuiManager::m_textRenderers, -1 for a renderer outside the table
    TextState state; // DrawText: the renderer at the draw
    Text16 text; // DrawText: before the code-page conversion, as the caller passed it
    bool operator==(const Call&) const = default;
};

// One line per call, for failure messages.
std::string Describe(const Call& call);

// Empty when the two lists are identical, otherwise a message with the first difference (and both lengths if they differ).
std::string Compare(const std::vector<Call>& expected, const std::vector<Call>& actual);

// A canvas that only notes what was asked of it. `metrics` answers for a sprite name (the game falls back to "dummy" for an
// unknown name; that lookup belongs to the caller), default is empty metrics. Text renderers start zeroed; `SeedText` copies
// what the live renderers held before the draw, for code that relies on the previous state (alignment is often left alone).
class RecordingCanvas final : public Canvas {
public:
    using MetricsLookup = std::function<SpriteMetrics(const char* name)>;
    explicit RecordingCanvas(MetricsLookup metrics = {}) : m_metricsLookup(std::move(metrics)) {}

    std::vector<Call> calls;
    int created = 0, released = 0;

    void SeedText(int renderer, const TextState& state);
    TextState Text(int renderer) const;
    int Live() const { return created - released; } // instances not yet released (a draw must give them all back)

    SpriteId CreateSprite(const char* name, CreateCall call) override;
    SpriteMetrics Metrics(SpriteId sprite) const override;
    void SetAnimType(SpriteId sprite, int type) override;
    void SetFrame(SpriteId sprite, int frame) override;
    void SetDirection(SpriteId sprite, int direction) override;
    void DrawClippedAt(SpriteId sprite, int x, int y, uint32_t depth, float time) override;
    void Draw(SpriteId sprite, int x, int y, int left, int top, int right, int bottom, uint32_t depth) override;
    void ReleaseSprite(SpriteId sprite) override;

    void SetTextRect(int renderer, int x, int y, int width, int height) override;
    void SetTextColor(int renderer, uint32_t rgb) override;
    void SetTextOutlined(int renderer, bool outlined) override;
    void SetTextAlign(int renderer, int align) override;
    void DrawWrapped(int renderer, int x, int y, const Char16* text) override;
    void DrawLine(int renderer, int x, int y, const Char16* text) override;
    void FillRect(int left, int top, int right, int bottom, uint16_t color) override;
    void FillRectBySize(int x, int y, int width, int height, uint16_t color) override;

    // An existing widget instance, without an observable create call.
    SpriteId SeedSprite(const char* name, SpriteMetrics metrics, int animType, int frame, int direction);

private:
    struct Instance {
        std::string name;
        SpriteMetrics metrics;
        int animType = 0, frame = 0, direction = 0;
        bool released = false;
    };
    Instance* Get(SpriteId sprite);
    const Instance* Get(SpriteId sprite) const;
    TextState* Renderer(int renderer);
    void DrawText(int renderer, bool wrapped, int x, int y, const Char16* text);

    MetricsLookup m_metricsLookup;
    std::vector<Instance> m_instances;
    TextState m_text[kTextRenderers];
};

} // namespace obl::gui_canvas
