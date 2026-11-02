#pragma once

#include "game/game/game_accessors.h"

#include <cstddef>
#include <cstdint>

// Access to game objects shared by the scripted-test modules.
namespace obl::test {

inline layout::World* World()
{
    return game::GetWorld();
}
inline game::Game* Game()
{
    return game::GetGame();
}

// Index of the current game state (game::GameStateId: 0 WorldTest/gameplay, 1 MainMenu, 2 Intro, 3 ExitGame,
// 4 ResetGame, 5 LoadGame, 6 GameWon); -1 before the game exists.
inline int GameStateIndex()
{
    const game::Game* game = Game();
    return game ? game::CurrentStateIndex(*game) : -1;
}

} // namespace obl::test
