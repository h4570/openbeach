#pragma once

#include <cstdint>

namespace obl::ui {
struct SoftwareCursor;
using Cursor = SoftwareCursor;
struct Input;
} // namespace obl::ui

namespace obl::ui {

// The map view's input and drawing state (the World's former `View` group); never saved. The game
// context owns it (game::Context::view); world construction resets every field.
struct ViewState {
    ui::Cursor* m_cursor = nullptr; // the game's cursor (the world's camera input: edge scroll, drag shape)
    ui::Input* m_input = nullptr; // keyboard and mouse state of this frame
    uint8_t m_cameraDragging = 0; // right-button camera drag in progress (the world's camera input)
    uint8_t m_devToggle = 0; // Shift+Alt+T flips it (the world's update); the guest debug draw reads it
    uint8_t m_drawObjectsArg = 0; // fourth argument of MapView::DrawObjects (the world's draw); nothing writes it
    uint8_t m_hotkeysDisabled = 0; // the world's update skips its hotkeys while set; only the constructor writes it
    uint8_t m_darkenByDepth = 0; // the world's draw darkens by depth after the objects; only the constructor writes it (0)
    int m_zoomRequest = 0; // wanted zoom level 0..2 (hotkeys, zoom buttons); the world's update applies it
};

} // namespace obl::ui
