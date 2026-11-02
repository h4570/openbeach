#pragma once

#include <cstdint>
#include <string>

namespace obl::game {

class GameState {
public:
    explicit GameState(const char* name) : m_name(name) {}
    virtual ~GameState() = default;
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    virtual int Update() = 0;
    virtual void Render() {}
    virtual void PostRender() {}
    virtual void Enter() = 0;
    // The front-end state exit, shared by every state but GameWon and WorldTest.
    virtual void Exit() { m_entered = 0; }
    virtual const char* GetGuiScript() { return nullptr; }
    virtual void ChangeResolution(int /*width*/, int /*height*/) {}

    const std::string& Name() const { return m_name; }

    uint8_t m_entered = 0; // set by Enter, cleared by Exit; GameWon and WorldTest act on it when destroyed

private:
    std::string m_name; // "Intro", "MainMenu"...
};

} // namespace obl::game
