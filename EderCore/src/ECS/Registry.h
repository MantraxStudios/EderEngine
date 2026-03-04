#pragma once
#include "Entity.h"
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <typeindex>
#include <vector>
#include <functional>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// ComponentPool — stores one component type for all entities
// ─────────────────────────────────────────────────────────────────────────────
struct IComponentPool
{
    virtual ~IComponentPool() = default;
    virtual void Remove(Entity e) = 0;
};

template<typename T>
struct ComponentPool : IComponentPool
{
    std::unordered_map<Entity, T> data;

    void Remove(Entity e) override { data.erase(e); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Registry
// ─────────────────────────────────────────────────────────────────────────────
class Registry
{
public:
    // ── Entity management ────────────────────────────────────────────────────

    Entity Create()
    {
        Entity e = ++_next;
        _alive.push_back(e);
        return e;
    }

    void Destroy(Entity e)
    {
        for (auto& [_, pool] : _pools)
            pool->Remove(e);

        auto it = std::find(_alive.begin(), _alive.end(), e);
        if (it != _alive.end()) _alive.erase(it);
    }

    const std::vector<Entity>& GetEntities() const { return _alive; }

    // ── Component access ─────────────────────────────────────────────────────

    template<typename T, typename... Args>
    T& Add(Entity e, Args&&... args)
    {
        auto& pool = GetOrCreatePool<T>();
        pool.data.emplace(e, T{std::forward<Args>(args)...});
        return pool.data.at(e);
    }

    template<typename T>
    T& Get(Entity e)
    {
        return GetOrCreatePool<T>().data.at(e);
    }

    template<typename T>
    const T& Get(Entity e) const
    {
        auto it = _pools.find(typeid(T));
        if (it == _pools.end()) throw std::out_of_range("Component not found");
        return static_cast<const ComponentPool<T>&>(*it->second).data.at(e);
    }

    template<typename T>
    bool Has(Entity e) const
    {
        auto it = _pools.find(typeid(T));
        if (it == _pools.end()) return false;
        return static_cast<const ComponentPool<T>&>(*it->second).data.count(e) > 0;
    }

    template<typename T>
    void Remove(Entity e)
    {
        auto it = _pools.find(typeid(T));
        if (it != _pools.end()) it->second->Remove(e);
    }

    // ── Iteration ────────────────────────────────────────────────────────────

    // Each<T>(fn) — iterates entities that have component T
    template<typename T>
    void Each(std::function<void(Entity, T&)> fn)
    {
        auto it = _pools.find(typeid(T));
        if (it == _pools.end()) return;
        auto& pool = static_cast<ComponentPool<T>&>(*it->second);
        for (auto& [e, comp] : pool.data)
            fn(e, comp);
    }

    // Each<T1,T2>(fn) — iterates entities that have ALL listed components
    template<typename T1, typename T2>
    void Each(std::function<void(Entity, T1&, T2&)> fn)
    {
        auto it = _pools.find(typeid(T1));
        if (it == _pools.end()) return;
        auto& pool = static_cast<ComponentPool<T1>&>(*it->second);
        for (auto& [e, c1] : pool.data)
        {
            if (Has<T2>(e))
                fn(e, c1, Get<T2>(e));
        }
    }

    template<typename T1, typename T2, typename T3>
    void Each(std::function<void(Entity, T1&, T2&, T3&)> fn)
    {
        auto it = _pools.find(typeid(T1));
        if (it == _pools.end()) return;
        auto& pool = static_cast<ComponentPool<T1>&>(*it->second);
        for (auto& [e, c1] : pool.data)
        {
            if (Has<T2>(e) && Has<T3>(e))
                fn(e, c1, Get<T2>(e), Get<T3>(e));
        }
    }

    // ── Bulk operations ──────────────────────────────────────────────────────

    /// Destroy all entities and wipe every component pool.
    /// After Clear() the registry is in the same state as a freshly constructed one.
    void Clear()
    {
        _pools.clear();
        _alive.clear();
        _next = NULL_ENTITY;
    }

private:
    Entity _next = NULL_ENTITY;
    std::vector<Entity> _alive;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> _pools;

    template<typename T>
    ComponentPool<T>& GetOrCreatePool()
    {
        auto& ptr = _pools[typeid(T)];
        if (!ptr) ptr = std::make_unique<ComponentPool<T>>();
        return static_cast<ComponentPool<T>&>(*ptr);
    }
};
