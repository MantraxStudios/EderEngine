#include "AudioSystem.h"

#include <inc/fmod_errors.h>
#include <IO/AssetManager.h>
#include "ECS/Components/AudioSourceComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Systems/TransformSystem.h"

#include <iostream>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: log FMOD errors
// ─────────────────────────────────────────────────────────────────────────────
static void FCheck(FMOD_RESULT r, const char* ctx)
{
    if (r != FMOD_OK)
        std::cerr << "[AudioSystem] " << ctx << ": " << FMOD_ErrorString(r) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
AudioSystem& AudioSystem::Get()
{
    static AudioSystem s_instance;
    return s_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioSystem::Init()
{
    if (m_initialized) return;

    FCheck(FMOD::System_Create(&m_system), "System_Create");

    // Up to 512 simultaneous channels; 3D sound enabled.
    // FMOD uses left-handed coords (+Z forward). Our engine is right-handed (-Z forward).
    // We keep positions in world space unchanged and negate the forward vector so that
    // FMOD LH correctly computes right = cross(up, -cameraForward).
    FCheck(m_system->init(512, FMOD_INIT_NORMAL, nullptr),
           "System::init");

    // Default listener at origin. Forward = -GetForward() at az=0,el=0 = (0,0,1).
    FMOD_VECTOR pos  = {0,0,0};
    FMOD_VECTOR vel  = {0,0,0};
    FMOD_VECTOR fwd  = {0,0,1};
    FMOD_VECTOR up   = {0,1,0};
    m_system->set3DListenerAttributes(0, &pos, &vel, &fwd, &up);

    m_initialized = true;
    std::cout << "[AudioSystem] Initialized (FMOD)\n";
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioSystem::Shutdown()
{
    if (!m_initialized) return;

    // Release all sounds and channels
    for (auto& [e, snd] : m_sounds)
        if (snd) snd->release();
    m_sounds.clear();
    m_channels.clear();

    if (m_system)
    {
        m_system->close();
        m_system->release();
        m_system = nullptr;
    }
    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioSystem::SetListenerTransform(const glm::vec3& pos,
                                       const glm::vec3& forward,
                                       const glm::vec3& up)
{
    if (!m_system) return;
    FMOD_VECTOR fpos = { pos.x,     pos.y,     pos.z     };
    FMOD_VECTOR fvel = { 0.0f, 0.0f, 0.0f };
    FMOD_VECTOR ffwd = { forward.x, forward.y, forward.z };
    FMOD_VECTOR fup  = { up.x,      up.y,      up.z      };
    m_system->set3DListenerAttributes(0, &fpos, &fvel, &ffwd, &fup);
}
// ─────────────────────────────────────────────────────────────────────────────
//  LoadSound — build an in-memory sound from AssetManager bytes
// ─────────────────────────────────────────────────────────────────────────────
FMOD::Sound* AudioSystem::LoadSound(Entity e, Registry& registry)
{
    if (!registry.Has<AudioSourceComponent>(e)) return nullptr;
    auto& as = registry.Get<AudioSourceComponent>(e);

    if (as.audioGuid == 0 && as.audioPath.empty()) return nullptr;

    // Load raw bytes from AssetManager (works loose-file and pak mode)
    std::vector<uint8_t> bytes;
    if (as.audioGuid != 0)
        bytes = Krayon::AssetManager::Get().GetBytesByGuid(as.audioGuid);
    if (bytes.empty() && !as.audioPath.empty())
        bytes = Krayon::AssetManager::Get().GetBytes(as.audioPath);
    if (bytes.empty())
    {
        std::cerr << "[AudioSystem] No bytes for audio entity=" << e << "\n";
        return nullptr;
    }

    // Copy bytes into a persistent buffer FMOD can hold a pointer to.
    // We store it inside a custom FMOD_CREATESOUNDEXINFO userdata block;
    // the simplest approach is to keep the buffer alive via a static map.
    // Use FMOD_OPENMEMORY_POINT (zero-copy) with a heap buffer we own.
    static std::unordered_map<Entity, std::vector<uint8_t>> s_buffers;
    s_buffers[e] = std::move(bytes);
    auto& buf = s_buffers[e];

    FMOD_MODE mode = FMOD_OPENMEMORY | FMOD_LOOP_OFF;
    if (as.spatial)
        mode |= FMOD_3D | FMOD_3D_LINEARROLLOFF;
    else
        mode |= FMOD_2D;
    if (as.loop)
        mode = (mode & ~FMOD_LOOP_OFF) | FMOD_LOOP_NORMAL;

    FMOD_CREATESOUNDEXINFO exinfo{};
    exinfo.cbsize = sizeof(exinfo);
    exinfo.length = static_cast<unsigned int>(buf.size());

    FMOD::Sound* snd = nullptr;
    FMOD_RESULT  r   = m_system->createSound(
        reinterpret_cast<const char*>(buf.data()), mode, &exinfo, &snd);
    FCheck(r, "createSound");
    if (r != FMOD_OK) return nullptr;

    if (as.spatial)
        snd->set3DMinMaxDistance(as.minDistance, as.maxDistance);

    return snd;
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioSystem::ReleaseEntity(Entity e)
{
    auto ch = m_channels.find(e);
    if (ch != m_channels.end())
    {
        if (ch->second) ch->second->stop();
        m_channels.erase(ch);
    }
    auto snd = m_sounds.find(e);
    if (snd != m_sounds.end())
    {
        if (snd->second) snd->second->release();
        m_sounds.erase(snd);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Play — load (if needed) and start playback
// ─────────────────────────────────────────────────────────────────────────────
void AudioSystem::Play(Entity e, Registry& registry)
{
    if (!m_initialized || !m_system) return;
    if (!registry.Has<AudioSourceComponent>(e)) return;
    auto& as = registry.Get<AudioSourceComponent>(e);

    // Stop existing channel
    auto ch = m_channels.find(e);
    if (ch != m_channels.end() && ch->second)
        ch->second->stop();

    // Reload sound if not cached or guid changed
    if (!m_sounds.count(e) || !m_sounds[e])
    {
        FMOD::Sound* snd = LoadSound(e, registry);
        if (!snd) return;
        m_sounds[e] = snd;
    }

    FMOD::Channel* channel = nullptr;
    FMOD_RESULT r = m_system->playSound(m_sounds[e], nullptr, true, &channel);
    FCheck(r, "playSound");
    if (r != FMOD_OK || !channel) return;

    channel->setVolume(as.muted ? 0.0f : as.volume);

    // Set initial 3D position (negate Z: right-handed → FMOD left-handed)
    if (as.spatial && registry.Has<TransformComponent>(e))
    {
        glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);
        FMOD_VECTOR pos = { world[3][0], world[3][1], world[3][2] };
        FMOD_VECTOR vel = { 0,0,0 };
        channel->set3DAttributes(&pos, &vel);
    }

    channel->setPaused(false);
    m_channels[e] = channel;
}

// ─────────────────────────────────────────────────────────────────────────────
void AudioSystem::Stop(Entity e)
{
    auto it = m_channels.find(e);
    if (it != m_channels.end() && it->second)
    {
        it->second->stop();
        it->second = nullptr;
    }
}

void AudioSystem::Pause(Entity e)
{
    auto it = m_channels.find(e);
    if (it != m_channels.end() && it->second)
        it->second->setPaused(true);
}

void AudioSystem::Resume(Entity e)
{
    auto it = m_channels.find(e);
    if (it != m_channels.end() && it->second)
        it->second->setPaused(false);
}

bool AudioSystem::IsPlaying(Entity e) const
{
    auto it = m_channels.find(e);
    if (it == m_channels.end() || !it->second) return false;
    bool playing = false;
    it->second->isPlaying(&playing);
    return playing;
}

void AudioSystem::SetVolume(Entity e, float v)
{
    auto it = m_channels.find(e);
    if (it != m_channels.end() && it->second)
        it->second->setVolume(v);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Update — called every frame
// ─────────────────────────────────────────────────────────────────────────────
void AudioSystem::Update(Registry& registry, float /*dt*/)
{
    if (!m_initialized || !m_system) return;

    // ── Sync listener to active CameraComponent entity (FPS camera) ──────────
    // Reads the world matrix directly — no angle reconstruction round-trip.
    // Falls back to whatever SetListenerTransform() last set (e.g. editor fly-cam).
    bool listenerSet = false;
    registry.Each<CameraComponent>([&](Entity e, CameraComponent& cam)
    {
        if (listenerSet || !cam.isActive) return;
        if (!registry.Has<TransformComponent>(e)) return;

        glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);
        glm::vec3 pos   = glm::vec3(world[3]);
        float     sz    = glm::length(glm::vec3(world[2]));
        glm::vec3 fwd   = (sz > 0.f) ? (-glm::vec3(world[2]) / sz) : glm::vec3(0.f, 0.f, -1.f);
        glm::vec3 up    = glm::normalize(glm::vec3(world[1]));
        SetListenerTransform(pos, fwd, up);
        listenerSet = true;
    });

    registry.Each<AudioSourceComponent>([&](Entity e, AudioSourceComponent& as)
    {
        // playOnAwake — fire once when started flag transitions false→true
        if (!as.started)
        {
            as.started = true;
            if (as.playOnAwake && as.audioGuid != 0)
                Play(e, registry);
            return;
        }

        // Keep 3D position in sync each frame
        if (as.spatial && registry.Has<TransformComponent>(e))
        {
            auto it = m_channels.find(e);
            if (it != m_channels.end() && it->second)
            {
                bool playing = false;
                it->second->isPlaying(&playing);
                if (playing)
                {
                    glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);
                    FMOD_VECTOR pos = { world[3][0], world[3][1], world[3][2] };
                    FMOD_VECTOR vel = { 0,0,0 };
                    it->second->set3DAttributes(&pos, &vel);
                }
            }
        }

        // Sync mute / volume live changes
        auto it = m_channels.find(e);
        if (it != m_channels.end() && it->second)
            it->second->setVolume(as.muted ? 0.0f : as.volume);
    });

    m_system->update();
}
