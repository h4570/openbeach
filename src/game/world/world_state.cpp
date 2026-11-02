#include "game/world/world_state.h"

namespace obl::world {
namespace {

SubsystemTeardown g_teardown = nullptr;

} // namespace

void SetSubsystemTeardown(SubsystemTeardown teardown)
{
    g_teardown = teardown;
}

void DestroySubsystem(Subsystem which, void* pointer)
{
    if (pointer && g_teardown) g_teardown(which, pointer);
}

// Teardown order: subsystems first, the scripted sound last. The PIS texts are freed with the members.
World::~World()
{
    m_commodityProviders.reset();
    m_player.reset();
    m_objectManager.reset();
    m_messageHandler.reset();
    m_path.reset();
    m_grid.reset();
    m_worldParams.reset();
    m_eventManager.reset();
    m_ferryManager.reset();
    m_repairManager.reset();
    m_cleaningManager.reset();
    m_feedbackManager.reset();
    m_mapView.reset();
    m_weather.reset();
    m_pathTiles.reset();
    m_constructionManager.reset();
    m_worldScriptHost.reset();
    m_scriptedSound.reset();
}

} // namespace obl::world
