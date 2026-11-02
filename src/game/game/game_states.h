#pragma once

#include "game/game/game_state_base.h"
#include "game/game/game_states_logic.h"

namespace obl::game {
namespace gs = game_states;

// ---- the states ----

// What the six share: every call counts, and Exit clears the entered flag (all but GameWon).
template<class Env> class SmallState : public GameState {
public:
    using GameState::GameState;
    void Exit() override
    {
        Env::Count();
        this->m_entered = 0;
    }

protected:
    Env MakeEnv(typename Env::Widgets* widgets = nullptr)
    {
        Env::Count();
        return Env(*this, widgets);
    }
    // The state's GUI layout file for the language; ExitGame and LoadGame have none.
    static const char* Script(const char* layout)
    {
        Env::Count();
        return Env::Script(layout);
    }
};

template<class Env> class ExitGameState final : public SmallState<Env>, public gs::ExitGameFields {
public:
    ExitGameState() : SmallState<Env>("ExitGame") {}
    int Update() override
    {
        Env env = this->MakeEnv();
        return gs::ExitUpdate(*this, env);
    }
    void Enter() override
    {
        Env env = this->MakeEnv();
        gs::ExitEnter(*this, env);
    }
    const char* GetGuiScript() override { return this->Script(nullptr); }
};

template<class Env> class IntroState final : public SmallState<Env>, public gs::IntroFields {
public:
    IntroState() : SmallState<Env>("Intro") {}
    int Update() override
    {
        Env env = this->MakeEnv();
        return gs::IntroUpdate(*this, env);
    }
    void Enter() override
    {
        Env env = this->MakeEnv();
        gs::IntroEnter(*this, env);
    }
    const char* GetGuiScript() override { return this->Script(gs::kIntroLayout); }
};

template<class Env> class LoadGameState final : public SmallState<Env>, public gs::LoadGameFields {
public:
    LoadGameState() : SmallState<Env>("LoadGame") {}
    int Update() override
    {
        Env env = this->MakeEnv();
        return gs::LoadGameUpdate(*this, env);
    }
    void Enter() override
    {
        Env::Count();
        gs::LoadGameEnter(*this);
    }
    const char* GetGuiScript() override { return this->Script(nullptr); }
};

template<class Env> class MainMenuState final : public SmallState<Env>, public gs::MainMenuFields {
public:
    MainMenuState() : SmallState<Env>("MainMenu") {}
    int Update() override
    {
        Env::Count();
        return 0;
    }
    void Enter() override
    {
        Env env = this->MakeEnv();
        gs::MainMenuEnter(*this, env);
    }
    const char* GetGuiScript() override { return this->Script(gs::kMainMenuLayout); }
};

template<class Env> class ResetGameState final : public SmallState<Env>, public gs::ResetGameFields {
public:
    ResetGameState() : SmallState<Env>("ResetGame") {}
    int Update() override
    {
        Env env = this->MakeEnv();
        return gs::ResetUpdate(env);
    }
    void Enter() override
    {
        Env env = this->MakeEnv();
        gs::ResetEnter(*this, env);
    }
    const char* GetGuiScript() override { return this->Script(gs::kResetLayout); }
};

template<class Env> class GameWonState final : public SmallState<Env>, public gs::GameWonFields {
public:
    GameWonState() : SmallState<Env>("GameWon") {}
    // An entered state closes its stream; the page widgets are not deleted.
    ~GameWonState() override
    {
        Env::Count();
        if (this->m_entered != 0) {
            Env env(*this, &m_widgets);
            env.CloseStream();
            this->m_entered = 0;
        }
    }
    int Update() override
    {
        Env env = this->MakeEnv(&m_widgets);
        return gs::GameWonUpdate(*this, env);
    }
    void Enter() override
    {
        Env env = this->MakeEnv(&m_widgets);
        gs::GameWonEnter(*this, env);
    }
    void Exit() override
    {
        Env env = this->MakeEnv(&m_widgets);
        gs::GameWonExit(*this, env);
    }
    const char* GetGuiScript() override { return this->Script(gs::kGameWonLayout); }
    // Draws the page: the widgets run slot 6 then slot 3 with the renderer in "visible objects" mode.
    void PostRender() override
    {
        Env::Count();
        Env env(*this, &m_widgets);
        env.RenderPages();
    }
    void ShowPage()
    {
        Env env = this->MakeEnv(&m_widgets);
        gs::GameWonShowPage(*this, env, Env::CreditRows());
    }

private:
    typename Env::Widgets m_widgets;
};

struct Game; // game/game/game.h
// The Game's seven states in GameStateId order, each with the live env (game_states.cpp), WorldTest first.
void CreateGameStates(game::Game& game);

} // namespace obl::game
