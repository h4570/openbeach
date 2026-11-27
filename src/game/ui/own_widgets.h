#pragma once

#include "game/ui/gui_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace obl::ui {
struct GuiScript;
}

// Controls of OpenBeach's own layer (own_layer.h) that draw themselves with plain fills and the layout's fonts, so they need no
// sprite of the pack: the mods screen borrows its colour from the pack's menu buttons (GetPackLook) and keeps a flat style otherwise.
//   OwnButton(x0, y0, x1, y1, TEXT_KEY, Dialog)   a button that closes the open dialogs and opens `Dialog`
//   ModsPanel(x0, y0, x1, y1)                     the mods screen (mods_screen.h)
// Mods may use them in their own GUI patches too.
namespace obl::gui::own_widgets {

// Parses the control `name` at the script's cursor (just before its parenthesis) and adds it; false when `name` is not one of them.
bool Parse(ui::GuiScript& script, const char* name);

// Colours as 0xRRGGBB, the screen's palette.
inline constexpr uint32_t kSea = 0x1f6eaa, kSeaDark = 0x12466e, kSand = 0xf5e6c4, kFoam = 0xfcf3de, kInk = 0x282828, kRed = 0xb42828,
                          kGreen = 0x3c9650, kGrey = 0x968c78, kWhite = 0xffffff, kBorder = 0xc8b48c;

// Drawing helpers shared by the own controls: a filled rectangle with a one pixel frame, and a line of text in a font slot of the
// layout (the first slot when the layout has fewer fonts), clipped to `width` with an ellipsis. `align` 0 left, 1 right, 2 centre.
void Fill(int x0, int y0, int x1, int y1, uint32_t rgb);

// The screen's colours in one place: the flat palette above, or colours derived from the pack's own menu buttons.
struct Palette {
    uint32_t sea = kSea, seaDark = kSeaDark, sand = kSand, foam = kFoam, ink = kInk, red = kRed, green = kGreen, grey = kGrey,
             white = kWhite, border = kBorder;
};

// What the pack's MainMenu button sprite tells about its look, sampled at runtime from the player's own art (nothing of it is
// stored): `available` is false for a pack with dark or no sprite buttons, where the flat palette applies.
struct PackLook {
    bool available = false;
    uint32_t accent = kSea; // the colour of the pack's button labels
    Palette palette; // the screen's colours for a light pack
};
const PackLook& GetPackLook();
void ResetPackLook(); // forget the sample (the pack or its layouts changed)

// A box with a one pixel border and corners of `radius` (0: square).
void Box(int x0, int y0, int x1, int y1, int radius, uint32_t fill, uint32_t border);
void Frame(int x0, int y0, int x1, int y1, uint32_t rgb);
void Text(int font, int x, int y, int width, const Char16* text, uint32_t rgb, int align = 0);
void TextWrapped(int font, int x, int y, int width, int height, const Char16* text, uint32_t rgb, int align = 0);
int LineHeight(int font);
// The width a line of text takes in a font slot, and the lines TextWrapped breaks it into at `width`, for laying out by content.
int TextWidth(int font, const Char16* text);
int TextLines(int font, const Char16* text, int width);
// Distance from the top of a text line to the visual middle of its capital letters, to centre a label in a button.
int InkCentre(int font);

} // namespace obl::gui::own_widgets

namespace obl::ui {

struct GuiOwnButton final : GuiLeaf<GuiWidget> {
    GuiOwnButton(int x0, int y0, int x1, int y1, std::string text, std::string dialog);
    ~GuiOwnButton() override;
    void Draw() override;
    bool HitTest(int x, int y) override;
    bool MouseLeft(int x, int y, int state) override;
    void Update(float dt) override;
    void SetVisible(uint32_t visible) override;
    const char* ClassName() const override { return "GuiOwnButton"; }

    void ResolvePackSprite();
    void ReleasePackSprite();

    std::string m_text; // text key
    std::string m_dialog;
    SpriteInstance* m_sprite = nullptr; // sibling MainMenu button sprite adopted at runtime for a native pill tab
    bool m_spriteResolved = false;
    std::vector<uint16_t> m_backdrop; // saved surface pixels behind the tab so translucent hover redraws never accumulate
    int m_backdropW = 0, m_backdropH = 0;
    int m_drawnState = -1;
    bool m_pressed = false; // acted on in Update, outside the dialog's walk over its children
};

} // namespace obl::ui
