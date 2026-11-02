#pragma once

#include "game/world/world_script_types.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The scenario's script host (the world's script host field, g_WorldScriptHost) as a native class. It owns
// the object logics a scenario loads with `LoadScript <logic> <path>;` (by name, in load order) and the scenario script itself. The
// scripts' contents (their command lists on the game heap) are built and freed by the parsers behind an `Env`, so the order of the
// calls stays unit-tested without the game (tests/unit/world_script_host_test.cpp):
//   void SetHost(WorldScriptHost*)                       game::Context::worldScriptHost
//   void ConstructObject(ScriptProgram&) / LoadObject(ScriptProgram&, path) / DestroyObject(ScriptProgram&)
//   void ConstructWorld(WorldScript&) / uint32_t LoadWorld(WorldScript&, path) / DestroyWorld(WorldScript&)
// The former ten fixed slots were not bounds-checked; the vector has no limit.
namespace obl::world_script_host {

class WorldScriptHost {
public:
    // Binds the global.
    template<class Env> explicit WorldScriptHost(Env& env) { env.SetHost(this); }
    WorldScriptHost(const WorldScriptHost&) = delete;
    WorldScriptHost& operator=(const WorldScriptHost&) = delete;

    // Destroys the object logics in load order, then the scenario script, then the global.
    template<class Env> void Destroy(Env& env)
    {
        for (Entry& entry : m_objectScripts) env.DestroyObject(*entry.script);
        m_objectScripts.clear();
        DropWorldScript(env);
        env.SetHost(nullptr);
    }

    // WorldScriptHost::LoadScript: replaces the scenario script; returns what the loader returns (the `.ble` size).
    template<class Env> uint32_t LoadScript(const char* path, Env& env)
    {
        DropWorldScript(env);
        m_worldScript = std::make_unique<world_script::WorldScript>();
        env.ConstructWorld(*m_worldScript);
        return env.LoadWorld(*m_worldScript, path);
    }

    // WorldScriptHost::AddScriptProgram: one object logic under `name`, built and loaded from `path`.
    template<class Env> void AddScriptProgram(const char* name, const char* path, Env& env)
    {
        Entry& entry = m_objectScripts.emplace_back(Entry{name, std::make_unique<script::ScriptProgram>()});
        env.ConstructObject(*entry.script);
        env.LoadObject(*entry.script, path);
    }

    // WorldScriptHost::FindScriptProgram: the first logic of that name, or null.
    script::ScriptProgram* FindScriptProgram(const char* name) const
    {
        for (const Entry& entry : m_objectScripts)
            if (entry.name == name) return entry.script.get();
        return nullptr;
    }

    // The game's own entry points (world_script_host_game.cpp: the game's script parsers behind them, each counts its own
    // `world-script-host.*` counter). `Create` binds the game context's host, `Release` frees it and clears the binding.
    static WorldScriptHost* Create();
    static void Release(WorldScriptHost* host);
    uint32_t Load(const char* path);
    void AddScript(const char* name, const char* path);
    script::ScriptProgram* Find(const char* name) const;

    // What World::m_worldScript points at (null before the first LoadScript).
    world_script::WorldScript* WorldScript() const { return m_worldScript.get(); }
    size_t ScriptProgramCount() const { return m_objectScripts.size(); }
    const std::string& ScriptProgramName(size_t i) const { return m_objectScripts[i].name; }
    script::ScriptProgram& ScriptProgramAt(size_t i) const { return *m_objectScripts[i].script; }

private:
    struct Entry {
        std::string name;
        std::unique_ptr<script::ScriptProgram> script;
    };

    template<class Env> void DropWorldScript(Env& env)
    {
        if (m_worldScript) env.DestroyWorld(*m_worldScript);
        m_worldScript.reset();
    }

    std::vector<Entry> m_objectScripts;
    std::unique_ptr<world_script::WorldScript> m_worldScript;
};

} // namespace obl::world_script_host
