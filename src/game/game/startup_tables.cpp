#include "game/game/startup_tables.h"

#include "game/game/game_api.h"
#include "game/ui/gui_data.h"
#include "game/ui/renderer_types.h"

#include <algorithm>
#include <bit>
#include <iterator>

namespace obl::game {

namespace {

float g_gameSpeedTable[3]{};
ui::RendererLoadingGradient g_loadingGradient{};
float g_posOffset1[2]{};
float g_posOffset2[2]{};
const char* g_guiListSpriteNames[3]{};
const char* g_guiMainItemSpriteNames[3]{};

void CopySpriteNames(const char* (&to)[3], const char* const (&names)[3])
{
    std::copy(std::begin(names), std::end(names), to);
}

} // namespace

void InitStartupTables()
{
    // the loading bar's colour steps.
    g_loadingGradient.m_redStep = std::bit_cast<float>(0x3eab2809u);
    g_loadingGradient.m_greenStep = std::bit_cast<float>(0x3e5c5f7fu);
    g_loadingGradient.m_blueStep = std::bit_cast<float>(0x3e1a42d9u);
    game_api::InitSpeedTable();
    g_posOffset1[0] = 15.0f;
    g_posOffset1[1] = 0.0f;
    g_posOffset2[0] = -15.0f;
    g_posOffset2[1] = 0.0f;
    CopySpriteNames(g_guiListSpriteNames, gui_data::kGuiListSpriteNamesInit);
    CopySpriteNames(g_guiMainItemSpriteNames, gui_data::kGuiMainItemSpriteNamesInit);
}

float* GameSpeedTable()
{
    return g_gameSpeedTable;
}

const float* PositionOffset(int variant)
{
    return variant == 1 ? g_posOffset1 : g_posOffset2;
}

ui::RendererLoadingGradient& LoadingGradient()
{
    return g_loadingGradient;
}

const char* const* GuiListSpriteNames()
{
    return g_guiListSpriteNames;
}

const char* const* GuiMainItemSpriteNames()
{
    return g_guiMainItemSpriteNames;
}

} // namespace obl::game
