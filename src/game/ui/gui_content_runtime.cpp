// The converted GUI data (plus OpenBeach's own credits, own_credits.h) as the GUI engine reads it. The game keeps raw
// pointers into the key strings, so the storage is filled once at start-up and never changed afterwards.

#include "core/char16.h"
#include "core/pack_path.h"
#include "gui_content_runtime.h"

#include "formats/gui_content.h"
#include "game/game/own_credits.h"
#include "game/game/diagnostics.h"
#include "core/log.h"
#include "core/wide_path.h"

#include <array>
#include <filesystem>
#include <vector>

namespace obl::gui::gui_content {

namespace {

obl::gui_content::Content g_content;
std::array<std::vector<ListRow>, 6> g_rows;
std::vector<ui::GuiTableHeadingKey> g_headings;
std::vector<ui::GuiCreditEntry> g_credits;
std::vector<obl::gui_content::Credit> g_creditRows;
std::string g_error = "not loaded";
bool g_loaded = false;

// Headless runs never reach the GUI, so they start without the files; a windowed game stops with the reason.
void Require()
{
    if (g_loaded) return;
    game::FatalError("Cannot load remaster GUI data (%s).\r\nImport the game data again", g_error.c_str());
}

} // namespace

bool Load(const Text16& gameDirectory)
{
    if (g_loaded) return true;
    if (!obl::gui_content::LoadPack(obl::WidePath(gameDirectory) / obl::pack_path::Selected(), g_content, g_error)) {
        OBL_LOG("gui content: %s", g_error.c_str());
        return false;
    }
    for (size_t i = 0; i < g_rows.size(); ++i)
        for (const auto& entry : g_content.lists[i]) g_rows[i].push_back({entry.key.empty() ? nullptr : entry.key.c_str(), entry.value});
    for (const auto& heading : g_content.headings) g_headings.push_back({heading.key.c_str(), static_cast<uint32_t>(heading.filter)});
    g_creditRows = own_credits::Merge(g_content.credits);
    for (const auto& credit : g_creditRows) {
        ui::GuiCreditEntry entry{};
        entry.m_font = credit.font;
        entry.m_number = credit.line;
        entry.m_color = credit.color;
        entry.m_long = credit.longSpacing ? 1 : 0;
        entry.m_kind = credit.kind;
        g_credits.push_back(entry);
    }
    g_loaded = true;
    g_error.clear();
    OBL_LOG("gui content: %u headings, %u credit rows", static_cast<unsigned>(g_headings.size()), static_cast<unsigned>(g_credits.size()));
    return true;
}

bool Loaded()
{
    return g_loaded;
}
const std::string& LoadError()
{
    return g_error;
}

std::span<const ListRow> Rows(List list)
{
    Require();
    return g_rows[static_cast<size_t>(list)];
}
std::span<const ui::GuiTableHeadingKey> Headings()
{
    Require();
    return g_headings;
}
const char* ButtonResource(int index)
{
    Require();
    return g_content.buttons[static_cast<size_t>(index)].c_str();
}
std::span<const ui::GuiCreditEntry> Credits()
{
    Require();
    return g_credits;
}

std::span<const obl::gui_content::Credit> CreditRows()
{
    if (g_loaded) return g_creditRows;
    static const auto own = own_credits::Merge({});
    return own;
}

} // namespace obl::gui::gui_content
