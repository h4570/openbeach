#pragma once
#include "game/game/game_state_base.h"

#include <memory>

namespace obl::world {
struct World;
}
namespace obl::game {

class WorldTestState final : public GameState {
public:
    WorldTestState();
    ~WorldTestState() override;

    int Update() override;
    void Render() override;
    void PostRender() override;
    void Enter() override;
    void Exit() override;
    const char* GetGuiScript() override;
    void ChangeResolution(int width, int height) override;
    // WorldTestState::FinishLoad: the second half of Enter, run after a save has filled the world.
    void FinishLoad();

    struct WorldDeleter {
        void operator()(obl::world::World* world) const;
    };
    std::unique_ptr<obl::world::World, WorldDeleter> m_world;
    uint8_t m_updateSuspended = 0; // Update does nothing while set (nothing sets it)
    // Set by the load-game flow: Enter builds the World but leaves the rest to FinishLoad. the state switch(WorldTest) clears it.
    uint8_t m_loadingSave = 0;
    int m_wavesChannel = 0; // waves_loop, faded with the zoom
    int m_crowdChannel = 0; // crowdbig, follows the number of guests drawn
    int m_cricketChannel = 0; // cricket_loop, night only
    float m_crowdVolume = 0.0f; // the last crowd volume UpdateCrowdCricket applied
};
} // namespace obl::game
