#pragma once

#include "core/char16.h"
#include "game/ui/gui_canvas.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"

#include <vector>

namespace obl::gui {

class LiveCanvas final : public gui_canvas::Canvas {
public:
    // Instances still alive when the canvas goes away are released, so a draw routine that bails out cannot leak references.
    ~LiveCanvas() override;

    gui_canvas::SpriteId CreateSprite(const char* name, gui_canvas::CreateCall call) override;
    gui_canvas::SpriteMetrics Metrics(gui_canvas::SpriteId sprite) const override;
    void SetAnimType(gui_canvas::SpriteId sprite, int type) override;
    void SetFrame(gui_canvas::SpriteId sprite, int frame) override;
    void SetDirection(gui_canvas::SpriteId sprite, int direction) override;
    void DrawClippedAt(gui_canvas::SpriteId sprite, int x, int y, uint32_t depth, float time) override;
    void Draw(gui_canvas::SpriteId sprite, int x, int y, int left, int top, int right, int bottom, uint32_t depth) override;
    void ReleaseSprite(gui_canvas::SpriteId sprite) override;

    void SetTextRect(int renderer, int x, int y, int width, int height) override;
    void SetTextColor(int renderer, uint32_t rgb) override;
    void SetTextOutlined(int renderer, bool outlined) override;
    void SetTextAlign(int renderer, int align) override;
    void DrawWrapped(int renderer, int x, int y, const Char16* text) override;
    void DrawLine(int renderer, int x, int y, const Char16* text) override;
    void FillRect(int left, int top, int right, int bottom, uint16_t color) override;
    void FillRectBySize(int x, int y, int width, int height, uint16_t color) override;
    gui_canvas::SpriteId BorrowSprite(ui::SpriteInstance* instance);

    // The instance behind a handle (null once released), for callers that keep the pointer in a game object like m_instance.
    ui::SpriteInstance* InstanceOf(gui_canvas::SpriteId sprite) const;

private:
    std::vector<ui::SpriteInstance*> m_instances; // null after release
    std::vector<bool> m_owned;
};

// First animation of the sprite `name` resolves to, with the game's fallback to "dummy" for an unknown name (names match without
// regard to case) - the same lookup the live sprite creation does, for a RecordingCanvas that stands in for this one.
gui_canvas::SpriteMetrics GameSpriteMetrics(const char* name);

ui::TextRenderer* TextRendererSlot(int renderer);

// The slot whose renderer is `object`, or -1 (a renderer that does not live in GuiManager::m_textRenderers).
int TextRendererIndex(const void* object);

} // namespace obl::gui
