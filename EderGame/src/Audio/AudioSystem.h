#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  AudioSystem
//  Singleton that owns the FMOD::System instance.
//  Call order each frame:
//      AudioSystem::Get().Update(registry);   // after LuaScriptSystem::Update
//  Lifecycle:
//      AudioSystem::Get().Init();             // once at startup
//      AudioSystem::Get().Shutdown();         // once at shutdown
// ─────────────────────────────────────────────────────────────────────────────

#include <inc/fmod.hpp>
#include <unordered_map>
#include <glm/glm.hpp>
#include "ECS/Registry.h"
#include "ECS/Entity.h"

class AudioSystem
{
public:
    static AudioSystem& Get();

    void Init    ();
    void Shutdown();

    // Per-frame: handles playOnAwake, syncs 3D positions, updates FMOD.
    void Update(Registry& registry, float dt);

    // Manually control playback (called from Lua bindings too).
    void Play    (Entity e, Registry& registry);
    void Stop    (Entity e);
    void Pause   (Entity e);
    void Resume  (Entity e);
    bool IsPlaying(Entity e) const;

    // Set volume of a running channel [0, 1].
    void SetVolume(Entity e, float v);

    // Called each frame with the active camera/listener position + forward.
    void SetListenerTransform(const glm::vec3& pos,
                              const glm::vec3& forward,
                              const glm::vec3& up);

    FMOD::System* GetSystem() const { return m_system; }

private:
    AudioSystem()  = default;
    ~AudioSystem() = default;

    // Load sound from AssetManager bytes for an entity; returns nullptr on fail.
    FMOD::Sound* LoadSound(Entity e, Registry& registry);

    // Release sound + channel for one entity.
    void ReleaseEntity(Entity e);

    FMOD::System* m_system = nullptr;

    // Loaded sounds (one per entity; FMOD_CREATESTREAM for audio files).
    std::unordered_map<Entity, FMOD::Sound*>   m_sounds;
    // Playing channels (may be nullptr if stopped).
    std::unordered_map<Entity, FMOD::Channel*> m_channels;

    bool m_initialized = false;
};
