#include "game/ui/extra_scenarios_screen.h"

#include "core/log.h"
#include "core/pack_path.h"
#include "game/game/game.h"
#include "game/game/game_accessors.h"
#include "game/game/game_api.h"
#include "game/tables/text_load.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/image.h"
#include "game/ui/own_layer.h"
#include "game/ui/own_widgets.h"
#include "game/ui/renderer_api.h"
#include "game/ui/renderer_ops.h"
#include "game/ui/renderer_types.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/text_table.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace obl::gui::extra_scenarios_screen {

namespace {

namespace own = own_widgets;

constexpr int kHeaderH = 48;
constexpr int kFooterH = 52;
constexpr int kTopY = 56;
constexpr int kListX0 = 12;
constexpr int kListX1 = 424;
constexpr int kCardX0 = 436;
constexpr int kCardX1 = 788;
constexpr int kContentBottom = 540;
constexpr int kSetHeaderH = 24;
constexpr int kRowH = 42;
constexpr int kRowGap = 4;
constexpr uint32_t kAmber = 0xc8820a;

std::u16string Utf8ToEngine(std::string_view utf8)
{
    std::u16string wide;
    wide.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        const auto c = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 6) | (static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 12) |
                    ((static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu) << 6) | (static_cast<uint8_t>(utf8[i + 3]) & 0x3Fu);
            i += 4;
        } else {
            cp = '?';
            ++i;
        }
        if (cp <= 0xFFFFu) wide.push_back(static_cast<char16_t>(cp));
    }
    return text_load::EngineValue(wide, false);
}

std::u16string LookupKey(const std::string& key)
{
    if (key.empty()) return {};
    const Char16* text = ui::text_table::Lookup(key.c_str());
    if (text && *text) return std::u16string(text);
    return Utf8ToEngine(key);
}

std::u16string FormatOne(const Char16* fmt, const std::u16string& arg)
{
    if (!fmt) return arg;
    std::u16string out;
    for (size_t i = 0; fmt[i]; ++i) {
        if (fmt[i] == u'%' && fmt[i + 1] == u's') {
            out += arg;
            ++i;
        } else {
            out.push_back(fmt[i]);
        }
    }
    return out;
}

uint16_t BlendPicturePixel(uint16_t background, const uint8_t* rgb, unsigned alpha, bool rgb555)
{
    const auto blend = [alpha](unsigned foreground, unsigned back) { return (foreground * alpha + back * (255 - alpha)) / 255; };
    const unsigned red = ((background >> (rgb555 ? 10 : 11)) & 31) * 255 / 31;
    const unsigned greenMax = rgb555 ? 31 : 63;
    const unsigned green = ((background >> 5) & greenMax) * 255 / greenMax;
    const unsigned blue = (background & 31) * 255 / 31;
    return renderer_ops::PackColor(rgb555, blend(rgb[0], red), blend(rgb[1], green), blend(rgb[2], blue));
}

template<class Pixel>
void BlitImage(const image::Image& img, Pixel* target, int pitch, int surfW, int surfH, int x, int y, int maxW, int maxH, bool rgb555)
{
    const int drawW = std::min(img.width, maxW);
    const int drawH = std::min(img.height, maxH);
    const auto span = renderer_ops::ClipFill(surfW, surfH, {x, y, x + drawW, y + drawH});
    if (span.width <= 0 || span.height <= 0) return;
    const int left = span.x - x;
    const int top = span.y - y;
    const int channels = img.bpp / 8;
    for (int row = 0; row < span.height; ++row) {
        const auto* src = img.pixels.data() + (static_cast<size_t>(top + row) * img.width + left) * channels;
        auto* dst = target + static_cast<size_t>(span.y + row) * pitch + span.x;
        for (int col = 0; col < span.width; ++col, src += channels, ++dst) {
            const uint8_t gray[] = {*src, *src, *src};
            const auto* rgb = channels != 1 ? src : img.palette.empty() ? gray : img.palette.data() + *src * 3;
            const unsigned alpha = channels == 4 ? src[3] : 255;
            if (alpha == 0) continue;
            const uint16_t color = alpha == 255 ? renderer_ops::PackColor(rgb555, rgb[0], rgb[1], rgb[2])
                                                : BlendPicturePixel(static_cast<uint16_t>(*dst), rgb, alpha, rgb555);
            if constexpr (sizeof(Pixel) == sizeof(uint32_t))
                *dst = (*dst & 0xffff0000u) | color;
            else
                *dst = color;
        }
    }
}

void DrawScenarioPictureBox(int x0, int y0, int x1, int y1, const scenario_load::ScenarioPicture& picture)
{
    const int boxW = x1 - x0;
    const int boxH = y1 - y0;
    if (boxW <= 0 || boxH <= 0) return;

    if (const auto* sprite = std::get_if<scenario_load::SpritePicture>(&picture)) {
        auto* bank = static_cast<ui::SpriteBank*>(ui::GetSpriteBank());
        if (!bank) return;
        ui::SpriteInstance* inst = bank->CreateObservedInstance(sprite->sprite.c_str());
        if (!inst) return;
        inst->m_animType = 3;
        inst->m_frameIndex = sprite->frame;
        int drawX = x0;
        int drawY = y0;
        if (inst->m_sprite && inst->m_sprite->m_anims && inst->m_sprite->m_anims[inst->m_animIndex]) {
            const ui::SpriteAnim* anim = inst->m_sprite->m_anims[inst->m_animIndex];
            if (anim->m_width > 0 && anim->m_width < boxW) drawX = x0 + (boxW - anim->m_width) / 2;
            if (anim->m_height > 0 && anim->m_height < boxH) drawY = y0 + (boxH - anim->m_height) / 2;
        }
        inst->DrawClipped(drawX, drawY, 0x100u, 0.0f);
        bank->ReleaseInstance(inst);
        return;
    }

    const std::string& relPath = std::get<std::string>(picture);
    if (relPath.empty()) return;
    const std::filesystem::path full = pack_path::Resolve(pack_path::Selected(), relPath);
    image::Image img;
    std::string error;
    if (!image::LoadPng(full, img, error) || img.width <= 0 || img.height <= 0) return;

    ui::Renderer* renderer = game::GetRenderer();
    if (!renderer) return;
    const int drawX = img.width < boxW ? x0 + (boxW - img.width) / 2 : x0;
    const int drawY = img.height < boxH ? y0 + (boxH - img.height) / 2 : y0;
    const bool rgb555 = renderer->m_rgb555 != 0;
    if (renderer->m_directMode) {
        if (uint16_t* bits = ui::renderer::GetBackBufferBits(renderer)) {
            const auto* surface = ui::SurfaceAt(*renderer, renderer->m_activeSurface);
            BlitImage(img, bits, surface->m_pitch / 2, surface->m_width, surface->m_height, drawX - surface->m_originX,
                    drawY - surface->m_originY, boxW, boxH, rgb555);
            ui::renderer::UnlockSurface(renderer);
        }
    } else if (renderer->m_workBuffer) {
        BlitImage(img, renderer->m_workBuffer, renderer->m_width, renderer->m_width, renderer->m_height, drawX, drawY, boxW, boxH, rgb555);
    }
}

std::u16string LockReason(const game::Game& g, scenario_load::ScenarioRef ref)
{
    const auto& registry = g.m_scenarioRegistry;
    const auto& progress = g.m_scenarioProgress;
    const auto* entry = registry.Find(ref);
    if (!entry) return {};
    const auto* set = registry.FindSet(entry->setIndex);
    if (!set) return {};

    const Char16* fmt = ui::text_table::Lookup("OBL_EXTRA_LOCKED_WIN_FIRST");

    for (const std::string& reqId : set->requiresCompleted) {
        const auto reqRef = registry.FindById(reqId, scenario_load::ScenarioKind::Campaign);
        if (!reqRef || !progress.IsComplete(registry, *reqRef)) {
            const std::u16string reqTitle = reqRef ? LookupKey(registry.Get(*reqRef).textKeys.title) : Utf8ToEngine(reqId);
            return FormatOne(fmt, reqTitle);
        }
    }

    if (entry->IsSandbox()) {
        if (set->sandboxUnlock == scenario_load::SandboxUnlock::SetCompleted) {
            for (const scenario_load::ScenarioRef sRef : set->scenarios) {
                if (!progress.IsComplete(registry, sRef)) {
                    return FormatOne(fmt, LookupKey(registry.Get(sRef).textKeys.title));
                }
            }
        }
        return {};
    }

    if (set->unlock == scenario_load::SetUnlock::Sequential) {
        for (size_t i = 0; i < set->scenarios.size(); ++i) {
            if (set->scenarios[i] != ref) continue;
            if (i > 0) {
                const scenario_load::ScenarioRef prevRef = set->scenarios[i - 1];
                return FormatOne(fmt, LookupKey(registry.Get(prevRef).textKeys.title));
            }
            break;
        }
    }
    return {};
}

struct ListRow {
    bool isHeader = false;
    int32_t setIndex = -1;
    scenario_load::ScenarioRef ref{};
};

struct Target {
    std::string name;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::function<void()> onClick;
};

struct GuiExtraScenariosPanel;
GuiExtraScenariosPanel* g_openPanel = nullptr;

struct GuiExtraScenariosPanel final : ui::GuiLeaf<ui::GuiWidget> {
    GuiExtraScenariosPanel(int x0, int y0, int x1, int y1) : GuiLeaf(x0, y0, x1, y1) { m_type = kExtraScenariosPanelType; }

    ~GuiExtraScenariosPanel() override
    {
        if (g_openPanel == this) g_openPanel = nullptr;
    }

    const char* ClassName() const override { return "GuiExtraScenariosPanel"; }

    void SetVisible(uint32_t visible) override
    {
        VisibleInvalidate(visible);
        if (visible) {
            g_openPanel = this;
            RefreshEntries();
        } else if (g_openPanel == this) {
            g_openPanel = nullptr;
        }
    }

    void Update(float) override
    {
        if (m_pendingBack) {
            m_pendingBack = false;
            if (ui::GuiManager* gui = game::GetGuiManager()) {
                gui->CloseAllDialogs();
                gui->ShowDialog("ScenarioScreen", nullptr, false);
            }
            return;
        }
        if (m_pendingPlay) {
            m_pendingPlay = false;
            StartSelected();
        }
    }

    bool MouseLeft(int x, int y, int state) override
    {
        if (!m_visible || state != 1 || !HitTestBase(x, y)) return false;
        for (size_t i = 0; i < m_targets.size(); ++i) {
            const Target& target = m_targets[i];
            if (x >= target.x0 && x < target.x1 && y >= target.y0 && y < target.y1) {
                auto action = target.onClick;
                if (action) action();
                return true;
            }
        }
        return true;
    }

    bool MouseWheel(int x, int y, int delta) override
    {
        if (!m_visible || !HitTestBase(x, y) || !delta) return false;
        const int maxScroll = MaxScroll();
        if (maxScroll <= 0) return false;
        m_scroll = std::clamp(m_scroll + (delta > 0 ? -1 : 1), 0, maxScroll);
        MarkDirty();
        return true;
    }

    void Draw() override
    {
        if (!m_visible || !m_dirty) return;
        m_drawing = true;
        Layout();
        m_drawing = false;
        m_dirty = 0;
    }

    void MarkDirty()
    {
        m_dirty = 1;
        if (ui::GuiManager* gui = game::GetGuiManager()) gui->InvalidateDialog(own_layer::kExtraScenariosDialog, 0);
    }

    void RefreshEntries()
    {
        m_rows.clear();
        m_scenarioIndices.clear();
        m_scroll = 0;
        const game::Game* g = game::GetGame();
        if (!g) return;
        const bool sandbox = g->m_sandbox != 0;
        const auto& sets = g->m_scenarioRegistry.Sets();
        for (size_t s = 0; s < sets.size(); ++s) {
            const auto& refs = sandbox ? sets[s].sandboxes : sets[s].scenarios;
            if (refs.empty()) continue;
            m_rows.push_back({true, static_cast<int32_t>(s), {}});
            for (const scenario_load::ScenarioRef ref : refs) {
                m_scenarioIndices.push_back(m_rows.size());
                m_rows.push_back({false, static_cast<int32_t>(s), ref});
            }
        }
        // Keep the current selection if it is one of our rows, else pick the first entry.
        bool found = false;
        for (const size_t idx : m_scenarioIndices) {
            if (m_rows[idx].ref == g->m_selectedScenario) {
                m_selected = m_rows[idx].ref;
                found = true;
                break;
            }
        }
        if (!found && !m_scenarioIndices.empty()) {
            m_selected = m_rows[m_scenarioIndices.front()].ref;
            SyncSelectionToGame();
        }
        OBL_LOG("extra scenarios: opened mode=%s sets=%zu entries=%zu", sandbox ? "sandbox" : "campaign", sets.size(),
                m_scenarioIndices.size());
        MarkDirty();
    }

    void SyncSelectionToGame()
    {
        game::Game* g = game::GetGame();
        if (!g || !m_selected.IsValid()) return;
        const bool sandbox = g->m_sandbox != 0;
        if (g->m_scenarioProgress.IsSelectable(g->m_scenarioRegistry, m_selected, sandbox)) {
            g->m_selectedScenario = m_selected;
        }
    }

    void SelectScenario(scenario_load::ScenarioRef ref)
    {
        m_selected = ref;
        SyncSelectionToGame();
        MarkDirty();
    }

    void MoveSelection(int delta)
    {
        if (m_scenarioIndices.empty()) return;
        size_t pos = 0;
        for (size_t i = 0; i < m_scenarioIndices.size(); ++i) {
            if (m_rows[m_scenarioIndices[i]].ref == m_selected) {
                pos = i;
                break;
            }
        }
        const int next = std::clamp(static_cast<int>(pos) + delta, 0, static_cast<int>(m_scenarioIndices.size()) - 1);
        SelectScenario(m_rows[m_scenarioIndices[static_cast<size_t>(next)]].ref);
        EnsureRowVisible(static_cast<int>(m_scenarioIndices[static_cast<size_t>(next)]));
    }

    void RequestPlay()
    {
        const game::Game* g = game::GetGame();
        if (!g || !m_selected.IsValid()) return;
        const bool sandbox = g->m_sandbox != 0;
        if (!g->m_scenarioProgress.IsSelectable(g->m_scenarioRegistry, m_selected, sandbox)) return;
        m_pendingPlay = true;
    }

    void StartSelected()
    {
        game::Game* g = game::GetGame();
        ui::GuiManager* gui = game::GetGuiManager();
        if (!g || !gui || !m_selected.IsValid()) return;
        const bool sandbox = g->m_sandbox != 0;
        if (!g->m_scenarioProgress.IsSelectable(g->m_scenarioRegistry, m_selected, sandbox)) return;
        g->m_selectedScenario = m_selected;
        gui->CloseAllDialogs();
        gui->ShowDialog("LoadingScreen1", nullptr, false);
        g->m_nextState = g->m_states[static_cast<size_t>(game::GameStateId::WorldTest)].get();
    }

    bool HandleKey(int scancode)
    {
        if (scancode == SDL_SCANCODE_ESCAPE) {
            m_pendingBack = true;
            return true;
        }
        if (scancode == SDL_SCANCODE_UP) {
            MoveSelection(-1);
            return true;
        }
        if (scancode == SDL_SCANCODE_DOWN) {
            MoveSelection(1);
            return true;
        }
        if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER) {
            RequestPlay();
            return true;
        }
        return false;
    }

    int MaxScroll() const
    {
        const int availH = kContentBottom - kTopY - 32;
        int totalH = 0;
        for (const ListRow& r : m_rows) totalH += (r.isHeader ? kSetHeaderH : kRowH) + kRowGap;
        if (totalH <= availH) return 0;
        return std::max(0, static_cast<int>(m_rows.size()) - 1);
    }

    void EnsureRowVisible(int rowIdx)
    {
        if (rowIdx < m_scroll) m_scroll = rowIdx;
    }

    void AddTarget(std::string name, int x0, int y0, int x1, int y1, std::function<void()> onClick)
    {
        m_targets.push_back({std::move(name), x0, y0, x1, y1, std::move(onClick)});
    }

    void DrawButton(std::string targetName, int x0, int y0, int x1, int y1, const Char16* label, bool enabled, bool primary,
            std::function<void()> onClick)
    {
        const uint32_t fill = !enabled ? m_pal.sand : (primary ? m_pal.sea : m_pal.foam);
        const uint32_t border = !enabled ? m_pal.border : (primary ? m_pal.seaDark : m_pal.border);
        const uint32_t textRgb = !enabled ? m_pal.grey : (primary ? m_pal.white : m_pal.ink);
        if (m_drawing) {
            own::Box(x0, y0, x1, y1, m_round, fill, border);
            const int font = 2;
            const int ty = y0 + (y1 - y0) / 2 - own::InkCentre(font);
            own::Text(font, x0 + 8, ty, x1 - x0 - 16, label, textRgb, 2);
        }
        AddTarget(std::move(targetName), x0, y0, x1, y1, enabled ? std::move(onClick) : nullptr);
    }

    void Layout()
    {
        m_targets.clear();
        const own::PackLook& look = own::GetPackLook();
        m_pal = look.palette;
        m_round = look.available ? 8 : 0;

        const int ox = m_x0;
        const int oy = m_y0;
        const int w = m_x1 - m_x0;
        const int h = m_y1 - m_y0;

        const game::Game* g = game::GetGame();
        if (!g) return;
        const bool sandbox = g->m_sandbox != 0;
        const auto& registry = g->m_scenarioRegistry;
        const auto& progress = g->m_scenarioProgress;

        if (m_drawing) {
            own::Fill(ox, oy, ox + w, oy + h, m_pal.sand);
            own::Fill(ox, oy, ox + w, oy + kHeaderH, m_pal.sea);
            own::Fill(ox, oy + kHeaderH, ox + w, oy + kHeaderH + 1, m_pal.seaDark);
            const Char16* title = ui::text_table::Lookup("OBL_EXTRA_SCENARIOS_TITLE");
            const int titleY = oy + kHeaderH / 2 - own::InkCentre(2);
            own::Text(2, ox + 16, titleY, w - 32, title, m_pal.white, 0);
        }

        // Left list pane
        const int listX0 = ox + kListX0;
        const int listX1 = ox + kListX1;
        const int listTop = oy + kTopY;
        const int listBottom = oy + kContentBottom;
        if (m_drawing) own::Box(listX0, listTop, listX1, listBottom, m_round, m_pal.foam, m_pal.border);

        const int maxScroll = MaxScroll();
        const int pagerH = maxScroll > 0 ? 30 : 0;
        const int rowsBottom = listBottom - pagerH - 6;
        int y = listTop + 8;
        for (int i = m_scroll; i < static_cast<int>(m_rows.size()); ++i) {
            const ListRow& row = m_rows[static_cast<size_t>(i)];
            const int rh = row.isHeader ? kSetHeaderH : kRowH;
            if (y + rh > rowsBottom) break;

            if (row.isHeader) {
                if (const auto* set = registry.FindSet(row.setIndex); set && m_drawing) {
                    const std::u16string setTitle = LookupKey(set->title);
                    own::Text(1, listX0 + 10, y + 4, listX1 - listX0 - 20, setTitle.c_str(), m_pal.seaDark, 0);
                    own::Fill(listX0 + 10, y + rh - 2, listX1 - 10, y + rh - 1, m_pal.border);
                }
            } else if (const auto* entry = registry.Find(row.ref)) {
                const bool selected = row.ref == m_selected;
                const bool selectable = progress.IsSelectable(registry, row.ref, sandbox);
                const bool complete = progress.IsComplete(registry, row.ref);
                const int rx0 = listX0 + 8;
                const int rx1 = listX1 - 8;
                const int ry0 = y;
                const int ry1 = y + rh;

                if (m_drawing) {
                    const uint32_t bg = selected ? m_pal.sea : m_pal.sand;
                    const uint32_t br = selected ? m_pal.seaDark : m_pal.border;
                    own::Box(rx0, ry0, rx1, ry1, m_round ? 6 : 0, bg, br);

                    const std::u16string title = LookupKey(entry->textKeys.title);
                    const uint32_t titleRgb = selected ? m_pal.white : (selectable ? m_pal.ink : m_pal.grey);
                    const int badgeW = 64;
                    own::Text(1, rx0 + 10, ry0 + 5, rx1 - rx0 - 20 - badgeW, title.c_str(), titleRgb, 0);

                    if (complete) {
                        const Char16* done = ui::text_table::Lookup("OBL_EXTRA_DONE");
                        own::Text(0, rx1 - badgeW - 8, ry0 + 6, badgeW, done, selected ? m_pal.white : m_pal.green, 1);
                    } else if (!selectable) {
                        const Char16* locked = ui::text_table::Lookup("OBL_EXTRA_LOCKED");
                        own::Text(0, rx1 - badgeW - 8, ry0 + 6, badgeW, locked, selected ? m_pal.white : m_pal.grey, 1);
                        const std::u16string reason = LockReason(*g, row.ref);
                        if (!reason.empty()) {
                            own::Text(0, rx0 + 10, ry0 + 22, rx1 - rx0 - 20, reason.c_str(), selected ? m_pal.foam : kAmber, 0);
                        }
                    }
                }
                const scenario_load::ScenarioRef ref = row.ref;
                AddTarget("select." + entry->id, rx0, ry0, rx1, ry1, [this, ref]() { SelectScenario(ref); });
            }
            y += rh + kRowGap;
        }

        if (maxScroll > 0) {
            const int py0 = listBottom - 30;
            const int py1 = listBottom - 4;
            DrawButton("page.prev", listX0 + 8, py0, listX0 + 88, py1, u"<", m_scroll > 0, false, [this]() {
                m_scroll = std::max(0, m_scroll - 3);
                MarkDirty();
            });
            DrawButton("page.next", listX1 - 88, py0, listX1 - 8, py1, u">", m_scroll < maxScroll, false, [this, maxScroll]() {
                m_scroll = std::min(maxScroll, m_scroll + 3);
                MarkDirty();
            });
        }

        // Right detail card
        const int cardX0 = ox + kCardX0;
        const int cardX1 = ox + kCardX1;
        const int cardTop = oy + kTopY;
        const int cardBottom = oy + kContentBottom;
        if (m_drawing) own::Box(cardX0, cardTop, cardX1, cardBottom, m_round, m_pal.foam, m_pal.border);

        const auto* selEntry = registry.Find(m_selected);
        const bool canPlay = selEntry && progress.IsSelectable(registry, m_selected, sandbox);
        if (selEntry && m_drawing) {
            const int pad = 12;
            const int innerW = cardX1 - cardX0 - 2 * pad;
            int cy = cardTop + pad;

            // Picture box (206x119 matches SCENARIO_PIC_LARGE)
            const int picH = 124;
            own::Box(cardX0 + pad, cy, cardX1 - pad, cy + picH, 0, m_pal.sand, m_pal.border);
            DrawScenarioPictureBox(cardX0 + pad + 2, cy + 2, cardX1 - pad - 2, cy + picH - 2, selEntry->pictures.large);
            cy += picH + 10;

            // Title
            const std::u16string title = LookupKey(selEntry->textKeys.title);
            own::Text(2, cardX0 + pad, cy, innerW, title.c_str(), m_pal.seaDark, 0);
            cy += own::LineHeight(2) + 4;

            // Status (MAINSS_SCENARIO_* in campaign mode) or lock reason
            if (!canPlay) {
                const std::u16string reason = LockReason(*g, m_selected);
                if (!reason.empty()) {
                    own::Text(1, cardX0 + pad, cy, innerW, reason.c_str(), kAmber, 0);
                    cy += own::LineHeight(1) + 6;
                }
            } else if (!sandbox) {
                const char* statusKey = "MAINSS_SCENARIO_NOTCOMPLETED";
                if (progress.IsComplete(registry, m_selected))
                    statusKey = "MAINSS_SCENARIO_COMPLETE";
                else if (progress.State(registry, m_selected) == scenario_progress::ScenarioState::AlwaysAvailable)
                    statusKey = "MAINSS_SCENARIO_ALWAYS";
                own::Text(1, cardX0 + pad, cy, innerW, ui::text_table::Lookup(statusKey), m_pal.green, 0);
                cy += own::LineHeight(1) + 6;
            }

            // Scenario description
            const std::u16string desc = LookupKey(selEntry->textKeys.description);
            const auto* set = registry.FindSet(selEntry->setIndex);
            const std::u16string setDesc = set && !set->description.empty() ? LookupKey(set->description) : std::u16string{};
            const int setReserve = setDesc.empty() ? 0 : 72;
            const int descH = std::max(0, cardBottom - pad - setReserve - cy);
            if (!desc.empty() && descH > 0) {
                own::TextWrapped(0, cardX0 + pad, cy, innerW, descH, desc.c_str(), m_pal.ink, 0);
            }

            // Set description at bottom of card
            if (!setDesc.empty()) {
                const int sy = cardBottom - pad - setReserve + 8;
                own::Fill(cardX0 + pad, sy - 6, cardX1 - pad, sy - 5, m_pal.border);
                own::TextWrapped(0, cardX0 + pad, sy, innerW, cardBottom - pad - sy, setDesc.c_str(), m_pal.grey, 0);
            }
        }

        // Footer bar: Back on left, Play on right
        const int footY0 = oy + h - kFooterH + 6;
        const int footY1 = oy + h - 8;
        DrawButton("back", ox + 12, footY0, ox + 172, footY1, ui::text_table::Lookup("OBL_MODS_BACK"), true, false,
                [this]() { m_pendingBack = true; });
        DrawButton("play", ox + w - 172, footY0, ox + w - 12, footY1, ui::text_table::Lookup("OBL_EXTRA_PLAY"), canPlay, true,
                [this]() { RequestPlay(); });
    }

    own::Palette m_pal;
    int m_round = 0;
    bool m_drawing = false;
    bool m_pendingBack = false;
    bool m_pendingPlay = false;
    int m_scroll = 0;
    scenario_load::ScenarioRef m_selected{};
    std::vector<ListRow> m_rows;
    std::vector<size_t> m_scenarioIndices;
    std::vector<Target> m_targets;
};

} // namespace

bool HasExtraScenarios()
{
    const game::Game* g = game::GetGame();
    if (!g) return false;
    for (const auto& entry : g->m_scenarioRegistry.Entries()) {
        if (entry.IsExtra()) return true;
    }
    return false;
}

bool HasEntriesForCurrentMode()
{
    const game::Game* g = game::GetGame();
    if (!g) return false;
    const bool sandbox = g->m_sandbox != 0;
    for (const auto& set : g->m_scenarioRegistry.Sets()) {
        if (!(sandbox ? set.sandboxes : set.scenarios).empty()) return true;
    }
    return false;
}

ui::GuiWidget* CreatePanel(int x0, int y0, int x1, int y1)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the dialog's widget tree owns its children (gui_widget_virtual.h)
    return new GuiExtraScenariosPanel(x0, y0, x1, y1);
}

bool IsOpen()
{
    return g_openPanel != nullptr;
}

bool HandleKeyDown(int scancode)
{
    return g_openPanel && g_openPanel->HandleKey(scancode);
}

bool FindTarget(std::string_view name, int& x, int& y)
{
    if (!g_openPanel) return false;
    if (g_openPanel->m_targets.empty()) g_openPanel->Layout();
    for (const Target& target : g_openPanel->m_targets) {
        if (target.name == name) {
            x = (target.x0 + target.x1) / 2;
            y = (target.y0 + target.y1) / 2;
            return true;
        }
    }
    return false;
}

} // namespace obl::gui::extra_scenarios_screen
