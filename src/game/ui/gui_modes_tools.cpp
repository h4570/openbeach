#include "core/char16.h"
#include "game/ui/gui_modes_tools.h"

#include <string>

namespace obl::gui_modes_tools {
namespace {

using gui_canvas::CreateCall;
using gui_canvas::SpriteId;

constexpr uint32_t kDepth = 0xffff;
constexpr int kFilmStripX = 0x1cf;
constexpr int kFilmStripRaise = 0x46;
constexpr int kGapTileLength = 64;

struct BarNames {
    const char* left;
    const char* right;
    const char* top;
    const char* bottom;
    const char* filmstrip;
};

BarNames NamesOf(CameraFrameSet set)
{
    switch (set) {
    case CameraFrameSet::A: return {"ui3_cam_a_left", "ui3_cam_a_right", "ui3_cam_a_top", "ui3_cam_a_bottom", "ui3_cam_a_filmstrip"};
    case CameraFrameSet::B: return {"ui3_cam_b_left", "ui3_cam_b_right", "ui3_cam_b_top", "ui3_cam_b_bottom", "ui3_cam_b_filmstrip"};
    default: return {"ui3_cam_c_left", "ui3_cam_c_right", "ui3_cam_c_top", "ui3_cam_c_bottom", "ui3_cam_c_filmstrip"};
    }
}

void FillBarGap(gui_canvas::Canvas& canvas, SpriteId bar, int barX, int barY, int gapEnd, bool vertical)
{
    const gui_canvas::SpriteMetrics size = canvas.Metrics(bar);
    if (size.width <= 0 || size.height <= 0) return;
    const int length = vertical ? size.height : size.width;
    const int tile = length < kGapTileLength ? length : kGapTileLength;
    const int slice = (length - tile) / 2;
    for (int pos = (vertical ? barY : barX) + length; pos < gapEnd; pos += tile) {
        const int part = gapEnd - pos < tile ? gapEnd - pos : tile;
        if (vertical)
            canvas.Draw(bar, barX, pos, 0, slice, size.width, slice + part, kDepth);
        else
            canvas.Draw(bar, pos, barY, 0, 0, part, size.height, kDepth);
    }
}

} // namespace

CameraFrameSet SelectCameraFrameSet(int width)
{
    if (width >= 1280) return CameraFrameSet::C;
    if (width >= 1024) return CameraFrameSet::B;
    if (width >= 800) return CameraFrameSet::A;
    return CameraFrameSet::None;
}

int AlbumPhotoLimit(int screenWidth)
{
    switch (SelectCameraFrameSet(screenWidth)) {
    case CameraFrameSet::A: return 8;
    case CameraFrameSet::B: return 11;
    case CameraFrameSet::C: return 15;
    default: return 6;
    }
}

int AlbumPhotoLimitFor(int screenWidth)
{
    switch (screenWidth) {
    case 800: return 8;
    case 1024: return 11;
    case 1280: return 15;
    default: return 6;
    }
}

int AlbumDropCount(int count, int limit)
{
    return count >= limit ? count - limit + 1 : 0;
}

std::string AlbumCommentKey(int roll)
{
    return "CAMERA_COMMENT_" + std::to_string(roll % kAlbumComments);
}

void DrawAlbumOverlay(gui_canvas::Canvas& canvas, int width, int height, const std::function<const Char16*()>& comment)
{
    // The logos are placed from their own sizes while the instance is alive; each one is created, drawn and released in turn.
    const auto logo = [&](const char* name, bool fromRight, int margin) {
        const SpriteId sprite = canvas.CreateSprite(name, CreateCall::Sprite);
        const gui_canvas::SpriteMetrics size = canvas.Metrics(sprite);
        const int x = fromRight ? width - size.width - margin : margin;
        canvas.DrawClipped(sprite, x, height - size.height - kAlbumPhotoOffset, kDepth);
        canvas.ReleaseSprite(sprite);
    };
    logo("photo_olympus", false, kAlbumPhotoOffset);
    logo("photo_beachlife", true, kAlbumPhotoOffset);
    logo("photo_eidosdr", true, 0x61);

    const Char16* text = comment();
    canvas.SetTextAlign(kAlbumTextRenderer, 2);
    canvas.SetTextOutlined(kAlbumTextRenderer, false);
    canvas.SetTextColor(kAlbumTextRenderer, 0);
    canvas.SetTextRect(kAlbumTextRenderer, 0, height - 0x96, width, height - 0x82);
    canvas.DrawWrapped(kAlbumTextRenderer, 0, 0, text);
}

void DrawCameraFrame(gui_canvas::Canvas& canvas, int width, int height, float time)
{
    const SpriteId upperLeft = canvas.CreateSprite("ui3_cam_upper_left", CreateCall::Bank);
    const gui_canvas::SpriteMetrics upperLeftSize = canvas.Metrics(upperLeft);
    canvas.DrawClippedAt(upperLeft, 0, 0, kDepth, time);
    canvas.ReleaseSprite(upperLeft);

    const SpriteId upperRight = canvas.CreateSprite("ui3_cam_upper_right", CreateCall::Bank);
    const gui_canvas::SpriteMetrics upperRightSize = canvas.Metrics(upperRight);
    canvas.DrawClippedAt(upperRight, width - upperRightSize.width, 0, kDepth, time);
    canvas.ReleaseSprite(upperRight);

    const SpriteId lowLeft = canvas.CreateSprite("ui3_cam_lowleft", CreateCall::Bank);
    const gui_canvas::SpriteMetrics lowLeftSize = canvas.Metrics(lowLeft);
    canvas.DrawClippedAt(lowLeft, 0, height - lowLeftSize.height, kDepth, time);
    canvas.ReleaseSprite(lowLeft);

    const SpriteId lowRight = canvas.CreateSprite("ui3_cam_lowright", CreateCall::Bank);
    const gui_canvas::SpriteMetrics lowRightSize = canvas.Metrics(lowRight);
    canvas.DrawClippedAt(lowRight, width - lowRightSize.width, height - lowRightSize.height, kDepth, time);
    canvas.ReleaseSprite(lowRight);

    const CameraFrameSet set = SelectCameraFrameSet(width);
    if (set == CameraFrameSet::None) return;
    const BarNames names = NamesOf(set);
    const SpriteId left = canvas.CreateSprite(names.left, CreateCall::Bank);
    const SpriteId right = canvas.CreateSprite(names.right, CreateCall::Bank);
    const SpriteId top = canvas.CreateSprite(names.top, CreateCall::Bank);
    const SpriteId bottom = canvas.CreateSprite(names.bottom, CreateCall::Bank);
    const SpriteId film = canvas.CreateSprite(names.filmstrip, CreateCall::Bank);
    const int rightX = width - canvas.Metrics(right).width;
    const int bottomY = height - canvas.Metrics(bottom).height;
    canvas.DrawClippedAt(left, 0, upperLeftSize.height, kDepth, time);
    FillBarGap(canvas, left, 0, upperLeftSize.height, height - lowLeftSize.height, true);
    canvas.DrawClippedAt(right, rightX, upperRightSize.height, kDepth, time);
    FillBarGap(canvas, right, rightX, upperRightSize.height, height - lowRightSize.height, true);
    canvas.DrawClippedAt(top, upperLeftSize.width, 0, kDepth, time);
    FillBarGap(canvas, top, upperLeftSize.width, 0, width - upperRightSize.width, false);
    canvas.DrawClippedAt(bottom, lowLeftSize.width, bottomY, kDepth, time);
    FillBarGap(canvas, bottom, lowLeftSize.width, bottomY, width - lowRightSize.width, false);
    canvas.DrawClippedAt(film, kFilmStripX, height - kFilmStripRaise, kDepth, time);
    canvas.ReleaseSprite(left);
    canvas.ReleaseSprite(right);
    canvas.ReleaseSprite(top);
    canvas.ReleaseSprite(bottom);
    canvas.ReleaseSprite(film);
}

} // namespace obl::gui_modes_tools
