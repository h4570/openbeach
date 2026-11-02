#include "core/char16.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/renderer_api.h"
#include "game/ui/sprite_instance_render.h"
#include "game/ui/text_renderer.h"
#include "gui_canvas_live.h"
#include "game/ui/sprite.h"

#include "core/ascii_case.h"
#include "game/game/game_accessors.h"

namespace obl::gui {
namespace {

using Instance = ui::SpriteInstance;

ui::SpriteBank* Bank()
{
    return ui::GetSpriteBank();
}

ui::GuiManager* Gui()
{
    return game::GetGuiManager();
}

const ui::Sprite* FindSprite(const ui::SpriteBank* bank, const char* name)
{
    for (int i = 0; i < bank->m_spriteCount; ++i)
        if (ascii::EqualsNoCase(bank->m_sprites[i]->m_name, name)) return bank->m_sprites[i];
    return nullptr;
}

} // namespace

gui_canvas::SpriteMetrics GameSpriteMetrics(const char* name)
{
    const ui::SpriteBank* bank = Bank();
    const ui::Sprite* sprite = FindSprite(bank, name);
    if (!sprite) sprite = FindSprite(bank, "dummy");
    if (!sprite || sprite->m_animCount <= 0) return {};
    const ui::SpriteAnim* anim = sprite->m_anims[0];
    return {anim->m_directions, anim->m_width, anim->m_height};
}

ui::TextRenderer* TextRendererSlot(int renderer)
{
    return renderer >= 0 && renderer < gui_canvas::kTextRenderers ? Gui()->m_textRenderers[renderer] : nullptr;
}

int TextRendererIndex(const void* object)
{
    for (int i = 0; i < gui_canvas::kTextRenderers; ++i)
        if (Gui()->m_textRenderers[i] == object) return i;
    return -1;
}

LiveCanvas::~LiveCanvas()
{
    for (size_t i = 0; i < m_instances.size(); ++i)
        if (m_owned[i]) ReleaseSprite(static_cast<gui_canvas::SpriteId>(i));
}

gui_canvas::SpriteId LiveCanvas::CreateSprite(const char* name, gui_canvas::CreateCall call)
{
    auto* bank = static_cast<ui::SpriteBank*>(Bank());
    m_instances.push_back(call == gui_canvas::CreateCall::Bank ? bank->CreateObservedInstance(name) : bank->CreateInstance(name));
    m_owned.push_back(true);
    return static_cast<gui_canvas::SpriteId>(m_instances.size() - 1);
}

gui_canvas::SpriteId LiveCanvas::BorrowSprite(Instance* instance)
{
    m_instances.push_back(instance);
    m_owned.push_back(false);
    return static_cast<gui_canvas::SpriteId>(m_instances.size() - 1);
}

Instance* LiveCanvas::InstanceOf(gui_canvas::SpriteId sprite) const
{
    return sprite >= 0 && sprite < static_cast<gui_canvas::SpriteId>(m_instances.size()) ? m_instances[sprite] : nullptr;
}

gui_canvas::SpriteMetrics LiveCanvas::Metrics(gui_canvas::SpriteId sprite) const
{
    const Instance* instance = InstanceOf(sprite);
    if (!instance) return {};
    const ui::SpriteAnim* anim = instance->m_sprite->m_anims[instance->m_animIndex];
    return {anim->m_directions, anim->m_width, anim->m_height};
}

void LiveCanvas::SetAnimType(gui_canvas::SpriteId sprite, int type)
{
    if (auto* instance = InstanceOf(sprite)) instance->m_animType = type;
}

void LiveCanvas::SetFrame(gui_canvas::SpriteId sprite, int frame)
{
    if (auto* instance = InstanceOf(sprite)) instance->m_frameIndex = frame;
}

void LiveCanvas::SetDirection(gui_canvas::SpriteId sprite, int direction)
{
    if (auto* instance = InstanceOf(sprite)) instance->m_direction = direction;
}

void LiveCanvas::DrawClippedAt(gui_canvas::SpriteId sprite, int x, int y, uint32_t depth, float time)
{
    if (auto* instance = InstanceOf(sprite)) (instance)->DrawClipped(x, y, depth, time);
}

void LiveCanvas::Draw(gui_canvas::SpriteId sprite, int x, int y, int left, int top, int right, int bottom, uint32_t depth)
{
    if (auto* instance = InstanceOf(sprite)) (instance)->Draw(x, y, left, top, right, bottom, depth);
}

void LiveCanvas::ReleaseSprite(gui_canvas::SpriteId sprite)
{
    auto* instance = InstanceOf(sprite);
    if (!instance) return;
    (static_cast<ui::SpriteBank*>(Bank()))->ReleaseInstance(instance);
    m_instances[sprite] = nullptr;
}

void LiveCanvas::SetTextRect(int renderer, int x, int y, int width, int height)
{
    if (auto* object = TextRendererSlot(renderer)) ui::font_text::SetRect(object, x, y, width, height);
}

void LiveCanvas::SetTextColor(int renderer, uint32_t rgb)
{
    if (auto* object = TextRendererSlot(renderer)) ui::font_text::SetColor(object, rgb);
}

void LiveCanvas::SetTextOutlined(int renderer, bool outlined)
{
    if (auto* object = TextRendererSlot(renderer)) ui::font_text::SetOutlined(object, outlined ? 1 : 0);
}

void LiveCanvas::SetTextAlign(int renderer, int align)
{
    if (auto* object = TextRendererSlot(renderer)) ui::font_text::SetAlign(object, align);
}

void LiveCanvas::DrawWrapped(int renderer, int x, int y, const Char16* text)
{
    if (auto* object = TextRendererSlot(renderer)) ui::font_text::DrawWrapped(object, x, y, text);
}

void LiveCanvas::DrawLine(int renderer, int x, int y, const Char16* text)
{
    if (auto* object = TextRendererSlot(renderer)) ui::font_text::DrawLine(object, x, y, text);
}

void LiveCanvas::FillRect(int left, int top, int right, int bottom, uint16_t color)
{
    const int rect[] = {left, top, right, bottom};
    obl::ui::renderer::FillRect(game::GetRenderer(), rect, color);
}

void LiveCanvas::FillRectBySize(int x, int y, int width, int height, uint16_t color)
{
    const int rect[] = {x, y, width, height};
    obl::ui::renderer::FillRectBySize(game::GetRenderer(), rect, color);
}

} // namespace obl::gui
