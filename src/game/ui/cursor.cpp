#include "game/ui/renderer_api.h"
#include "game/ui/cursor.h"
#include "game/game/diagnostics.h"
#include "game/ui/image.h"
#include "game/ui/input.h"

#include "game/game/game_accessors.h"
#include "game/ui/cursor_logic.h"
#include "game/ui/cursor_types.h"

// The game's software cursor object (g_Cursor). The OS-cursor side lives in cursor.cpp; this file is the object contract
// over the Input functions of input.h.
namespace obl::ui {
namespace {

using ui::SoftwareCursor;
namespace logic = obl::cursor;

void SetInputShape(int shape)
{
    InputSetCursorShape(*game::GetInput(), shape);
}

} // namespace

SoftwareCursor* CursorConstruct(SoftwareCursor* cursor, const char* tgaPath, void* owner)
{
    game::SetCursor(cursor);
    cursor->m_owner = owner;
    cursor->m_x = logic::kStartX;
    cursor->m_y = logic::kStartY;
    cursor->m_shape = 0;
    SetInputShape(logic::InputShape(0));
    cursor->m_frame = 0;
    cursor->m_phase = 0;
    cursor->m_systemCursorHidden = 0;
    cursor->m_dragMode = 0;
    cursor->m_speed = logic::kStartSpeed;
    cursor->m_image = ui::images::LoadTga(tgaPath);
    ui::images::BuildPalette16(cursor->m_image, game::GetRenderer()->m_rgb555 == 1);
    return cursor;
}

void CursorDestruct(SoftwareCursor* cursor)
{
    ui::images::Free(cursor->m_image);
    game::SetCursor(nullptr);
}

bool CursorUpdate(SoftwareCursor* cursor)
{
    cursor->m_phase = logic::AdvancePhase(cursor->m_phase, game::GetGameClock()->m_deltaTime, cursor->m_speed);
    cursor->m_frame = 0;
    cursor->m_x = game::GetInput()->m_x;
    cursor->m_y = game::GetInput()->m_y;
    return true;
}

void CursorSetDragMode(SoftwareCursor* cursor, uint32_t enable)
{
    cursor->m_dragMode = static_cast<uint8_t>(enable);
    InputSetRelativeMode(*game::GetInput(), enable & 0xff);
}

bool CursorIsInRect(SoftwareCursor* cursor, int left, int top, int width, int height)
{
    return logic::InRect(cursor->m_x, cursor->m_y, left, top, width, height);
}

void CursorSetShape(SoftwareCursor* cursor, int shape)
{
    cursor->m_shape = shape;
    if (const int input = logic::InputShape(shape)) SetInputShape(input);
}

bool CursorRender(SoftwareCursor* cursor)
{
    if (!logic::RenderStateLegal(cursor->m_shape, cursor->m_frame)) {
        // FatalError ends the process.
        if (cursor->m_shape >= logic::kShapes)
            game::FatalError("Cursor: cursor shape %d out of range", cursor->m_shape);
        else
            game::FatalError("Cursor: animation frame %d out of range", cursor->m_frame);
        return true;
    }
    if (!logic::Visible(cursor->m_x, cursor->m_y)) return true;
    const auto cell = logic::RenderCell(cursor->m_shape, cursor->m_frame);
    int position[2] = {0, 0};
    int rect[4] = {cell.left, cell.top, cell.right, cell.bottom};
    obl::ui::renderer::DrawImage(game::GetRenderer(), position, rect, cursor->m_image, 0xb, 1, 0);
    return true;
}

void CursorSetSystemHidden(SoftwareCursor* cursor, uint32_t hide)
{
    cursor->m_systemCursorHidden = static_cast<uint8_t>(hide);
    if (hide & 0xff)
        ui::InputHideSystemCursor();
    else
        ui::InputShowSystemCursor();
}

int* CursorGetPos(SoftwareCursor* cursor, int* out)
{
    logic::WorldPos(cursor->m_x, cursor->m_y, game::GetRenderer()->m_zoomScale, out);
    return out;
}

} // namespace obl::ui
