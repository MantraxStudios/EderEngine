#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  LuaScriptSystem
//  Manages one shared sol::state, one sol::environment per scripted entity.
//  Call order each frame:
//      LuaScriptSystem::Get().Update(registry, dt);
//  Call once at startup (after AssetManager is ready):
//      LuaScriptSystem::Get().Init();
//  Call once at shutdown:
//      LuaScriptSystem::Get().Shutdown();
//  To force a script reload (e.g. after hot-saving in editor):
//      LuaScriptSystem::Get().Reload(entity);
// ─────────────────────────────────────────────────────────────────────────────

#define SOL_ALL_SAFETIES_ON 1
#define SOL_USING_CXX_OPTIONAL 1
#include <sol.hpp>
#include <unordered_map>
#include "ECS/Registry.h"
#include "ECS/Entity.h"

class LuaScriptSystem
{
public:
    static LuaScriptSystem& Get();

    void Init    ();
    void Shutdown();

    // Per-frame: loads + starts new scripts, calls OnUpdate on all.
    void Update(Registry& registry, float dt);

    // Force-resets a single entity so it re-loads its script next Update.
    void Reload(Entity e);

    // Remove a single entity's environment (call when entity is destroyed).
    void Remove(Entity e);

    sol::state& GetState() { return m_lua; }

    // Returns and clears the scene path queued by Scene.load(), or empty string.
    std::string ConsumePendingScene()
    {
        std::string s = std::move(m_pendingScene);
        m_pendingScene.clear();
        return s;
    }

private:
    LuaScriptSystem()  = default;
    ~LuaScriptSystem() = default;

    // Compile + execute the script bytes in a fresh env for entity e.
    // Returns false on error.
    bool LoadScript (Entity e, Registry& reg);

    // Register CollisionCallbackComponent on `e` forwarding events to Lua.
    void HookCollisions(Entity e, Registry& reg);

    // Bind the engine API tables (Transform, Tag, Rigidbody, Collider, …)
    void BindAPI();

    sol::state                                 m_lua;
    std::unordered_map<Entity, sol::environment> m_envs;
    bool                                       m_initialized = false;
    std::string                                m_pendingScene;
};
