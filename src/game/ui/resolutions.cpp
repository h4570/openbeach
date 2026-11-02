// More in-game resolutions (windowed mode).
//
// The Options > Video combo box ("ResolutionList") is filled by the combo box's populate step from
// k_ResolutionTable: 4 entries {int modeIndex; const char* textKey; int height;} (640x480 ..
// 1280x960), shown only if the renderer's display-mode flag [modeIndex] is set, and selected by
// height in the list's OnItemSelected -> GuiManager::RequestResolution -> the game's resolution change, which
// handles any size in windowed mode. We point the populate loop at our own table (monitor-sized
// entries, all using the always-present 800x600 mode flag), map selections back to width/height,
// provide the texts for the new keys. src/platform/window_placement.cpp measures the monitor and places the window.

#include "core/text16.h"
#include "game/ui/resolutions.h"

#include "game/game/game_accessors.h"
#include "game/ui/display_geometry.h"
#include "core/log.h"

#include <algorithm>
#include <string>
#include <vector>

namespace obl::ui {

namespace {

// Same layout as k_ResolutionTable.
using TableEntry = ResolutionListEntry;

// The four modes the options list offers before the extended table is built.
constexpr ResolutionListEntry kBaseResolutions[] = {
        {0, "640_480", 480},
        {1, "800_600", 600},
        {2, "1024_768", 768},
        {3, "1280_960", 960},
};

struct Resolution {
    int width, height;
    std::string key; // "1920_1080"
    Text16 text; // L"1920x1080"
};

std::vector<Resolution> g_resolutions;
std::vector<TableEntry> g_table;
int g_lastScreenHeight;

void BuildTable(int monitorW, int monitorH)
{
    static const ui::DisplaySize kCandidates[] = {
            {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

    std::vector<ui::DisplaySize> sizes;
    for (ui::DisplaySize s : kCandidates) {
        if (s.cx <= monitorW && s.cy <= monitorH) sizes.push_back(s);
    }
    // Native size (borderless). The list selects by height, so it replaces a same-height entry.
    std::erase_if(sizes, [&](ui::DisplaySize s) { return s.cy == monitorH; });
    sizes.push_back({monitorW, monitorH});
    std::sort(sizes.begin(), sizes.end(), [](ui::DisplaySize a, ui::DisplaySize b) { return a.cx != b.cx ? a.cx < b.cx : a.cy < b.cy; });

    for (ui::DisplaySize s : sizes) {
        Resolution r{s.cx, s.cy, std::to_string(s.cx) + "_" + std::to_string(s.cy), text16::Decimal(s.cx) + u"x" + text16::Decimal(s.cy)};
        g_resolutions.push_back(std::move(r));
    }
    g_table.reserve(g_resolutions.size() + 1);
    for (const auto& r : g_resolutions) g_table.push_back({1, r.key.c_str(), r.height});
    g_table.push_back({}); // a zero entry ends the list, as in k_ResolutionTable
}

const Resolution* FindByHeight(int height)
{
    for (const auto& r : g_resolutions) {
        if (r.height == height) return &r;
    }
    return nullptr;
}

} // namespace

bool IsListedResolutionWidth(int width)
{
    for (const auto& r : g_resolutions) {
        if (r.width == width) return true;
    }
    return false;
}

void BuildResolutionTable(int monitorWidth, int monitorHeight)
{
    BuildTable(monitorWidth, monitorHeight);
    std::string list;
    for (const auto& r : g_resolutions) list += r.key + " ";
    OBL_LOG("resolutions: %s", list.c_str());
}

const Char16* ResolutionText(const char* key)
{
    for (const auto& r : g_resolutions) {
        if (key == r.key.c_str()) return r.text.c_str();
    }
    return nullptr;
}

// GuiManager::SyncBarSurfaces (gui_manager_frame.cpp) runs this first. It is run by every GuiManager::LoadScript (the GUI is reloaded
// after each resolution change) and resizes the bottom-bar surface to (screen width x bar height); only when that size changed does it
// recreate the GUI surfaces and move the bar origin to `screen height - bar height`. The game 4:3 modes always change the width,
// so a height-only change (1280x960 -> 1280x720) left the origin below the new screen and the next frame drew the bar past the end of
// the back buffer (heap corruption). Invalidating the surface's recorded width when the screen height changed makes the sync take its
// full rebuild path.
void PrepareBarSurfaceSync(void* gui)
{
    ui::Renderer* renderer = game::GetRenderer();
    int height = renderer->m_height;
    if (g_lastScreenHeight && height != g_lastScreenHeight) {
        ui::SurfaceAt(*renderer, static_cast<int*>(gui)[0x2b])->m_width = 0; // never matches, forces the rebuild
    }
    g_lastScreenHeight = height;
}

std::span<const ResolutionListEntry> ResolutionListEntries()
{
    if (!g_table.empty()) return {g_table.data(), g_table.size() - 1};
    return kBaseResolutions;
}

bool FindExtendedResolution(int height, int* width)
{
    const Resolution* r = FindByHeight(height);
    if (r) *width = r->width;
    return r != nullptr;
}

} // namespace obl::ui
