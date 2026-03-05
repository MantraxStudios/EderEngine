#include "LuaScriptSystem.h"

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace fs = std::filesystem;

#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/RigidbodyComponent.h"
#include "ECS/Components/ColliderComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/CollisionCallbackComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/LayerComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/CharacterControllerComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include <IO/AssetManager.h>
#include <IO/DebugDraw.h>
#include "Physics/PhysicsSystem.h"
#include <SDL3/SDL.h>
#include <cstring>

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
    m_lua["__update_input"]();

    registry.Each<ScriptComponent>([&](Entity e, ScriptComponent& sc)
    {
        if (sc.scriptGuid == 0) return;

        // ── Load + Start ─────────────────────────────────────────────────────
        if (!sc.started)
        {
            if (!LoadScript(e, registry))
            {
                sc.started = true;   // avoid spamming the error every frame
                return;
            }
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
    RB["setVelocity"] = [](int e, float x, float y, float z) {
        if (s_reg && s_reg->Has<RigidbodyComponent>((Entity)e))
            s_reg->Get<RigidbodyComponent>((Entity)e).linearVelocity = {x, y, z};
    };
    RB["getAngularVelocity"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<RigidbodyComponent>((Entity)e)) return sol::nil;
        auto& rb = s_reg->Get<RigidbodyComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = rb.angularVelocity.x; r["y"] = rb.angularVelocity.y; r["z"] = rb.angularVelocity.z;
        return r;
    };
    RB["getLinearDrag"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<RigidbodyComponent>((Entity)e)) return 0.f;
        return s_reg->Get<RigidbodyComponent>((Entity)e).linearDrag;
    };
    RB["setLinearDrag"] = [](int e, float v) {
        if (s_reg && s_reg->Has<RigidbodyComponent>((Entity)e))
            s_reg->Get<RigidbodyComponent>((Entity)e).linearDrag = v;
    };
    RB["getAngularDrag"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<RigidbodyComponent>((Entity)e)) return 0.f;
        return s_reg->Get<RigidbodyComponent>((Entity)e).angularDrag;
    };
    RB["setAngularDrag"] = [](int e, float v) {
        if (s_reg && s_reg->Has<RigidbodyComponent>((Entity)e))
            s_reg->Get<RigidbodyComponent>((Entity)e).angularDrag = v;
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
    // shape: "Box", "Sphere", "Capsule"
    Col["getShape"] = [](int e) -> std::string {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return "";
        switch (s_reg->Get<ColliderComponent>((Entity)e).shape) {
            case ColliderShape::Box:     return "Box";
            case ColliderShape::Sphere:  return "Sphere";
            case ColliderShape::Capsule: return "Capsule";
        }
        return "";
    };
    Col["setShape"] = [](int e, const std::string& s) {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return;
        auto& c = s_reg->Get<ColliderComponent>((Entity)e);
        if (s == "Box")          c.shape = ColliderShape::Box;
        else if (s == "Sphere")  c.shape = ColliderShape::Sphere;
        else if (s == "Capsule") c.shape = ColliderShape::Capsule;
    };
    Col["getRadius"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return 0.f;
        return s_reg->Get<ColliderComponent>((Entity)e).radius;
    };
    Col["setRadius"] = [](int e, float v) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).radius = v;
    };
    Col["getBoxHalfExtents"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return sol::nil;
        auto& c = s_reg->Get<ColliderComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = c.boxHalfExtents.x; r["y"] = c.boxHalfExtents.y; r["z"] = c.boxHalfExtents.z;
        return r;
    };
    Col["setBoxHalfExtents"] = [](int e, float x, float y, float z) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).boxHalfExtents = {x, y, z};
    };
    Col["getCapsuleHalfHeight"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return 0.f;
        return s_reg->Get<ColliderComponent>((Entity)e).capsuleHalfHeight;
    };
    Col["setCapsuleHalfHeight"] = [](int e, float v) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).capsuleHalfHeight = v;
    };
    Col["getCenter"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return sol::nil;
        auto& c = s_reg->Get<ColliderComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["x"] = c.center.x; r["y"] = c.center.y; r["z"] = c.center.z;
        return r;
    };
    Col["setCenter"] = [](int e, float x, float y, float z) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).center = {x, y, z};
    };
    Col["getStaticFriction"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return 0.f;
        return s_reg->Get<ColliderComponent>((Entity)e).staticFriction;
    };
    Col["setStaticFriction"] = [](int e, float v) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).staticFriction = v;
    };
    Col["getDynamicFriction"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return 0.f;
        return s_reg->Get<ColliderComponent>((Entity)e).dynamicFriction;
    };
    Col["setDynamicFriction"] = [](int e, float v) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).dynamicFriction = v;
    };
    Col["getRestitution"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<ColliderComponent>((Entity)e)) return 0.f;
        return s_reg->Get<ColliderComponent>((Entity)e).restitution;
    };
    Col["setRestitution"] = [](int e, float v) {
        if (s_reg && s_reg->Has<ColliderComponent>((Entity)e))
            s_reg->Get<ColliderComponent>((Entity)e).restitution = v;
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
    // Hierarchy table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Hier = m_lua.create_named_table("Hierarchy");
    Hier["hasParent"] = [](int e) -> bool {
        return s_reg && s_reg->Has<HierarchyComponent>((Entity)e)
            && s_reg->Get<HierarchyComponent>((Entity)e).parent != NULL_ENTITY;
    };
    Hier["getParent"] = [](int e) -> int {
        if (!s_reg || !s_reg->Has<HierarchyComponent>((Entity)e)) return (int)NULL_ENTITY;
        return (int)s_reg->Get<HierarchyComponent>((Entity)e).parent;
    };
    Hier["getChildCount"] = [](int e) -> int {
        if (!s_reg || !s_reg->Has<HierarchyComponent>((Entity)e)) return 0;
        return (int)s_reg->Get<HierarchyComponent>((Entity)e).children.size();
    };
    Hier["getChild"] = [](int e, int idx) -> int {
        if (!s_reg || !s_reg->Has<HierarchyComponent>((Entity)e)) return (int)NULL_ENTITY;
        const auto& ch = s_reg->Get<HierarchyComponent>((Entity)e).children;
        if (idx < 0 || idx >= (int)ch.size()) return (int)NULL_ENTITY;
        return (int)ch[idx];
    };

    // ─────────────────────────────────────────────────────────────────────────
    // MeshRenderer table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table MR = m_lua.create_named_table("MeshRenderer");
    MR["hasMeshRenderer"] = [](int e) -> bool {
        return s_reg && s_reg->Has<MeshRendererComponent>((Entity)e);
    };
    MR["isVisible"] = [](int e) -> bool {
        return s_reg && s_reg->Has<MeshRendererComponent>((Entity)e)
            && s_reg->Get<MeshRendererComponent>((Entity)e).visible;
    };
    MR["setVisible"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<MeshRendererComponent>((Entity)e))
            s_reg->Get<MeshRendererComponent>((Entity)e).visible = v;
    };
    MR["castsShadow"] = [](int e) -> bool {
        return s_reg && s_reg->Has<MeshRendererComponent>((Entity)e)
            && s_reg->Get<MeshRendererComponent>((Entity)e).castShadow;
    };
    MR["setCastShadow"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<MeshRendererComponent>((Entity)e))
            s_reg->Get<MeshRendererComponent>((Entity)e).castShadow = v;
    };
    MR["getMaterialName"] = [](int e) -> std::string {
        if (!s_reg || !s_reg->Has<MeshRendererComponent>((Entity)e)) return "";
        return s_reg->Get<MeshRendererComponent>((Entity)e).materialName;
    };
    MR["setMaterialName"] = [](int e, const std::string& name) {
        if (s_reg && s_reg->Has<MeshRendererComponent>((Entity)e))
            s_reg->Get<MeshRendererComponent>((Entity)e).materialName = name;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Light table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Light = m_lua.create_named_table("Light");
    Light["hasLight"] = [](int e) -> bool {
        return s_reg && s_reg->Has<LightComponent>((Entity)e);
    };
    Light["getColor"] = [](int e) -> sol::table {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return sol::nil;
        auto& l = s_reg->Get<LightComponent>((Entity)e);
        sol::table r = LuaScriptSystem::Get().GetState().create_table();
        r["r"] = l.color.r; r["g"] = l.color.g; r["b"] = l.color.b;
        return r;
    };
    Light["setColor"] = [](int e, float r, float g, float b) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).color = {r, g, b};
    };
    Light["getIntensity"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).intensity;
    };
    Light["setIntensity"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).intensity = v;
    };
    Light["getRange"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).range;
    };
    Light["setRange"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).range = v;
    };
    Light["getInnerCone"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).innerConeAngle;
    };
    Light["setInnerCone"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).innerConeAngle = v;
    };
    Light["getOuterCone"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).outerConeAngle;
    };
    Light["setOuterCone"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).outerConeAngle = v;
    };
    Light["castsShadow"] = [](int e) -> bool {
        return s_reg && s_reg->Has<LightComponent>((Entity)e)
            && s_reg->Get<LightComponent>((Entity)e).castShadow;
    };
    Light["setCastShadow"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).castShadow = v;
    };
    // getType: "Directional", "Point", "Spot"
    Light["getType"] = [](int e) -> std::string {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return "";
        switch (s_reg->Get<LightComponent>((Entity)e).type) {
            case LightType::Directional: return "Directional";
            case LightType::Point:       return "Point";
            case LightType::Spot:        return "Spot";
        }
        return "";
    };
    Light["setType"] = [](int e, const std::string& t) {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return;
        auto& l = s_reg->Get<LightComponent>((Entity)e);
        if (t == "Directional")   l.type = LightType::Directional;
        else if (t == "Point")    l.type = LightType::Point;
        else if (t == "Spot")     l.type = LightType::Spot;
    };
    // Volumetric
    Light["isVolumetricEnabled"] = [](int e) -> bool {
        return s_reg && s_reg->Has<LightComponent>((Entity)e)
            && s_reg->Get<LightComponent>((Entity)e).volumetricEnabled;
    };
    Light["setVolumetricEnabled"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).volumetricEnabled = v;
    };
    Light["getVolDensity"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).volDensity;
    };
    Light["setVolDensity"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).volDensity = v;
    };
    Light["getVolIntensity"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).volIntensity;
    };
    Light["setVolIntensity"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).volIntensity = v;
    };
    Light["setVolTint"] = [](int e, float r, float g, float b) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).volTint = {r, g, b};
    };
    // Sun Shafts
    Light["isSunShaftsEnabled"] = [](int e) -> bool {
        return s_reg && s_reg->Has<LightComponent>((Entity)e)
            && s_reg->Get<LightComponent>((Entity)e).sunShaftsEnabled;
    };
    Light["setSunShaftsEnabled"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).sunShaftsEnabled = v;
    };
    Light["getShaftsDensity"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).shaftsDensity;
    };
    Light["setShaftsDensity"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).shaftsDensity = v;
    };
    Light["setShaftsTint"] = [](int e, float r, float g, float b) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).shaftsTint = {r, g, b};
    };
    Light["getShaftsExposure"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<LightComponent>((Entity)e)) return 0.f;
        return s_reg->Get<LightComponent>((Entity)e).shaftsExposure;
    };
    Light["setShaftsExposure"] = [](int e, float v) {
        if (s_reg && s_reg->Has<LightComponent>((Entity)e))
            s_reg->Get<LightComponent>((Entity)e).shaftsExposure = v;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Animation table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Anim = m_lua.create_named_table("Animation");
    Anim["hasAnimation"] = [](int e) -> bool {
        return s_reg && s_reg->Has<AnimationComponent>((Entity)e);
    };
    Anim["play"] = [](int e) {
        if (s_reg && s_reg->Has<AnimationComponent>((Entity)e))
            s_reg->Get<AnimationComponent>((Entity)e).playing = true;
    };
    Anim["stop"] = [](int e) {
        if (s_reg && s_reg->Has<AnimationComponent>((Entity)e))
            s_reg->Get<AnimationComponent>((Entity)e).playing = false;
    };
    Anim["isPlaying"] = [](int e) -> bool {
        return s_reg && s_reg->Has<AnimationComponent>((Entity)e)
            && s_reg->Get<AnimationComponent>((Entity)e).playing;
    };
    Anim["setClip"] = [](int e, int idx) {
        if (s_reg && s_reg->Has<AnimationComponent>((Entity)e))
            s_reg->Get<AnimationComponent>((Entity)e).animIndex = idx;
    };
    Anim["getClip"] = [](int e) -> int {
        if (!s_reg || !s_reg->Has<AnimationComponent>((Entity)e)) return 0;
        return s_reg->Get<AnimationComponent>((Entity)e).animIndex;
    };
    Anim["setSpeed"] = [](int e, float v) {
        if (s_reg && s_reg->Has<AnimationComponent>((Entity)e))
            s_reg->Get<AnimationComponent>((Entity)e).speed = v;
    };
    Anim["getSpeed"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<AnimationComponent>((Entity)e)) return 1.f;
        return s_reg->Get<AnimationComponent>((Entity)e).speed;
    };
    Anim["setLoop"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<AnimationComponent>((Entity)e))
            s_reg->Get<AnimationComponent>((Entity)e).loop = v;
    };
    Anim["isLooping"] = [](int e) -> bool {
        return s_reg && s_reg->Has<AnimationComponent>((Entity)e)
            && s_reg->Get<AnimationComponent>((Entity)e).loop;
    };
    Anim["getTime"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<AnimationComponent>((Entity)e)) return 0.f;
        return s_reg->Get<AnimationComponent>((Entity)e).currentTime;
    };
    Anim["setBlendDuration"] = [](int e, float v) {
        if (s_reg && s_reg->Has<AnimationComponent>((Entity)e))
            s_reg->Get<AnimationComponent>((Entity)e).blendDuration = v;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // VolumetricFog table
    // ─────────────────────────────────────────────────────────────────────────
    sol::table Fog = m_lua.create_named_table("VolumetricFog");
    Fog["hasFog"] = [](int e) -> bool {
        return s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e);
    };
    Fog["isEnabled"] = [](int e) -> bool {
        return s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e)
            && s_reg->Get<VolumetricFogComponent>((Entity)e).enabled;
    };
    Fog["setEnabled"] = [](int e, bool v) {
        if (s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e))
            s_reg->Get<VolumetricFogComponent>((Entity)e).enabled = v;
    };
    Fog["getDensity"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<VolumetricFogComponent>((Entity)e)) return 0.f;
        return s_reg->Get<VolumetricFogComponent>((Entity)e).density;
    };
    Fog["setDensity"] = [](int e, float v) {
        if (s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e))
            s_reg->Get<VolumetricFogComponent>((Entity)e).density = v;
    };
    Fog["getFogStart"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<VolumetricFogComponent>((Entity)e)) return 0.f;
        return s_reg->Get<VolumetricFogComponent>((Entity)e).fogStart;
    };
    Fog["setFogStart"] = [](int e, float v) {
        if (s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e))
            s_reg->Get<VolumetricFogComponent>((Entity)e).fogStart = v;
    };
    Fog["getFogEnd"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<VolumetricFogComponent>((Entity)e)) return 0.f;
        return s_reg->Get<VolumetricFogComponent>((Entity)e).fogEnd;
    };
    Fog["setFogEnd"] = [](int e, float v) {
        if (s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e))
            s_reg->Get<VolumetricFogComponent>((Entity)e).fogEnd = v;
    };
    Fog["setColor"] = [](int e, float r, float g, float b) {
        if (s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e))
            s_reg->Get<VolumetricFogComponent>((Entity)e).fogColor = {r, g, b};
    };
    Fog["setScatterStrength"] = [](int e, float v) {
        if (s_reg && s_reg->Has<VolumetricFogComponent>((Entity)e))
            s_reg->Get<VolumetricFogComponent>((Entity)e).scatterStrength = v;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Set s_reg each frame — expose a setter called internally
    // ─────────────────────────────────────────────────────────────────────────
    m_lua["__set_registry"] = [](void* ud) {
        s_reg = static_cast<Registry*>(ud);
    };

    // Global log alias
    m_lua["log"] = [](const std::string& s) { std::cout << "[Lua] " << s << "\n"; };

    // ─────────────────────────────────────────────────────────────────────────
    // Layer table — read / write LayerComponent
    // ─────────────────────────────────────────────────────────────────────────
    sol::table LayerT = m_lua.create_named_table("Layer");

    // Returns true when the entity has a LayerComponent.
    LayerT["hasLayer"] = [](int e) -> bool {
        return s_reg && s_reg->Has<LayerComponent>((Entity)e);
    };
    // Returns the layer index (0-31). Entities without a LayerComponent are layer 0.
    LayerT["getLayer"] = [](int e) -> int {
        if (!s_reg || !s_reg->Has<LayerComponent>((Entity)e)) return 0;
        return static_cast<int>(s_reg->Get<LayerComponent>((Entity)e).layer);
    };
    // Assigns the entity to a layer (0-31). Creates LayerComponent if absent.
    // Marks the physics actor dirty so filter data is refreshed next frame.
    LayerT["setLayer"] = [](int e, int layer) {
        if (!s_reg) return;
        if (!s_reg->Has<LayerComponent>((Entity)e))
            s_reg->Add<LayerComponent>((Entity)e);
        auto& lc = s_reg->Get<LayerComponent>((Entity)e);
        lc.layer = static_cast<uint8_t>(std::clamp(layer, 0, 31));
        PhysicsSystem::Get().MarkDirty((Entity)e);
    };
    // Returns the collision / raycast mask as an integer bitmask.
    LayerT["getMask"] = [](int e) -> int {
        if (!s_reg || !s_reg->Has<LayerComponent>((Entity)e)) return static_cast<int>(0xFFFFFFFFu);
        return static_cast<int>(s_reg->Get<LayerComponent>((Entity)e).layerMask);
    };
    // Sets the collision / raycast mask as a bitmask integer.
    LayerT["setMask"] = [](int e, int mask) {
        if (!s_reg) return;
        if (!s_reg->Has<LayerComponent>((Entity)e))
            s_reg->Add<LayerComponent>((Entity)e);
        s_reg->Get<LayerComponent>((Entity)e).layerMask = static_cast<uint32_t>(mask);
        PhysicsSystem::Get().MarkDirty((Entity)e);
    };
    // Enable or disable interaction with a specific layer index.
    LayerT["setLayerEnabled"] = [](int e, int targetLayer, bool enabled) {
        if (!s_reg) return;
        if (!s_reg->Has<LayerComponent>((Entity)e))
            s_reg->Add<LayerComponent>((Entity)e);
        auto& lc = s_reg->Get<LayerComponent>((Entity)e);
        uint32_t bit = (targetLayer >= 0 && targetLayer < 32) ? (1u << targetLayer) : 0u;
        if (enabled) lc.layerMask |=  bit;
        else         lc.layerMask &= ~bit;
        PhysicsSystem::Get().MarkDirty((Entity)e);
    };
    // Returns true if the given layer bit is set in this entity's mask.
    LayerT["isLayerEnabled"] = [](int e, int targetLayer) -> bool {
        if (!s_reg || !s_reg->Has<LayerComponent>((Entity)e)) return true;
        uint32_t bit = (targetLayer >= 0 && targetLayer < 32) ? (1u << targetLayer) : 0u;
        return (s_reg->Get<LayerComponent>((Entity)e).layerMask & bit) != 0;
    };
    // Explicitly add a LayerComponent with default values (layer=0, mask=0xFFFFFFFF).
    // Safe to call even if the component already exists (no-op in that case).
    LayerT["add"] = [](int e) {
        if (!s_reg) return;
        if (!s_reg->Has<LayerComponent>((Entity)e))
            s_reg->Add<LayerComponent>((Entity)e);
    };
    // Remove the LayerComponent from the entity.
    // The actor will be treated as layer 0 / all-mask on the next SyncActors.
    LayerT["remove"] = [](int e) {
        if (!s_reg) return;
        s_reg->Remove<LayerComponent>((Entity)e);
        PhysicsSystem::Get().MarkDirty((Entity)e);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Physics table — scene queries
    // ─────────────────────────────────────────────────────────────────────────
    sol::table PhysicsT = m_lua.create_named_table("Physics");

    // Physics.raycast(ox, oy, oz, dx, dy, dz, maxDist [, layerMask]) -> hit table
    //   Casts a ray from origin (ox,oy,oz) in direction (dx,dy,dz) up to maxDist.
    //   layerMask is optional (default 0xFFFFFFFF = all layers).
    //   Returns: { hit=bool, entity=int, distance=float,
    //              x=float, y=float, z=float,       -- hit world position
    //              nx=float, ny=float, nz=float }     -- surface normal
    PhysicsT["raycast"] = [this](float ox, float oy, float oz,
                                 float dx, float dy, float dz,
                                 float maxDist,
                                 sol::optional<int> maskOpt) -> sol::table
    {
        uint32_t mask = maskOpt.has_value()
                        ? static_cast<uint32_t>(maskOpt.value())
                        : 0xFFFFFFFFu;
        RaycastHit h = PhysicsSystem::Get().Raycast(
            {ox, oy, oz}, {dx, dy, dz}, maxDist, mask);

        sol::table t = m_lua.create_table();
        t["hit"]      = h.hit;
        t["entity"]   = static_cast<int>(h.entity);
        t["distance"] = h.distance;
        t["x"]        = h.position.x;
        t["y"]        = h.position.y;
        t["z"]        = h.position.z;
        t["nx"]       = h.normal.x;
        t["ny"]       = h.normal.y;
        t["nz"]       = h.normal.z;
        return t;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Script — cross-entity script communication
    // ─────────────────────────────────────────────────────────────────────────
    auto Script = m_lua.create_named_table("Script");

    // Script.call(entity, "FunctionName", arg1, arg2, ...)
    // Calls a Lua function defined in another entity's script environment.
    // Returns up to 8 values from the callee, or nothing on error.
    Script["call"] = [this](int e, const std::string& fn, sol::variadic_args va) -> sol::object
    {
        auto it = m_envs.find((Entity)e);
        if (it == m_envs.end()) {
            std::cerr << "[Script.call] entity " << e << " has no script environment\n";
            return sol::nil;
        }
        sol::environment& env = it->second;
        sol::protected_function func = env[fn];
        if (!func.valid()) {
            std::cerr << "[Script.call] function '" << fn << "' not found on entity " << e << "\n";
            return sol::nil;
        }
        std::vector<sol::object> args(va.begin(), va.end());
        sol::protected_function_result res = func(sol::as_args(args));
        if (!res.valid()) {
            sol::error err = res;
            std::cerr << "[Script.call] error calling '" << fn << "' on entity " << e << ": " << err.what() << "\n";
            return sol::nil;
        }
        // Return first result (or nil)
        if (res.return_count() > 0)
            return res.get<sol::object>(0);
        return sol::nil;
    };

    // Script.get(entity, "varName")
    // Reads a global variable from another entity's script environment.
    Script["get"] = [this](int e, const std::string& key) -> sol::object
    {
        auto it = m_envs.find((Entity)e);
        if (it == m_envs.end()) return sol::nil;
        return it->second[key];
    };

    // Script.set(entity, "varName", value)
    // Writes a value into another entity's script environment.
    Script["set"] = [this](int e, const std::string& key, sol::object value)
    {
        auto it = m_envs.find((Entity)e);
        if (it == m_envs.end()) return;
        it->second[key] = value;
    };

    // Script.has(entity)
    // Returns true if the entity has an active (loaded) script environment.
    Script["has"] = [this](int e) -> bool
    {
        return m_envs.count((Entity)e) > 0;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // System — OS / filesystem utilities
    // ─────────────────────────────────────────────────────────────────────────
    auto Sys = m_lua.create_named_table("System");

    // ── Time ────────────────────────────────────────────────────────────────
    // Returns seconds since epoch (same as os.time())
    Sys["getTime"] = []() -> double {
        return static_cast<double>(std::time(nullptr));
    };

    // Returns formatted date string. format uses strftime codes.
    // Default: "%Y-%m-%d %H:%M:%S"  → "2026-03-04 14:30:00"
    Sys["getDate"] = [](sol::optional<std::string> fmt) -> std::string {
        std::time_t t = std::time(nullptr);
        struct tm tm_info;
#ifdef _WIN32
        localtime_s(&tm_info, &t);
#else
        localtime_r(&t, &tm_info);
#endif
        char buf[128];
        const char* f = fmt.has_value() ? fmt->c_str() : "%Y-%m-%d %H:%M:%S";
        std::strftime(buf, sizeof(buf), f, &tm_info);
        return std::string(buf);
    };

    // Returns seconds since program start (high-resolution, good for profiling)
    Sys["getClock"] = []() -> double {
        return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
    };

    // ── Environment / machine ────────────────────────────────────────────────
    // Returns an environment variable value, or "" if not found.
    Sys["getEnv"] = [](const std::string& name) -> std::string {
        const char* v = std::getenv(name.c_str());
        return v ? std::string(v) : std::string{};
    };

    // Returns the machine name (COMPUTERNAME on Windows, HOSTNAME elsewhere)
    Sys["getComputerName"] = []() -> std::string {
#ifdef _WIN32
        const char* v = std::getenv("COMPUTERNAME");
#else
        const char* v = std::getenv("HOSTNAME");
#endif
        return v ? std::string(v) : std::string{};
    };

    // Returns the username of the current session
    Sys["getUserName"] = []() -> std::string {
#ifdef _WIN32
        const char* v = std::getenv("USERNAME");
#else
        const char* v = std::getenv("USER");
#endif
        return v ? std::string(v) : std::string{};
    };

    // ── Working directory ────────────────────────────────────────────────────
    Sys["getCwd"] = []() -> std::string {
        std::error_code ec;
        auto p = fs::current_path(ec);
        return ec ? std::string{} : p.string();
    };

    // ── Files ────────────────────────────────────────────────────────────────
    // Read entire file as a string. Returns nil if the file cannot be opened.
    Sys["readFile"] = [this](const std::string& path) -> sol::object {
        std::ifstream f(path, std::ios::binary);
        if (!f) return sol::nil;
        std::ostringstream ss;
        ss << f.rdbuf();
        return sol::make_object(m_lua, ss.str());
    };

    // Write (overwrite) a file with a string. Returns true on success.
    Sys["writeFile"] = [](const std::string& path, const std::string& content) -> bool {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << content;
        return f.good();
    };

    // Append a string to a file. Returns true on success.
    Sys["appendFile"] = [](const std::string& path, const std::string& content) -> bool {
        std::ofstream f(path, std::ios::binary | std::ios::app);
        if (!f) return false;
        f << content;
        return f.good();
    };

    // Delete a file. Returns true if deleted.
    Sys["deleteFile"] = [](const std::string& path) -> bool {
        std::error_code ec;
        return fs::remove(path, ec);
    };

    // Returns true if path exists (file or directory)
    Sys["exists"] = [](const std::string& path) -> bool {
        std::error_code ec;
        return fs::exists(path, ec);
    };

    // Returns true if path is a regular file
    Sys["isFile"] = [](const std::string& path) -> bool {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    };

    // Returns true if path is a directory
    Sys["isDir"] = [](const std::string& path) -> bool {
        std::error_code ec;
        return fs::is_directory(path, ec);
    };

    // ── Directories ──────────────────────────────────────────────────────────
    // Create directory (including all parents). Returns true on success.
    Sys["createDir"] = [](const std::string& path) -> bool {
        std::error_code ec;
        return fs::create_directories(path, ec);
    };

    // List files in a directory. Returns a Lua array of file-name strings.
    // If recursive=true it walks subdirectories too.
    Sys["listFiles"] = [this](const std::string& path, sol::optional<bool> recursive) -> sol::table {
        sol::table t = m_lua.create_table();
        std::error_code ec;
        if (!fs::exists(path, ec)) return t;
        int idx = 1;
        auto push = [&](const fs::path& p) {
            if (fs::is_regular_file(p, ec))
                t[idx++] = p.filename().string();
        };
        if (recursive.value_or(false)) {
            for (auto& entry : fs::recursive_directory_iterator(path, ec))
                push(entry.path());
        } else {
            for (auto& entry : fs::directory_iterator(path, ec))
                push(entry.path());
        }
        return t;
    };

    // List subdirectory names in a directory. Returns a Lua array of strings.
    Sys["listDirs"] = [this](const std::string& path) -> sol::table {
        sol::table t = m_lua.create_table();
        std::error_code ec;
        if (!fs::exists(path, ec)) return t;
        int idx = 1;
        for (auto& entry : fs::directory_iterator(path, ec))
            if (fs::is_directory(entry.path(), ec))
                t[idx++] = entry.path().filename().string();
        return t;
    };

    // Returns file size in bytes, or -1 on error.
    Sys["fileSize"] = [](const std::string& path) -> long long {
        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        return ec ? -1LL : static_cast<long long>(sz);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Debug — visual debug drawing (collider gizmos, rays, lines)
    // ─────────────────────────────────────────────────────────────────────────
    auto Debug = m_lua.create_named_table("Debug");

    // Debug.drawLine(x1,y1,z1, x2,y2,z2 [,r,g,b,a [,duration]])
    // Draws a line for one frame (duration=0) or N seconds.
    // Default color: green (0,1,0,1).
    Debug["drawLine"] = [](float x1, float y1, float z1,
                            float x2, float y2, float z2,
                            sol::variadic_args va)
    {
        glm::vec4 color(0.f, 1.f, 0.f, 1.f);
        float     duration = 0.f;
        int i = 0;
        if (va.size() > static_cast<std::size_t>(i)) color.r   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) color.g   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) color.b   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) color.a   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) duration   = va[i].get<float>();
        Krayon::DebugDraw::Get().AddLine({x1,y1,z1}, {x2,y2,z2}, color, duration);
    };

    // Debug.drawRay(ox,oy,oz, dx,dy,dz [,r,g,b,a [,duration]])
    // Draws a ray: origin + direction (magnitude = length). Default color: green.
    Debug["drawRay"] = [](float ox, float oy, float oz,
                           float dx, float dy, float dz,
                           sol::variadic_args va)
    {
        glm::vec4 color(0.f, 1.f, 0.f, 1.f);
        float     duration = 0.f;
        int i = 0;
        if (va.size() > static_cast<std::size_t>(i)) color.r   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) color.g   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) color.b   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) color.a   = va[i++].get<float>();
        if (va.size() > static_cast<std::size_t>(i)) duration   = va[i].get<float>();
        Krayon::DebugDraw::Get().AddRay({ox,oy,oz}, {dx,dy,dz}, color, duration);
    };

    // Debug.log(message)
    // Prints a message to stdout prefixed with "[Lua]".
    Debug["log"] = [](const std::string& msg) {
        std::cout << "[Lua] " << msg << '\n';
    };

    // ── Camera table ──────────────────────────────────────────────────────────
    // Controls per-entity game cameras.  Position/orientation come from the
    // entity's TransformComponent; only the entity with isActive=true drives
    // the renderer's main camera each frame.
    sol::table Cam = m_lua.create_named_table("Camera");

    Cam["hasCamera"] = [](int e) -> bool {
        return s_reg && s_reg->Has<CameraComponent>((Entity)e);
    };

    // add(e) — attach a CameraComponent with default values (fov=45, near=0.1, far=500, inactive)
    Cam["add"] = [](int e) {
        if (s_reg && !s_reg->Has<CameraComponent>((Entity)e))
            s_reg->Add<CameraComponent>((Entity)e);
    };

    // remove(e) — detach the CameraComponent
    Cam["remove"] = [](int e) {
        if (s_reg && s_reg->Has<CameraComponent>((Entity)e))
            s_reg->Remove<CameraComponent>((Entity)e);
    };

    // setActive(e) — make e the active camera, deactivate all others
    Cam["setActive"] = [](int e) {
        if (!s_reg) return;
        s_reg->Each<CameraComponent>([&](Entity ent, CameraComponent& c) {
            c.isActive = ((int)ent == e);
        });
        // Auto-add component if the entity doesn't have one yet
        if (s_reg && !s_reg->Has<CameraComponent>((Entity)e))
        {
            s_reg->Add<CameraComponent>((Entity)e);
            s_reg->Get<CameraComponent>((Entity)e).isActive = true;
        }
    };

    // getActive() — returns entity id of the active camera, or 0 if none
    Cam["getActive"] = []() -> int {
        if (!s_reg) return 0;
        int active = 0;
        s_reg->Each<CameraComponent>([&](Entity e, CameraComponent& c) {
            if (c.isActive) active = (int)e;
        });
        return active;
    };

    Cam["isActive"] = [](int e) -> bool {
        if (!s_reg || !s_reg->Has<CameraComponent>((Entity)e)) return false;
        return s_reg->Get<CameraComponent>((Entity)e).isActive;
    };

    Cam["getFOV"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<CameraComponent>((Entity)e)) return 45.0f;
        return s_reg->Get<CameraComponent>((Entity)e).fov;
    };
    Cam["setFOV"] = [](int e, float v) {
        if (s_reg && s_reg->Has<CameraComponent>((Entity)e))
            s_reg->Get<CameraComponent>((Entity)e).fov = v;
    };

    Cam["getNear"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<CameraComponent>((Entity)e)) return 0.1f;
        return s_reg->Get<CameraComponent>((Entity)e).nearPlane;
    };
    Cam["setNear"] = [](int e, float v) {
        if (s_reg && s_reg->Has<CameraComponent>((Entity)e))
            s_reg->Get<CameraComponent>((Entity)e).nearPlane = v;
    };

    Cam["getFar"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<CameraComponent>((Entity)e)) return 500.0f;
        return s_reg->Get<CameraComponent>((Entity)e).farPlane;
    };
    Cam["setFar"] = [](int e, float v) {
        if (s_reg && s_reg->Has<CameraComponent>((Entity)e))
            s_reg->Get<CameraComponent>((Entity)e).farPlane = v;
    };

    // ── CharacterController table ────────────────────────────────────────────────
    // Capsule-based kinematic character controller.
    // Call CharacterController.move(e, dx, dy, dz) every frame to move an entity.
    sol::table CC = m_lua.create_named_table("CharacterController");

    CC["has"] = [](int e) -> bool {
        return s_reg && s_reg->Has<CharacterControllerComponent>((Entity)e);
    };

    // add(e) — attach a CharacterControllerComponent with default values
    CC["add"] = [](int e) {
        if (s_reg && !s_reg->Has<CharacterControllerComponent>((Entity)e))
            s_reg->Add<CharacterControllerComponent>((Entity)e);
    };

    // remove(e) — detach the CharacterControllerComponent
    CC["remove"] = [](int e) {
        if (s_reg && s_reg->Has<CharacterControllerComponent>((Entity)e))
        {
            s_reg->Remove<CharacterControllerComponent>((Entity)e);
            PhysicsSystem::Get().MarkDirty((Entity)e);
        }
    };

    // move(e, dx, dy, dz) — sweep-move the capsule controller by a world-space displacement
    //   dx/dy/dz are in metres (apply gravity + jump + speed * dt yourself)
    //   Writes back position to TransformComponent and updates isGrounded.
    CC["move"] = [](int e, float dx, float dy, float dz) {
        if (!s_reg) return;
        PhysicsSystem::Get().MoveController((Entity)e, {dx, dy, dz}, 1.0f / 60.0f);
        // WriteBackControllers is called each fixed-step in the game loop,
        // but calling it here too ensures Lua sees position immediately.
        PhysicsSystem::Get().WriteBackControllers(*s_reg);
    };

    // isGrounded(e) — returns true when the controller is touching the ground
    CC["isGrounded"] = [](int e) -> bool {
        if (!s_reg || !s_reg->Has<CharacterControllerComponent>((Entity)e)) return false;
        return s_reg->Get<CharacterControllerComponent>((Entity)e).isGrounded;
    };

    // getVelocity(e) — returns last-frame displacement as {x,y,z}
    CC["getVelocity"] = [](int e) -> std::tuple<float,float,float> {
        if (!s_reg || !s_reg->Has<CharacterControllerComponent>((Entity)e))
            return {0.f, 0.f, 0.f};
        const auto& v = s_reg->Get<CharacterControllerComponent>((Entity)e).velocity;
        return {v.x, v.y, v.z};
    };

    CC["getRadius"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<CharacterControllerComponent>((Entity)e)) return 0.3f;
        return s_reg->Get<CharacterControllerComponent>((Entity)e).radius;
    };
    CC["setRadius"] = [](int e, float v) {
        if (s_reg && s_reg->Has<CharacterControllerComponent>((Entity)e))
        {
            s_reg->Get<CharacterControllerComponent>((Entity)e).radius = v;
            PhysicsSystem::Get().MarkDirty((Entity)e);
        }
    };

    CC["getHeight"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<CharacterControllerComponent>((Entity)e)) return 1.8f;
        return s_reg->Get<CharacterControllerComponent>((Entity)e).height;
    };
    CC["setHeight"] = [](int e, float v) {
        if (s_reg && s_reg->Has<CharacterControllerComponent>((Entity)e))
        {
            s_reg->Get<CharacterControllerComponent>((Entity)e).height = v;
            PhysicsSystem::Get().MarkDirty((Entity)e);
        }
    };

    CC["getSlopeLimit"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<CharacterControllerComponent>((Entity)e)) return 45.f;
        return s_reg->Get<CharacterControllerComponent>((Entity)e).slopeLimit;
    };
    CC["setSlopeLimit"] = [](int e, float deg) {
        if (s_reg && s_reg->Has<CharacterControllerComponent>((Entity)e))
        {
            s_reg->Get<CharacterControllerComponent>((Entity)e).slopeLimit = deg;
            PhysicsSystem::Get().MarkDirty((Entity)e);
        }
    };

    CC["getStepOffset"] = [](int e) -> float {
        if (!s_reg || !s_reg->Has<CharacterControllerComponent>((Entity)e)) return 0.3f;
        return s_reg->Get<CharacterControllerComponent>((Entity)e).stepOffset;
    };
    CC["setStepOffset"] = [](int e, float v) {
        if (s_reg && s_reg->Has<CharacterControllerComponent>((Entity)e))
        {
            s_reg->Get<CharacterControllerComponent>((Entity)e).stepOffset = v;
            PhysicsSystem::Get().MarkDirty((Entity)e);
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Input — keyboard, mouse buttons, mouse motion
    // Updated once per frame via __update_input() before any OnUpdate call.
    // ─────────────────────────────────────────────────────────────────────────
    static bool     s_keysPrev[SDL_SCANCODE_COUNT] = {};
    static bool     s_keysCurr[SDL_SCANCODE_COUNT] = {};
    static uint32_t s_mousePrev = 0;
    static uint32_t s_mouseCurr = 0;
    static float    s_mouseX    = 0.f, s_mouseY  = 0.f;
    static float    s_mouseDX   = 0.f, s_mouseDY = 0.f;

    m_lua["__update_input"] = []() {
        std::memcpy(s_keysPrev, s_keysCurr, sizeof(s_keysCurr));
        s_mousePrev = s_mouseCurr;
        int numKeys = 0;
        const bool* keys = SDL_GetKeyboardState(&numKeys);
        int n = (numKeys < SDL_SCANCODE_COUNT) ? numKeys : SDL_SCANCODE_COUNT;
        std::memcpy(s_keysCurr, keys, static_cast<std::size_t>(n) * sizeof(bool));
        s_mouseCurr = SDL_GetMouseState(&s_mouseX, &s_mouseY);
        SDL_GetRelativeMouseState(&s_mouseDX, &s_mouseDY);
    };

    sol::table Inp = m_lua.create_named_table("Input");

    // ── Keyboard ──────────────────────────────────────────────────────────────
    // getKey(scancode)     — true while key is held
    Inp["getKey"] = [](int sc) -> bool {
        return (sc >= 0 && sc < SDL_SCANCODE_COUNT) && s_keysCurr[sc];
    };
    // getKeyDown(scancode) — true only on the first frame the key goes down
    Inp["getKeyDown"] = [](int sc) -> bool {
        return (sc >= 0 && sc < SDL_SCANCODE_COUNT) && s_keysCurr[sc] && !s_keysPrev[sc];
    };
    // getKeyUp(scancode)   — true only on the first frame the key is released
    Inp["getKeyUp"] = [](int sc) -> bool {
        return (sc >= 0 && sc < SDL_SCANCODE_COUNT) && !s_keysCurr[sc] && s_keysPrev[sc];
    };

    // ── Mouse ─────────────────────────────────────────────────────────────────
    // getMouseButton(btn)     — btn: 1=left 2=middle 3=right; true while held
    Inp["getMouseButton"] = [](int btn) -> bool {
        if (btn < 1 || btn > 5) return false;
        return (s_mouseCurr & SDL_BUTTON_MASK(btn)) != 0;
    };
    // getMouseButtonDown(btn) — true only on the frame it was first pressed
    Inp["getMouseButtonDown"] = [](int btn) -> bool {
        if (btn < 1 || btn > 5) return false;
        uint32_t mask = SDL_BUTTON_MASK(btn);
        return (s_mouseCurr & mask) != 0 && (s_mousePrev & mask) == 0;
    };
    // getMouseButtonUp(btn)   — true only on the frame it was released
    Inp["getMouseButtonUp"] = [](int btn) -> bool {
        if (btn < 1 || btn > 5) return false;
        uint32_t mask = SDL_BUTTON_MASK(btn);
        return (s_mouseCurr & mask) == 0 && (s_mousePrev & mask) != 0;
    };
    // getMousePosition() — cursor x, y in window pixels (two return values)
    Inp["getMousePosition"] = []() -> std::tuple<float, float> {
        return { s_mouseX, s_mouseY };
    };
    // getMouseDelta() — relative motion since last frame, dx, dy (two values)
    Inp["getMouseDelta"] = []() -> std::tuple<float, float> {
        return { s_mouseDX, s_mouseDY };
    };

    // ── Axis helpers ──────────────────────────────────────────────────────────
    // getAxis("Horizontal") — A/Left=-1 .. D/Right=+1
    // getAxis("Vertical")   — S/Down=-1 .. W/Up=+1
    Inp["getAxis"] = [](const std::string& axis) -> float {
        if (axis == "Horizontal") {
            float v = 0.f;
            if (s_keysCurr[SDL_SCANCODE_D] || s_keysCurr[SDL_SCANCODE_RIGHT]) v += 1.f;
            if (s_keysCurr[SDL_SCANCODE_A] || s_keysCurr[SDL_SCANCODE_LEFT])  v -= 1.f;
            return v;
        }
        if (axis == "Vertical") {
            float v = 0.f;
            if (s_keysCurr[SDL_SCANCODE_W] || s_keysCurr[SDL_SCANCODE_UP])   v += 1.f;
            if (s_keysCurr[SDL_SCANCODE_S] || s_keysCurr[SDL_SCANCODE_DOWN]) v -= 1.f;
            return v;
        }
        return 0.f;
    };

    // ── Named key constants ───────────────────────────────────────────────────
    // Letters
    Inp["KEY_A"] = (int)SDL_SCANCODE_A;  Inp["KEY_B"] = (int)SDL_SCANCODE_B;
    Inp["KEY_C"] = (int)SDL_SCANCODE_C;  Inp["KEY_D"] = (int)SDL_SCANCODE_D;
    Inp["KEY_E"] = (int)SDL_SCANCODE_E;  Inp["KEY_F"] = (int)SDL_SCANCODE_F;
    Inp["KEY_G"] = (int)SDL_SCANCODE_G;  Inp["KEY_H"] = (int)SDL_SCANCODE_H;
    Inp["KEY_I"] = (int)SDL_SCANCODE_I;  Inp["KEY_J"] = (int)SDL_SCANCODE_J;
    Inp["KEY_K"] = (int)SDL_SCANCODE_K;  Inp["KEY_L"] = (int)SDL_SCANCODE_L;
    Inp["KEY_M"] = (int)SDL_SCANCODE_M;  Inp["KEY_N"] = (int)SDL_SCANCODE_N;
    Inp["KEY_O"] = (int)SDL_SCANCODE_O;  Inp["KEY_P"] = (int)SDL_SCANCODE_P;
    Inp["KEY_Q"] = (int)SDL_SCANCODE_Q;  Inp["KEY_R"] = (int)SDL_SCANCODE_R;
    Inp["KEY_S"] = (int)SDL_SCANCODE_S;  Inp["KEY_T"] = (int)SDL_SCANCODE_T;
    Inp["KEY_U"] = (int)SDL_SCANCODE_U;  Inp["KEY_V"] = (int)SDL_SCANCODE_V;
    Inp["KEY_W"] = (int)SDL_SCANCODE_W;  Inp["KEY_X"] = (int)SDL_SCANCODE_X;
    Inp["KEY_Y"] = (int)SDL_SCANCODE_Y;  Inp["KEY_Z"] = (int)SDL_SCANCODE_Z;
    // Numbers
    Inp["KEY_0"] = (int)SDL_SCANCODE_0;  Inp["KEY_1"] = (int)SDL_SCANCODE_1;
    Inp["KEY_2"] = (int)SDL_SCANCODE_2;  Inp["KEY_3"] = (int)SDL_SCANCODE_3;
    Inp["KEY_4"] = (int)SDL_SCANCODE_4;  Inp["KEY_5"] = (int)SDL_SCANCODE_5;
    Inp["KEY_6"] = (int)SDL_SCANCODE_6;  Inp["KEY_7"] = (int)SDL_SCANCODE_7;
    Inp["KEY_8"] = (int)SDL_SCANCODE_8;  Inp["KEY_9"] = (int)SDL_SCANCODE_9;
    // Special
    Inp["KEY_SPACE"]     = (int)SDL_SCANCODE_SPACE;
    Inp["KEY_RETURN"]    = (int)SDL_SCANCODE_RETURN;
    Inp["KEY_ESCAPE"]    = (int)SDL_SCANCODE_ESCAPE;
    Inp["KEY_BACKSPACE"] = (int)SDL_SCANCODE_BACKSPACE;
    Inp["KEY_TAB"]       = (int)SDL_SCANCODE_TAB;
    Inp["KEY_LSHIFT"]    = (int)SDL_SCANCODE_LSHIFT;
    Inp["KEY_RSHIFT"]    = (int)SDL_SCANCODE_RSHIFT;
    Inp["KEY_LCTRL"]     = (int)SDL_SCANCODE_LCTRL;
    Inp["KEY_RCTRL"]     = (int)SDL_SCANCODE_RCTRL;
    Inp["KEY_LALT"]      = (int)SDL_SCANCODE_LALT;
    Inp["KEY_RALT"]      = (int)SDL_SCANCODE_RALT;
    // Arrow keys
    Inp["KEY_UP"]        = (int)SDL_SCANCODE_UP;
    Inp["KEY_DOWN"]      = (int)SDL_SCANCODE_DOWN;
    Inp["KEY_LEFT"]      = (int)SDL_SCANCODE_LEFT;
    Inp["KEY_RIGHT"]     = (int)SDL_SCANCODE_RIGHT;
    // Function keys
    Inp["KEY_F1"]  = (int)SDL_SCANCODE_F1;  Inp["KEY_F2"]  = (int)SDL_SCANCODE_F2;
    Inp["KEY_F3"]  = (int)SDL_SCANCODE_F3;  Inp["KEY_F4"]  = (int)SDL_SCANCODE_F4;
    Inp["KEY_F5"]  = (int)SDL_SCANCODE_F5;  Inp["KEY_F6"]  = (int)SDL_SCANCODE_F6;
    Inp["KEY_F7"]  = (int)SDL_SCANCODE_F7;  Inp["KEY_F8"]  = (int)SDL_SCANCODE_F8;
    Inp["KEY_F9"]  = (int)SDL_SCANCODE_F9;  Inp["KEY_F10"] = (int)SDL_SCANCODE_F10;
    Inp["KEY_F11"] = (int)SDL_SCANCODE_F11; Inp["KEY_F12"] = (int)SDL_SCANCODE_F12;
    // Mouse button constants
    Inp["MB_LEFT"]   = 1;
    Inp["MB_MIDDLE"] = 2;
    Inp["MB_RIGHT"]  = 3;
}
