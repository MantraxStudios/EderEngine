#include "LuaScriptSystem.h"

#include <iostream>
#include <string>

#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/RigidbodyComponent.h"
#include "ECS/Components/ColliderComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/CollisionCallbackComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include <IO/AssetManager.h>

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
LuaScriptSystem& LuaScriptSystem::Get()
{
    static LuaScriptSystem s_instance;
    return s_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────
void LuaScriptSystem::Init()
{
    if (m_initialized) return;

    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table,
        sol::lib::io,
        sol::lib::os
    );

    BindAPI();

    m_initialized = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void LuaScriptSystem::Shutdown()
{
    m_envs.clear();
    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reload — force re-load for one entity
// ─────────────────────────────────────────────────────────────────────────────
void LuaScriptSystem::Reload(Entity e)
{
    m_envs.erase(e);
}

void LuaScriptSystem::Remove(Entity e)
{
    m_envs.erase(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — called every frame
// ─────────────────────────────────────────────────────────────────────────────
void LuaScriptSystem::Update(Registry& registry, float dt)
{
    if (!m_initialized) return;

    // Update the registry pointer used by all Lua API bindings this frame
    m_lua["__set_registry"](static_cast<void*>(&registry));

    registry.Each<ScriptComponent>([&](Entity e, ScriptComponent& sc)
    {
        if (sc.scriptGuid == 0) return;

        // ── Load + Start ─────────────────────────────────────────────────────
        if (!sc.started)
        {
            if (!LoadScript(e, registry)) return;
            sc.started = true;

            HookCollisions(e, registry);

            auto& env = m_envs.at(e);
            sol::protected_function fn = env["OnStart"];
            if (fn.valid())
            {
                auto res = fn();
                if (!res.valid())
                {
                    sol::error err = res;
                    std::cerr << "[Lua] OnStart error (entity=" << e << "): " << err.what() << "\n";
                }
            }
        }

        // ── OnUpdate ─────────────────────────────────────────────────────────
        auto& env = m_envs.at(e);
        sol::protected_function fn = env["OnUpdate"];
        if (fn.valid())
        {
            auto res = fn(dt);
            if (!res.valid())
            {
                sol::error err = res;
                std::cerr << "[Lua] OnUpdate error (entity=" << e << "): " << err.what() << "\n";
            }
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadScript — compile and run the script in a fresh env
// ─────────────────────────────────────────────────────────────────────────────
bool LuaScriptSystem::LoadScript(Entity e, Registry& reg)
{
    auto& sc = reg.Get<ScriptComponent>(e);

    // Load raw bytes via AssetManager (works in both editor and PAK mode)
    std::vector<uint8_t> bytes;
    if (sc.scriptGuid != 0)
        bytes = Krayon::AssetManager::Get().GetBytesByGuid(sc.scriptGuid);

    if (bytes.empty())
    {
        std::cerr << "[Lua] Could not load script for entity=" << e
                  << " guid=0x" << std::hex << sc.scriptGuid << std::dec << "\n";
        return false;
    }

    // Create isolated environment (inherits globals like Transform, math, etc.)
    sol::environment env(m_lua, sol::create, m_lua.globals());
    env["this_entity"] = static_cast<int>(e);

    // Redirect print to stderr tagged with entity id
    env["print"] = [e](sol::variadic_args args) {
        std::cerr << "[Lua:" << e << "] ";
        for (auto v : args)
        {
            if (v.get_type() == sol::type::string)        std::cerr << v.get<std::string>();
            else if (v.get_type() == sol::type::number)   std::cerr << v.get<double>();
            else if (v.get_type() == sol::type::boolean)  std::cerr << (v.get<bool>() ? "true" : "false");
            else std::cerr << "(" << sol::type_name(LuaScriptSystem::Get().GetState().lua_state(), v.get_type()) << ")";
        }
        std::cerr << "\n";
    };
    env["log"] = env["print"];

    // Compile + execute
    const std::string chunk(bytes.begin(), bytes.end());
    auto res = m_lua.safe_script(chunk, env,
        sol::script_pass_on_error, "script");

    if (!res.valid())
    {
        sol::error err = res;
        std::cerr << "[Lua] Load error (entity=" << e << "): " << err.what() << "\n";
        return false;
    }

    m_envs[e] = std::move(env);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// HookCollisions — register CollisionCallbackComponent forwarding to Lua
// ─────────────────────────────────────────────────────────────────────────────
void LuaScriptSystem::HookCollisions(Entity e, Registry& reg)
{
    if (!reg.Has<CollisionCallbackComponent>(e))
        reg.Add<CollisionCallbackComponent>(e);

    auto& cb  = reg.Get<CollisionCallbackComponent>(e);
    // Capture env by value (sol::environment is ref-counted)
    auto  env = m_envs.at(e);

    auto callLua = [this, e, env](const char* fn, const CollisionEvent& ev) mutable
    {
        sol::protected_function f = env[fn];
        if (!f.valid()) return;
        // Build a small table: {x,y,z} for point and normal
        auto makeVec = [&](const glm::vec3& v) {
            sol::table t = m_lua.create_table();
            t["x"] = v.x; t["y"] = v.y; t["z"] = v.z;
            return t;
        };
        auto res = f(static_cast<int>(ev.other), makeVec(ev.point), makeVec(ev.normal));
        if (!res.valid())
        {
            sol::error err = res;
            std::cerr << "[Lua] " << fn << " error (entity=" << e << "): " << err.what() << "\n";
        }
    };

    cb.onCollisionEnter = [callLua](const CollisionEvent& ev) mutable { callLua("OnCollisionEnter", ev); };
    cb.onCollisionStay  = [callLua](const CollisionEvent& ev) mutable { callLua("OnCollisionStay",  ev); };
    cb.onCollisionExit  = [callLua](const CollisionEvent& ev) mutable { callLua("OnCollisionExit",  ev); };

    cb.onTriggerEnter = [this, e, env](const CollisionEvent& ev) mutable {
        sol::protected_function f = env["OnTriggerEnter"];
        if (!f.valid()) return;
        auto res = f(static_cast<int>(ev.other));
        if (!res.valid()) { sol::error err = res; std::cerr << "[Lua] OnTriggerEnter: " << err.what() << "\n"; }
    };
    cb.onTriggerExit  = [this, e, env](const CollisionEvent& ev) mutable {
        sol::protected_function f = env["OnTriggerExit"];
        if (!f.valid()) return;
        auto res = f(static_cast<int>(ev.other));
        if (!res.valid()) { sol::error err = res; std::cerr << "[Lua] OnTriggerExit: " << err.what() << "\n"; }
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// BindAPI — expose engine tables as Lua globals
// ─────────────────────────────────────────────────────────────────────────────
void LuaScriptSystem::BindAPI()
{
    // ── Helper: registry pointer stored as upvalue ───────────────────────────
    // We pass registry per-call so scripts can be called from different worlds.
    // Instead, we store a raw ptr in a closure variable updated each frame.
    // A simpler approach: store ptr in __registry global, update before each call.
    // Here we bind functions that capture nothing and take entity + registry via
    // a thread-local pointer set before every script call.

    // We use a Registry* stored each frame.
    static Registry* s_reg = nullptr;

    // ─────────────────────────────────────────────────────────────────────────
    // Transform table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table T = m_lua.create_named_table("Transform");

    T["getPosition"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<TransformComponent>((Entity)e)) return sol::nil;
        auto& t = s_reg->Get<TransformComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = t.position.x; r["y"] = t.position.y; r["z"] = t.position.z;
        return r;
    };
    T["setPosition"] = [](int e, float x, float y, float z) {
        if (s_reg && s_reg->Has<TransformComponent>((Entity)e))
        {
            auto& t = s_reg->Get<TransformComponent>((Entity)e);
            t.position = { x, y, z };
        }
    };
    T["getRotation"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<TransformComponent>((Entity)e)) return sol::nil;
        auto& t = s_reg->Get<TransformComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = t.rotation.x; r["y"] = t.rotation.y; r["z"] = t.rotation.z;
        return r;
    };
    T["setRotation"] = [](int e, float x, float y, float z) {
        if (s_reg && s_reg->Has<TransformComponent>((Entity)e))
            s_reg->Get<TransformComponent>((Entity)e).rotation = { x, y, z };
    };
    T["getScale"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<TransformComponent>((Entity)e)) return sol::nil;
        auto& t = s_reg->Get<TransformComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = t.scale.x; r["y"] = t.scale.y; r["z"] = t.scale.z;
        return r;
    };
    T["setScale"] = [](int e, float x, float y, float z) {
        if (s_reg && s_reg->Has<TransformComponent>((Entity)e))
            s_reg->Get<TransformComponent>((Entity)e).scale = { x, y, z };
    };
    T["getWorldPosition"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<TransformComponent>((Entity)e)) return sol::nil;
        glm::mat4 w = TransformSystem::GetWorldMatrix((Entity)e, *s_reg);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = w[3][0]; r["y"] = w[3][1]; r["z"] = w[3][2];
        return r;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Tag table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Tag = m_lua.create_named_table("Tag");
    Tag["getName"] = [](int e) -> std::string {
        if (s_reg && s_reg->Has<TagComponent>((Entity)e))
            return s_reg->Get<TagComponent>((Entity)e).name;
        return "";
    };
    Tag["setName"] = [](int e, const std::string& n) {
        if (s_reg && s_reg->Has<TagComponent>((Entity)e))
            s_reg->Get<TagComponent>((Entity)e).name = n;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Rigidbody table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table RB = m_lua.create_named_table("Rigidbody");
    RB["hasRigidbody"]  = [](int e) { return s_reg && s_reg->Has<RigidbodyComponent>((Entity)e); };
    RB["getMass"]       = [](int e) -> float {
        return (s_reg && s_reg->Has<RigidbodyComponent>((Entity)e))
               ? s_reg->Get<RigidbodyComponent>((Entity)e).mass : 0.0f;
    };
    RB["setMass"] = [](int e, float v) {
        if (s_reg && s_reg->Has<RigidbodyComponent>((Entity)e)) {
            s_reg->Get<RigidbodyComponent>((Entity)e).mass = v;
        }
    };
    RB["getVelocity"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<RigidbodyComponent>((Entity)e)) return sol::nil;
        auto& rb = s_reg->Get<RigidbodyComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = rb.linearVelocity.x; r["y"] = rb.linearVelocity.y; r["z"] = rb.linearVelocity.z;
        return r;
    };
    RB["isKinematic"] = [](int e) -> bool {
        return s_reg && s_reg->Has<RigidbodyComponent>((Entity)e) &&
               s_reg->Get<RigidbodyComponent>((Entity)e).isKinematic;
    };
    RB["setKinematic"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<RigidbodyComponent>((Entity)e)) {
            s_reg->Get<RigidbodyComponent>((Entity)e).isKinematic = v;
        }
    };
    RB["useGravity"] = [](int e) -> bool {
        return s_reg && s_reg->Has<RigidbodyComponent>((Entity)e) &&
               s_reg->Get<RigidbodyComponent>((Entity)e).useGravity;
    };
    RB["setGravity"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<RigidbodyComponent>((Entity)e))
            s_reg->Get<RigidbodyComponent>((Entity)e).useGravity = v;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Collider table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Col = m_lua.create_named_table("Collider");
    Col["hasCollider"]   = [](int e) { return s_reg && s_reg->Has<ColliderComponent>((Entity)e); };
    Col["isTrigger"]     = [](int e) -> bool {
        return s_reg && s_reg->Has<ColliderComponent>((Entity)e) &&
               s_reg->Get<ColliderComponent>((Entity)e).isTrigger;
    };
    Col["setTrigger"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).isTrigger = v;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Entity table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Ent = m_lua.create_named_table("Entity");
    Ent["create"] = []() -> int {
        if (!s_reg) return (int)NULL_ENTITY;
        return (int)s_reg->Create();
    };
    Ent["destroy"] = [](int e) {
        if (s_reg) s_reg->Destroy((Entity)e);
        LuaScriptSystem::Get().Remove((Entity)e);
    };
    Ent["isValid"] = [](int e) -> bool {
        if (!s_reg || e == (int)NULL_ENTITY) return false;
        for (Entity alive : s_reg->GetEntities())
            if (alive == (Entity)e) return true;
        return false;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Math helpers
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Vec3 = m_lua.create_named_table("Vec3");
    Vec3["new"]  = [](float x, float y, float z) -> sol::table {
        sol::table t = LuaScriptSystem::Get().GetState().create_table();
        t["x"] = x; t["y"] = y; t["z"] = z;
        return t;
    };
    Vec3["length"] = [](sol::table v) -> float {
        float x = v.get_or("x", 0.0f), y = v.get_or("y", 0.0f), z = v.get_or("z", 0.0f);
        return std::sqrt(x*x + y*y + z*z);
    };
    Vec3["dot"] = [](sol::table a, sol::table b) -> float {
        return a.get_or("x",0.f)*b.get_or("x",0.f)
             + a.get_or("y",0.f)*b.get_or("y",0.f)
             + a.get_or("z",0.f)*b.get_or("z",0.f);
    };
    Vec3["normalize"] = [](sol::table v) -> sol::table {
        float x = v.get_or("x",0.f), y = v.get_or("y",0.f), z = v.get_or("z",0.f);
        float len = std::sqrt(x*x + y*y + z*z);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        if (len > 1e-6f) { r["x"]=x/len; r["y"]=y/len; r["z"]=z/len; }
        else              { r["x"]=0.f;   r["y"]=0.f;   r["z"]=0.f;  }
        return r;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Set s_reg each frame — expose a setter called internally
    // ─────────────────────────────────────────────────────────────────────────
    m_lua["__set_registry"] = [](void* ud) {
        s_reg = static_cast<Registry*>(ud);
    };

    // Global log alias
    m_lua["log"] = [](const std::string& s) { std::cout << "[Lua] " << s << "\n"; };
}
