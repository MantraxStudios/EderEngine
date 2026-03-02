#pragma once
#include "Scene.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <stdexcept>

namespace EderCore {

// ─────────────────────────────────────────────────────────────────────────────
// SceneManager
//
// Uso:
//   auto& menu  = SceneManager::Get().Register<MenuScene>("Menu");
//   auto& game  = SceneManager::Get().Register<GameScene>("Game");
//
//   SceneManager::Get().LoadScene("Menu");      // carga inmediata
//   SceneManager::Get().QueueScene("Game");     // transición al final del frame
//   SceneManager::Get().Update(dt);             // llama OnUpdate en la escena activa
//   SceneManager::Get().FlushPending();         // aplica QueueScene (llamar al final del frame)
// ─────────────────────────────────────────────────────────────────────────────
class SceneManager
{
public:
    static SceneManager& Get()
    {
        static SceneManager instance;
        return instance;
    }

    // ── Registro ─────────────────────────────────────────────────────────────

    // Registra una escena derivada de Scene. Devuelve referencia a ella.
    template<typename T, typename... Args>
    T& Register(const std::string& name, Args&&... args)
    {
        static_assert(std::is_base_of_v<Scene, T>,
                      "T must derive from Scene");
        auto ptr = std::make_unique<T>(name, std::forward<Args>(args)...);
        T& ref   = *ptr;
        _scenes[name] = std::move(ptr);
        return ref;
    }

    // Registra una Scene base genérica (sin subclasificar).
    Scene& Register(const std::string& name)
    {
        auto ptr = std::make_unique<Scene>(name);
        Scene& ref = *ptr;
        _scenes[name] = std::move(ptr);
        return ref;
    }

    // ── Carga ─────────────────────────────────────────────────────────────────

    // Transición inmediata (detiene la actual, inicia la nueva).
    void LoadScene(const std::string& name)
    {
        if (_active) _active->OnStop();

        auto it = _scenes.find(name);
        if (it == _scenes.end())
            throw std::runtime_error("SceneManager: scene not found: " + name);

        _active = it->second.get();
        _active->OnStart();
        _pending.clear();
    }

    // Encola una transición para el final del frame.
    void QueueScene(const std::string& name) { _pending = name; }

    // Aplica la transición pendiente si existe. Llamar al final del game loop.
    void FlushPending()
    {
        if (!_pending.empty())
        {
            LoadScene(_pending);
            _pending.clear();
        }
    }

    // ── Update ───────────────────────────────────────────────────────────────

    void Update(float dt)
    {
        if (_active) _active->OnUpdate(dt);
    }

    // ── Consultas ────────────────────────────────────────────────────────────

    Scene*             GetActive()                        { return _active; }
    bool               HasScene(const std::string& name)  { return _scenes.count(name) > 0; }

    Scene* GetScene(const std::string& name)
    {
        auto it = _scenes.find(name);
        return (it != _scenes.end()) ? it->second.get() : nullptr;
    }

private:
    SceneManager() = default;

    std::unordered_map<std::string, std::unique_ptr<Scene>> _scenes;
    Scene*      _active  = nullptr;
    std::string _pending;
};

} // namespace EderCore
