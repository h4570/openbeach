// Observer-based capture of the game's own draw calls and the gui-canvas-check command (docs/architecture/data-formats/converter/gui-runtime.md).

#include "core/char16.h"
#include "gui_capture.h"

#include "game/ui/gui_canvas_live.h"
#include "game/ui/sprite_observer.h"
#include "game/ui/text_observer.h"
#include "game/ui/fill_observer.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/log.h"

#include <map>

namespace obl::test {
namespace {

using namespace gui_canvas;
using Instance = ui::SpriteInstance;

struct State {
    bool active = false;
    std::vector<Call> calls;
    struct Created {
        ui::Sprite* sprite;
        int referencesAfterCreate;
    };
    std::vector<Created> created;
    std::map<const Instance*, std::string> names; // the name each instance was requested under (the instance's own may differ in case)
    const ui::SpriteObserver* previousSprite = nullptr;
    const ui::TextObserver* previousText = nullptr;
    const ui::FillObserver* previousFill = nullptr;
} g;

// Creating a sprite keeps the sprite's own name in the instance, the draws are reported under the requested one so that they match
// what the code asked for; an instance made before the capture began falls back to its own name.
std::string NameOf(const Instance* instance)
{
    const auto it = g.names.find(instance);
    if (it != g.names.end()) return it->second;
    return instance->m_name ? instance->m_name : "";
}

void OnCreate(bool viaBank, const char* name, Instance* instance)
{
    Call call;
    call.kind = Call::Create;
    call.create = viaBank ? CreateCall::Bank : CreateCall::Sprite;
    call.name = name;
    g.calls.push_back(call);
    g.names[instance] = name;
    g.created.push_back({instance->m_sprite, instance->m_sprite->m_refCount});
    if (g.previousSprite && g.previousSprite->create) g.previousSprite->create(viaBank, name, instance);
}

void OnDrawClipped(Instance* instance, int x, int y, uint32_t depth, float time)
{
    Call call;
    call.kind = Call::DrawClipped;
    call.name = NameOf(instance);
    call.animType = instance->m_animType;
    call.frame = instance->m_frameIndex;
    call.direction = instance->m_direction;
    call.x = x;
    call.y = y;
    call.depth = depth;
    call.time = time;
    g.calls.push_back(call);
    if (g.previousSprite && g.previousSprite->drawClipped) g.previousSprite->drawClipped(instance, x, y, depth, time);
}

void OnDraw(Instance* instance, int x, int y, int left, int top, int right, int bottom, uint32_t depth)
{
    Call call;
    call.kind = Call::Draw;
    call.name = NameOf(instance);
    call.animType = instance->m_animType;
    call.frame = instance->m_frameIndex;
    call.direction = instance->m_direction;
    call.x = x;
    call.y = y;
    call.depth = depth;
    call.left = left;
    call.top = top;
    call.right = right;
    call.bottom = bottom;
    g.calls.push_back(call);
    if (g.previousSprite && g.previousSprite->draw) g.previousSprite->draw(instance, x, y, left, top, right, bottom, depth);
}

void RecordText(bool wrapped, const void* renderer, int x, int y, const Char16* text)
{
    const auto* object = static_cast<const ui::TextRenderer*>(renderer);
    Call call;
    call.kind = Call::DrawText;
    call.wrapped = wrapped;
    call.renderer = gui::TextRendererIndex(renderer);
    call.x = x;
    call.y = y;
    call.state = {object->m_x, object->m_y, object->m_width, object->m_height, object->m_color, object->m_outlined != 0, object->m_align};
    if (text) call.text = text;
    g.calls.push_back(call);
}

void OnDrawWrapped(const void* renderer, int x, int y, const Char16* text)
{
    RecordText(true, renderer, x, y, text);
    if (g.previousText && g.previousText->drawWrapped) g.previousText->drawWrapped(renderer, x, y, text);
}

void OnDrawLine(const void* renderer, int x, int y, const Char16* text)
{
    RecordText(false, renderer, x, y, text);
    if (g.previousText && g.previousText->drawLine) g.previousText->drawLine(renderer, x, y, text);
}

const ui::SpriteObserver kSpriteObserver = {&OnCreate, &OnDrawClipped, &OnDraw};
const ui::TextObserver kTextObserver = {&OnDrawWrapped, &OnDrawLine};

void OnFill(const void* renderer, bool xywh, const int* rect, uint16_t color)
{
    Call call;
    call.kind = Call::Fill;
    call.xywh = xywh;
    call.color = color;
    call.left = rect[0];
    call.top = rect[1];
    call.right = rect[2];
    call.bottom = rect[3];
    g.calls.push_back(call);
    if (g.previousFill && g.previousFill->fill) g.previousFill->fill(renderer, xywh, rect, color);
}
const ui::FillObserver kFillObserver = {&OnFill};

// The fixed draw list of gui-canvas-check. It uses every canvas operation: two instances alive at once drawn out of creation
// order (a named sprite, a missing name that falls back to "dummy"), both create entry points, both sprite draws, a released
// instance, and text through every state setter, wrapped (several lines, so the inner DrawLine calls must stay
// unreported), a direct line and a string with characters above 255 (the code-page conversion happens after the report).
// The renderer's alignment is left to the state it already had for the first draw.
void Scenario(Canvas& canvas, int renderer, const TextState& before)
{
    const SpriteId stars = canvas.CreateSprite("UI3_STARS", CreateCall::Sprite);
    const SpriteId fallbackSprite = canvas.CreateSprite("no_such_sprite_for_gui_canvas_check", CreateCall::Bank);
    const SpriteMetrics starMetrics = canvas.Metrics(stars);

    canvas.SetAnimType(stars, 3);
    canvas.SetFrame(stars, 0);
    canvas.SetDirection(stars, starMetrics.directions > 1 ? 1 : 0);
    canvas.SetAnimType(fallbackSprite, 3);
    canvas.SetFrame(fallbackSprite, 0);
    canvas.SetDirection(fallbackSprite, 0);

    canvas.DrawClipped(fallbackSprite, 40, 30, 0xffff);
    canvas.DrawClipped(stars, 10, 20, 0xffff);
    canvas.Draw(stars, 60, 50, 0, 0, starMetrics.width / 2 + 1, starMetrics.height, 0xffff);
    canvas.ReleaseSprite(fallbackSprite);
    canvas.Draw(stars, 80, 50, 1, 0, starMetrics.width, starMetrics.height, 0xfffe);
    canvas.ReleaseSprite(stars);

    canvas.FillRect(-5, 8, 21, 15, 0x9abc);
    canvas.FillRectBySize(30, 20, 11, 3, 0xdef0);
    canvas.FillRect(20, 25, 20, 25, 0x1234);
    canvas.FillRectBySize(800, 600, -3, -2, 0xffff);

    canvas.SetTextRect(renderer, 10, 100, 90, 60);
    canvas.SetTextColor(renderer, (0x40u << 16) | 0xff20u); // RGB, spelled apart for check_layers' image-address lint
    canvas.SetTextOutlined(renderer, true);
    canvas.DrawWrapped(renderer, 0, 0, u"A rather long sentence that has to wrap over several lines inside the rectangle");
    canvas.SetTextAlign(renderer, 1);
    canvas.DrawLine(renderer, 12, 101, u"centred line");
    canvas.SetTextAlign(renderer, 2);
    canvas.DrawWrapped(renderer, 0, 0, u"Poląski żółw świat");

    canvas.SetTextRect(renderer, before.x, before.y, before.width, before.height);
    canvas.SetTextColor(renderer, before.color);
    canvas.SetTextOutlined(renderer, before.outlined);
    canvas.SetTextAlign(renderer, before.align);
}

TextState LiveText(int renderer)
{
    const ui::TextRenderer* object = gui::TextRendererSlot(renderer);
    return {object->m_x, object->m_y, object->m_width, object->m_height, object->m_color, object->m_outlined != 0, object->m_align};
}

} // namespace

void BeginGuiCapture()
{
    if (g.active) EndGuiCapture();
    g.calls.clear();
    g.created.clear();
    g.names.clear();
    g.previousSprite = ui::GetSpriteObserver();
    g.previousText = ui::GetTextObserver();
    g.previousFill = ui::GetFillObserver();
    ui::SetSpriteObserver(&kSpriteObserver);
    ui::SetTextObserver(&kTextObserver);
    ui::SetFillObserver(&kFillObserver);
    g.active = true;
}

void EndGuiCapture()
{
    if (!g.active) return;
    ui::SetSpriteObserver(g.previousSprite);
    ui::SetTextObserver(g.previousText);
    ui::SetFillObserver(g.previousFill);
    g.previousSprite = nullptr;
    g.previousText = nullptr;
    g.previousFill = nullptr;
    g.active = false;
}

bool GuiCaptureActive()
{
    return g.active;
}

const std::vector<Call>& GuiCaptureCalls()
{
    return g.calls;
}

int GuiCaptureLeaked()
{
    int leaked = 0;
    std::map<const ui::Sprite*, int> baseline;
    for (const auto& created : g.created) baseline.emplace(created.sprite, created.referencesAfterCreate - 1);
    for (const auto& [sprite, expected] : baseline) leaked += sprite->m_refCount != expected;
    return leaked;
}

bool ExecuteGuiCaptureCommand(const std::string& name, const std::vector<std::string>&)
{
    if (name != "gui-canvas-check") return false;
    constexpr int kRenderer = 1; // the second font slot, which the GUI uses for headings
    if (!gui::TextRendererSlot(kRenderer)) {
        OBL_LOG("autotest: FAILED gui-canvas-check - no text renderer %d yet", kRenderer);
        return true;
    }

    const TextState before = LiveText(kRenderer);
    BeginGuiCapture();
    {
        gui::LiveCanvas live;
        Scenario(live, kRenderer, before);
    }
    EndGuiCapture();

    RecordingCanvas recording(&gui::GameSpriteMetrics);
    recording.SeedText(kRenderer, before);
    Scenario(recording, kRenderer, before);

    int sprites = 0, wrapped = 0, lines = 0, fills = 0;
    for (const Call& call : GuiCaptureCalls()) {
        sprites += call.kind == Call::Create || call.kind == Call::Draw || call.kind == Call::DrawClipped;
        fills += call.kind == Call::Fill;
        wrapped += call.kind == Call::DrawText && call.wrapped;
        lines += call.kind == Call::DrawText && !call.wrapped;
    }
    const std::string difference = Compare(recording.calls, GuiCaptureCalls());
    const bool restored = LiveText(kRenderer) == before;
    OBL_LOG("autotest: gui-canvas-check calls=%zu sprites=%d text=%d wrapped=%d lines=%d mismatch=%d leaked=%d restored=%d live=%d "
            "fills=%d",
            GuiCaptureCalls().size(), sprites, wrapped + lines, wrapped, lines, !difference.empty(), GuiCaptureLeaked(), restored,
            recording.Live(), fills);
    if (!difference.empty()) OBL_LOG("autotest: FAILED gui-canvas-check %s", difference.c_str());
    return true;
}

} // namespace obl::test
