#include "game/ui/gui_patch_runtime.h"

#include "core/log.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"

#include <cstring>
#include <unordered_map>

namespace obl::gui_patch_runtime {
namespace {

ModActionHandler g_modActionHandler;
std::string g_lastModAction;
uint32_t g_modActionCount = 0;

struct BoundControl {
    ui::GuiWidget* widget = nullptr;
    std::string containerName;
};

std::unordered_map<std::string, BoundControl> g_controlsById;

ui::GuiWidget* ChildAt(ui::GuiContainerChildNode* sentinel, size_t index)
{
    if (!sentinel) return nullptr;
    size_t i = 0;
    for (auto* node = sentinel->m_next; node != sentinel; node = node->m_next, ++i) {
        if (i == index) return node->m_widget;
    }
    return nullptr;
}

ui::GuiDialogData* FindDialogDef(ui::GuiManager* manager, std::string_view name)
{
    if (!manager) return nullptr;
    for (auto* def = manager->m_definitions; def; def = def->m_next) {
        if (def->m_dialog && def->m_dialog->m_name && name == def->m_dialog->m_name) return def->m_dialog;
    }
    return nullptr;
}

} // namespace

void SetModActionHandler(ModActionHandler handler)
{
    g_modActionHandler = std::move(handler);
}

void DispatchModAction(std::string_view action)
{
    g_lastModAction.assign(action);
    ++g_modActionCount;
    OBL_LOG("gui mod action: %s", g_lastModAction.c_str());
    if (!g_modActionHandler || !gui_patch::IsModAction(action)) return;
    const std::string_view rest = action.substr(4);
    const size_t dot = rest.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= rest.size()) return;
    g_modActionHandler(rest.substr(0, dot), rest.substr(dot + 1));
}

const std::string& LastModAction()
{
    return g_lastModAction;
}

uint32_t ModActionCount()
{
    return g_modActionCount;
}

void ResetModActionStats()
{
    g_lastModAction.clear();
    g_modActionCount = 0;
}

void ClearBindings()
{
    g_controlsById.clear();
}

void BindLayout(ui::GuiManager* manager, std::span<const gui_patch::ControlBinding> bindings)
{
    g_controlsById.clear();
    if (!manager || bindings.empty()) return;
    for (const auto& b : bindings) {
        ui::GuiWidget* widget = nullptr;
        if (b.containerName == "MainItem" || b.containerName == "MainItem640") {
            if (manager->m_mainItem) {
                auto* mainItem = static_cast<ui::GuiMainItemData*>(manager->m_mainItem);
                widget = ChildAt(mainItem->m_children, b.childIndex);
            }
        } else if (auto* dialog = FindDialogDef(manager, b.containerName)) {
            widget = ChildAt(dialog->m_children, b.childIndex);
        }
        if (!widget) continue;
        widget->m_modAdded = b.fromMod ? 1 : 0;
        g_controlsById.emplace(b.id, BoundControl{widget, b.containerName});
    }
}

ui::GuiWidget* FindControl(std::string_view id)
{
    const auto it = g_controlsById.find(std::string(id));
    return it == g_controlsById.end() ? nullptr : it->second.widget;
}

bool SetControlVisible(ui::GuiManager* manager, std::string_view id, bool visible)
{
    const auto it = g_controlsById.find(std::string(id));
    if (it == g_controlsById.end() || !it->second.widget) return false;
    ui::GuiWidget* widget = it->second.widget;
    widget->m_modHidden = visible ? 0 : 1;
    widget->SetVisible(visible ? 1 : 0);
    if (manager) {
        if (it->second.containerName == "MainItem" || it->second.containerName == "MainItem640") {
            if (manager->m_mainItem) manager->m_mainItem->Invalidate();
        } else {
            manager->InvalidateDialog(it->second.containerName.c_str(), 1);
        }
    }
    return true;
}

bool MoveControl(ui::GuiManager* manager, std::string_view id, int x, int y)
{
    const auto it = g_controlsById.find(std::string(id));
    if (it == g_controlsById.end() || !it->second.widget) return false;
    ui::GuiWidget* widget = it->second.widget;
    const int dx = x - widget->m_x0;
    const int dy = y - widget->m_y0;
    widget->Translate(dx, dy);
    widget->m_baseX0 += dx;
    widget->m_baseX1 += dx;
    widget->m_baseY0 += dy;
    widget->m_baseY1 += dy;
    widget->Invalidate();
    if (manager) {
        if (it->second.containerName == "MainItem" || it->second.containerName == "MainItem640") {
            if (manager->m_mainItem) manager->m_mainItem->Invalidate();
        } else {
            manager->InvalidateDialog(it->second.containerName.c_str(), 1);
        }
    }
    return true;
}

bool ShowDialog(ui::GuiManager* manager, std::string_view name, bool modal)
{
    if (!manager) return false;
    const std::string s(name);
    return manager->ShowDialog(s.c_str(), nullptr, modal);
}

bool HideDialog(ui::GuiManager* manager, std::string_view name)
{
    if (!manager) return false;
    const std::string s(name);
    return manager->CloseDialog(s.c_str());
}

bool MoveDialog(ui::GuiManager* manager, std::string_view name, int x, int y)
{
    if (!manager) return false;
    auto* dialog = FindDialogDef(manager, name);
    if (!dialog) return false;
    const int dx = x - dialog->m_x0;
    const int dy = y - dialog->m_y0;
    dialog->Translate(dx, dy);
    dialog->m_baseX0 += dx;
    dialog->m_baseX1 += dx;
    dialog->m_baseY0 += dy;
    dialog->m_baseY1 += dy;
    const std::string s(name);
    manager->InvalidateDialog(s.c_str(), 1);
    return true;
}

} // namespace obl::gui_patch_runtime
