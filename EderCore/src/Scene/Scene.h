#pragma once
#include "ECS/Registry.h"
#include "ECS/Systems/System.h"
#include <string>
#include <vector>
#include <memory>

namespace EderCore {

class Scene
{
public:
    explicit Scene(std::string name) : _name(std::move(name)) {}

    // ── Metadata ─────────────────────────────────────────────────────────────
    const std::string& GetName() const { return _name; }

    // ── Registry ─────────────────────────────────────────────────────────────
    Registry& GetRegistry() { return _registry; }

    // ── Systems ──────────────────────────────────────────────────────────────

    // Agrega un sistema y toma ownership. Devuelve referencia al sistema.
    template<typename T, typename... Args>
    T& AddSystem(Args&&... args)
    {
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref   = *ptr;
        _systems.push_back(std::move(ptr));
        return ref;
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void OnStart()
    {
        for (auto& sys : _systems)
            sys->OnStart(_registry);
    }

    void OnUpdate(float dt)
    {
        for (auto& sys : _systems)
            sys->OnUpdate(_registry, dt);
    }

    void OnStop()
    {
        for (auto& sys : _systems)
            sys->OnStop(_registry);
    }

private:
    std::string                          _name;
    Registry                             _registry;
    std::vector<std::unique_ptr<System>> _systems;
};

} // namespace EderCore
