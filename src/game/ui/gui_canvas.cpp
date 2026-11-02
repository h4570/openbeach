#include "core/char16.h"
#include "game/ui/gui_canvas.h"

#include <cstdio>

namespace obl::gui_canvas {

namespace {

// Text goes into failure messages: keep it printable and short, non-ASCII code units as \uXXXX.
std::string Printable(const Text16& text)
{
    std::string out;
    for (Char16 ch : text) {
        char piece[8];
        if (ch >= 0x20 && ch < 0x7f)
            std::snprintf(piece, sizeof(piece), "%c", static_cast<char>(ch));
        else
            std::snprintf(piece, sizeof(piece), "\\u%04x", static_cast<unsigned>(ch));
        out += piece;
        if (out.size() > 80) {
            out += "...";
            break;
        }
    }
    return out;
}

} // namespace

std::string Describe(const Call& c)
{
    char text[320];
    switch (c.kind) {
    case Call::Create:
        std::snprintf(text, sizeof(text), "create %s %s", c.name.c_str(), c.create == CreateCall::Bank ? "bank" : "sprite");
        break;
    case Call::DrawClipped:
        std::snprintf(text, sizeof(text), "clipped %s anim=%d frame=%d dir=%d at (%d,%d) depth=%u time=%g", c.name.c_str(), c.animType,
                c.frame, c.direction, c.x, c.y, c.depth, static_cast<double>(c.time));
        break;
    case Call::Draw:
        std::snprintf(text, sizeof(text), "draw %s anim=%d frame=%d dir=%d at (%d,%d) depth=%u rect (%d,%d,%d,%d)", c.name.c_str(),
                c.animType, c.frame, c.direction, c.x, c.y, c.depth, c.left, c.top, c.right, c.bottom);
        break;
    case Call::DrawText:
        std::snprintf(text, sizeof(text), "%s renderer=%d at (%d,%d) rect (%d,%d,%d,%d) color=%06x outlined=%d align=%d \"%s\"",
                c.wrapped ? "wrapped" : "line", c.renderer, c.x, c.y, c.state.x, c.state.y, c.state.width, c.state.height, c.state.color,
                c.state.outlined, c.state.align, Printable(c.text).c_str());
        break;
    case Call::Fill:
        std::snprintf(text, sizeof(text), "%s (%d,%d,%d,%d) color=%04x", c.xywh ? "fill-xywh" : "fill", c.left, c.top, c.right, c.bottom,
                c.color);
        break;
    }
    return text;
}

std::string Compare(const std::vector<Call>& expected, const std::vector<Call>& actual)
{
    const size_t common = expected.size() < actual.size() ? expected.size() : actual.size();
    for (size_t i = 0; i < common; ++i)
        if (!(expected[i] == actual[i]))
            return "call " + std::to_string(i) + " differs: expected [" + Describe(expected[i]) + "] got [" + Describe(actual[i]) + "]";
    if (expected.size() != actual.size()) {
        const bool extra = actual.size() > expected.size();
        const Call& first = extra ? actual[common] : expected[common];
        return "expected " + std::to_string(expected.size()) + " calls, got " + std::to_string(actual.size()) + "; first " +
                (extra ? "unexpected [" : "missing [") + Describe(first) + "]";
    }
    return {};
}

void RecordingCanvas::SeedText(int renderer, const TextState& state)
{
    if (TextState* slot = Renderer(renderer)) *slot = state;
}

TextState RecordingCanvas::Text(int renderer) const
{
    return renderer >= 0 && renderer < kTextRenderers ? m_text[renderer] : TextState{};
}

RecordingCanvas::Instance* RecordingCanvas::Get(SpriteId sprite)
{
    return sprite >= 0 && sprite < static_cast<SpriteId>(m_instances.size()) ? &m_instances[sprite] : nullptr;
}

const RecordingCanvas::Instance* RecordingCanvas::Get(SpriteId sprite) const
{
    return sprite >= 0 && sprite < static_cast<SpriteId>(m_instances.size()) ? &m_instances[sprite] : nullptr;
}

TextState* RecordingCanvas::Renderer(int renderer)
{
    return renderer >= 0 && renderer < kTextRenderers ? &m_text[renderer] : nullptr;
}

SpriteId RecordingCanvas::CreateSprite(const char* name, CreateCall call)
{
    Call c;
    c.kind = Call::Create;
    c.create = call;
    c.name = name;
    calls.push_back(c);
    Instance instance;
    instance.name = name;
    if (m_metricsLookup) instance.metrics = m_metricsLookup(name);
    m_instances.push_back(instance);
    ++created;
    return static_cast<SpriteId>(m_instances.size() - 1);
}

SpriteMetrics RecordingCanvas::Metrics(SpriteId sprite) const
{
    const Instance* instance = Get(sprite);
    return instance ? instance->metrics : SpriteMetrics{};
}

void RecordingCanvas::SetAnimType(SpriteId sprite, int type)
{
    if (Instance* instance = Get(sprite)) instance->animType = type;
}

void RecordingCanvas::SetFrame(SpriteId sprite, int frame)
{
    if (Instance* instance = Get(sprite)) instance->frame = frame;
}

void RecordingCanvas::SetDirection(SpriteId sprite, int direction)
{
    if (Instance* instance = Get(sprite)) instance->direction = direction;
}

void RecordingCanvas::DrawClippedAt(SpriteId sprite, int x, int y, uint32_t depth, float time)
{
    const Instance* instance = Get(sprite);
    if (!instance) return;
    Call c;
    c.kind = Call::DrawClipped;
    c.name = instance->name;
    c.animType = instance->animType;
    c.frame = instance->frame;
    c.direction = instance->direction;
    c.x = x;
    c.y = y;
    c.depth = depth;
    c.time = time;
    calls.push_back(c);
}

void RecordingCanvas::Draw(SpriteId sprite, int x, int y, int left, int top, int right, int bottom, uint32_t depth)
{
    const Instance* instance = Get(sprite);
    if (!instance) return;
    Call c;
    c.kind = Call::Draw;
    c.name = instance->name;
    c.animType = instance->animType;
    c.frame = instance->frame;
    c.direction = instance->direction;
    c.x = x;
    c.y = y;
    c.depth = depth;
    c.left = left;
    c.top = top;
    c.right = right;
    c.bottom = bottom;
    calls.push_back(c);
}

void RecordingCanvas::ReleaseSprite(SpriteId sprite)
{
    Instance* instance = Get(sprite);
    if (!instance || instance->released) return;
    instance->released = true;
    ++released;
}

void RecordingCanvas::SetTextRect(int renderer, int x, int y, int width, int height)
{
    if (TextState* slot = Renderer(renderer)) {
        slot->x = x;
        slot->y = y;
        slot->width = width;
        slot->height = height;
    }
}

void RecordingCanvas::SetTextColor(int renderer, uint32_t rgb)
{
    if (TextState* slot = Renderer(renderer)) slot->color = rgb;
}

void RecordingCanvas::SetTextOutlined(int renderer, bool outlined)
{
    if (TextState* slot = Renderer(renderer)) slot->outlined = outlined;
}

void RecordingCanvas::SetTextAlign(int renderer, int align)
{
    if (TextState* slot = Renderer(renderer)) slot->align = align;
}

void RecordingCanvas::DrawText(int renderer, bool wrapped, int x, int y, const Char16* text)
{
    Call c;
    c.kind = Call::DrawText;
    c.wrapped = wrapped;
    c.renderer = renderer;
    c.x = x;
    c.y = y;
    c.state = Text(renderer);
    if (text) c.text = text;
    calls.push_back(c);
}

void RecordingCanvas::DrawWrapped(int renderer, int x, int y, const Char16* text)
{
    DrawText(renderer, true, x, y, text);
}

void RecordingCanvas::DrawLine(int renderer, int x, int y, const Char16* text)
{
    DrawText(renderer, false, x, y, text);
}

SpriteId RecordingCanvas::SeedSprite(const char* name, SpriteMetrics metrics, int animType, int frame, int direction)
{
    m_instances.push_back({name, metrics, animType, frame, direction, false});
    ++created;
    return static_cast<SpriteId>(m_instances.size() - 1);
}

void RecordingCanvas::FillRect(int left, int top, int right, int bottom, uint16_t color)
{
    Call call;
    call.kind = Call::Fill;
    call.left = left;
    call.top = top;
    call.right = right;
    call.bottom = bottom;
    call.color = color;
    calls.push_back(call);
}

void RecordingCanvas::FillRectBySize(int x, int y, int width, int height, uint16_t color)
{
    FillRect(x, y, width, height, color);
    calls.back().xywh = true;
}

} // namespace obl::gui_canvas
