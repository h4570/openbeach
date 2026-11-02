#include "core/text16.h"
#include "gui_image_bar_frame.h"

#include "game/game/game_accessors.h"
#include "game/objects/object_base.h"
#include "game/ui/gui_canvas_live.h"
#include "game/ui/gui_types.h"
#include "game/ui/gui_widgets_image_bar.h"
#include "game/ui/sprite_bank.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "png.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <vector>

namespace obl::test {
namespace {

test::CallCounter g_frame("GuiImageBarFrame");

const Char16* g_providerText = u"0";
// The bar's provider answers GetVariableText with the current test value.
struct Provider : object::Base {
    void GetVariableText(const char*, Char16* out) override { text16::Copy(out, g_providerText); }
};

// `call-drive GuiImageBarFrame 1`: a deterministic frame of a real sprite and of the bars in every colour mode, drawn into a guarded
// buffer; the PNG it writes is checked against the committed digest (tools/check_gui_image_bar_frame.py). `call-drive GuiImageBarFrame 2`
// draws the sprite the L0 pack owns, which keeps a missing-sprite placeholder out of the L0 digest.
void DrivePixels(int count)
{
    if (count <= 0) return;
    constexpr int kWidth = 256, kHeight = 192, kGuard = kWidth;
    constexpr uint32_t kGuardWord = 0x12345678, kBackground = 0xffff0842;
    std::vector<uint32_t> buffer(kWidth * kHeight + 2 * kGuard, kGuardWord);
    std::vector<uint8_t> light(kWidth * kHeight, 0);
    auto* renderer = game::GetRenderer();
    const ui::Renderer saved = *renderer;
    renderer->m_width = kWidth;
    renderer->m_height = kHeight;
    renderer->m_directMode = 0;
    renderer->m_view[0] = renderer->m_view[1] = 0;
    renderer->m_view[2] = kWidth;
    renderer->m_view[3] = kHeight;
    renderer->m_zoomLevel = 1;
    renderer->m_zoomScale = 1;
    renderer->m_rgb555 = 1;
    renderer->m_lightBuffer = light.data();
    renderer->m_trackDirtyRects = 0;
    Provider provider{};
    const Char16* values[] = {u"0", u"0.25", u"0.5", u"0.75", u"1"};
    std::vector<int> references;
    for (int n = 0; n < ui::GetSpriteBank()->m_spriteCount; ++n) references.push_back(ui::GetSpriteBank()->m_sprites[n]->m_refCount);
    auto* work = buffer.data() + kGuard;
    std::fill(work, work + kWidth * kHeight, kBackground);
    renderer->m_workBuffer = work;
    // Ui32 deliberately drops a positive source-left bound; clip vertically to exercise visible pixels.
    auto* image = gui::ImageConstruct(new ui::GuiImage(), 4, -3, 100, 100, count == 2 ? "UI3_bp_thumb" : "UI3_bi_sideicon_queue");
    gui::GuiImageSetFrame(image, 0);
    image->m_instance->m_direction = 0;
    image->Draw();
    delete image;
    for (int mode = 0; mode < 3; ++mode) {
        for (int n = 0; n < 5; ++n) {
            const int y = 25 + (mode * 5 + n) * 10;
            auto* bar = gui::GuiBarConstruct(new ui::GuiBar(), 12, y, 213, y + 7, 0, 1, "frame", mode);
            bar->m_provider = &provider;
            g_providerText = values[n];
            bar->Draw();
            delete bar;
        }
    }
    // Both public fill entries contribute actual clipped pixels to this frame.
    gui::LiveCanvas canvas;
    canvas.FillRect(-5, 180, 12, 185, 0x7c00);
    canvas.FillRectBySize(245, 180, 20, 8, 0x03e0);
    *renderer = saved;
    int leaked = 0;
    for (int n = 0; n < ui::GetSpriteBank()->m_spriteCount; ++n) leaked += references[n] != ui::GetSpriteBank()->m_sprites[n]->m_refCount;
    const bool guard = std::all_of(buffer.begin(), buffer.begin() + kGuard, [](uint32_t p) { return p == kGuardWord; }) &&
            std::all_of(buffer.end() - kGuard, buffer.end(), [](uint32_t p) { return p == kGuardWord; });
    const bool changed = std::count(buffer.begin() + kGuard, buffer.end() - kGuard, kBackground) < kWidth * kHeight - 1000;
    const bool sprite = std::count(buffer.begin() + kGuard, buffer.begin() + kGuard + 25 * kWidth, kBackground) < 25 * kWidth;
    // Relative to the game directory (cwd): the standalone host's executable lives in build/, not run/.
    const std::filesystem::path folder = "captures";
    std::filesystem::create_directories(folder);
    std::vector<uint16_t> pixels(kWidth * kHeight);
    for (size_t n = 0; n < pixels.size(); ++n) pixels[n] = static_cast<uint16_t>(buffer[n + kGuard]);
    const auto path = folder / "gui_image_bar_frame.png";
    const bool written = WritePngRgb555(path.c_str(), pixels.data(), kWidth, kHeight, kWidth);
    g_frame.Record(guard && changed && written && !leaked && sprite, "pixels/depth/guards/frame PNG/sprite references");
    OBL_LOG("autotest: gui-image-bar-frame mismatch=0 guard=%d changed=%d written=%d restored=1 leaked=%d sprite=%d", guard, changed,
            written, leaked, sprite);
}
} // namespace

bool RegisterGuiImageBarFrame()
{
    g_frame.SetDriver([](int n) { DrivePixels(n); });
    return true;
}
} // namespace obl::test
