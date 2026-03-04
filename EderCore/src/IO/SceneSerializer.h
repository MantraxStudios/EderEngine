#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  SceneSerializer  — save / load the full ECS state to a plain-text .scene
//  Header-only, part of EderCore.
//
//  Format (one block per entity):
//    version=1
//    name=MyLevel
//
//    [entity]
//    id=1
//    tag.name=Player
//    transform.pos=0 1 0
//    transform.rot=0 0 0
//    transform.scale=1 1 1
//    mesh.guid=abc123   (hex)
//    mesh.path=assets/hero.fbx
//    ...
//    [/entity]
// ─────────────────────────────────────────────────────────────────────────────

#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/VolumetricFogComponent.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <glm/glm.hpp>

namespace Krayon {

// ─────────────────────────────────────────────────────────────────────────────
class SceneSerializer
{
public:

    // ─────────────────────────────────────────────────────────────────────────
    //  Save — serialises the complete registry to `path`.
    //  `sceneName` defaults to the file stem.
    //  Returns true on success.
    // ─────────────────────────────────────────────────────────────────────────
    static bool Save(const std::string& path,
                     Registry&          reg,
                     const std::string& sceneName = "")
    {
        namespace fs = std::filesystem;
        std::ofstream f(path, std::ios::trunc);
        if (!f.is_open()) return false;

        const std::string name = sceneName.empty()
            ? fs::path(path).stem().string()
            : sceneName;

        f << "version=1\n";
        f << "name=" << Escape(name) << "\n\n";

        for (Entity e : reg.GetEntities())
        {
            f << "[entity]\n";
            f << "id=" << e << "\n";

            // ── TagComponent ─────────────────────────────────────────────────
            if (reg.Has<TagComponent>(e))
            {
                f << "tag.name=" << Escape(reg.Get<TagComponent>(e).name) << "\n";
            }

            // ── TransformComponent ───────────────────────────────────────────
            if (reg.Has<TransformComponent>(e))
            {
                const auto& t = reg.Get<TransformComponent>(e);
                f << "transform.pos="   << Vec3Str(t.position) << "\n";
                f << "transform.rot="   << Vec3Str(t.rotation) << "\n";
                f << "transform.scale=" << Vec3Str(t.scale)    << "\n";
            }

            // ── HierarchyComponent ───────────────────────────────────────────
            if (reg.Has<HierarchyComponent>(e))
            {
                const auto& h = reg.Get<HierarchyComponent>(e);
                f << "hierarchy.parent=" << h.parent << "\n";
                f << "hierarchy.children=";
                for (size_t i = 0; i < h.children.size(); ++i)
                {
                    if (i > 0) f << ' ';
                    f << h.children[i];
                }
                f << "\n";
            }

            // ── MeshRendererComponent ─────────────────────────────────────────
            if (reg.Has<MeshRendererComponent>(e))
            {
                const auto& m = reg.Get<MeshRendererComponent>(e);
                char buf[32];
                snprintf(buf, sizeof(buf), "%llx", (unsigned long long)m.meshGuid);
                f << "mesh.guid="       << buf << "\n";
                f << "mesh.path="       << Escape(m.meshPath) << "\n";
                snprintf(buf, sizeof(buf), "%llx", (unsigned long long)m.materialGuid);
                f << "mesh.matGuid="    << buf << "\n";
                f << "mesh.matName="    << Escape(m.materialName) << "\n";
                f << "mesh.castShadow=" << (m.castShadow ? 1 : 0) << "\n";
                f << "mesh.visible="    << (m.visible    ? 1 : 0) << "\n";
            }

            // ── LightComponent ────────────────────────────────────────────────
            if (reg.Has<LightComponent>(e))
            {
                const auto& l = reg.Get<LightComponent>(e);
                f << "light.type="      << static_cast<int>(l.type) << "\n";
                f << "light.color="     << Vec3Str(l.color) << "\n";
                f << "light.intensity=" << l.intensity << "\n";
                f << "light.range="     << l.range << "\n";
                f << "light.innerCone=" << l.innerConeAngle << "\n";
                f << "light.outerCone=" << l.outerConeAngle << "\n";
                f << "light.castShadow=" << (l.castShadow ? 1 : 0) << "\n";

                // Volumetric
                f << "light.vol.enabled="    << (l.volumetricEnabled ? 1 : 0) << "\n";
                f << "light.vol.steps="      << l.volNumSteps    << "\n";
                f << "light.vol.density="    << l.volDensity     << "\n";
                f << "light.vol.absorption=" << l.volAbsorption  << "\n";
                f << "light.vol.g="          << l.volG           << "\n";
                f << "light.vol.intensity="  << l.volIntensity   << "\n";
                f << "light.vol.maxDist="    << l.volMaxDistance << "\n";
                f << "light.vol.jitter="     << l.volJitter      << "\n";
                f << "light.vol.tint="       << Vec3Str(l.volTint) << "\n";

                // Sun Shafts
                f << "light.shafts.enabled="   << (l.sunShaftsEnabled ? 1 : 0) << "\n";
                f << "light.shafts.density="   << l.shaftsDensity   << "\n";
                f << "light.shafts.bloom="     << l.shaftsBloomScale << "\n";
                f << "light.shafts.decay="     << l.shaftsDecay     << "\n";
                f << "light.shafts.weight="    << l.shaftsWeight    << "\n";
                f << "light.shafts.exposure="  << l.shaftsExposure  << "\n";
                f << "light.shafts.sunRadius=" << l.shaftsSunRadius << "\n";
                f << "light.shafts.tint="      << Vec3Str(l.shaftsTint) << "\n";
            }

            // ── AnimationComponent ────────────────────────────────────────────
            if (reg.Has<AnimationComponent>(e))
            {
                const auto& a = reg.Get<AnimationComponent>(e);
                f << "anim.index="         << a.animIndex     << "\n";
                f << "anim.speed="         << a.speed         << "\n";
                f << "anim.loop="          << (a.loop    ? 1 : 0) << "\n";
                f << "anim.playing="       << (a.playing ? 1 : 0) << "\n";
                f << "anim.blendDuration=" << a.blendDuration << "\n";
            }

            // ── VolumetricFogComponent ────────────────────────────────────────
            if (reg.Has<VolumetricFogComponent>(e))
            {
                const auto& fog = reg.Get<VolumetricFogComponent>(e);
                f << "fog.enabled="        << (fog.enabled ? 1 : 0) << "\n";
                f << "fog.color="          << Vec3Str(fog.fogColor)        << "\n";
                f << "fog.horizonColor="   << Vec3Str(fog.horizonColor)    << "\n";
                f << "fog.sunScatter="     << Vec3Str(fog.sunScatterColor) << "\n";
                f << "fog.density="        << fog.density        << "\n";
                f << "fog.heightFalloff="  << fog.heightFalloff  << "\n";
                f << "fog.heightOffset="   << fog.heightOffset   << "\n";
                f << "fog.fogStart="       << fog.fogStart       << "\n";
                f << "fog.fogEnd="         << fog.fogEnd         << "\n";
                f << "fog.scatterStr="     << fog.scatterStrength << "\n";
                f << "fog.maxFog="         << fog.maxFogAmount   << "\n";
            }

            f << "[/entity]\n\n";
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Load — populates `reg` from `path`.
    //  Does NOT clear the registry first; call reg.Clear() yourself if needed.
    //  Fills `outSceneName` with the serialised "name=" value (optional).
    //  Entity IDs are remapped to fresh IDs — file IDs are only used for
    //  resolving hierarchy parent/child cross-references.
    //  Returns false only on file-open error.
    // ─────────────────────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────
    //  Load — deserialises a .scene file from disk into `reg`.
    // ─────────────────────────────────────────────────────────────────────────
    static bool Load(const std::string& path,
                     Registry&          reg,
                     std::string*       outSceneName = nullptr)
    {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        return ParseStream(f, reg, outSceneName);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  LoadFromBytes — deserialises from raw bytes (e.g. read from a .pak).
    // ─────────────────────────────────────────────────────────────────────────
    static bool LoadFromBytes(const std::vector<uint8_t>& bytes,
                              Registry&                   reg,
                              std::string*                outSceneName = nullptr)
    {
        const std::string text(bytes.begin(), bytes.end());
        std::istringstream ss(text);
        return ParseStream(ss, reg, outSceneName);
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    //  ParseStream — shared istream-based scene parser used by Load + LoadFromBytes
    // ─────────────────────────────────────────────────────────────────────────
    static bool ParseStream(std::istream& f,
                            Registry&     reg,
                            std::string*  outSceneName)
    {
        struct Block { std::unordered_map<std::string, std::string> kv; };
        std::vector<Block> blocks;
        Block* cur = nullptr;
        std::string line;

        while (std::getline(f, line))
        {
            TrimRight(line);
            if (line.empty() || line[0] == '#') continue;
            if (line == "[entity]")
            {
                blocks.push_back({});
                cur = &blocks.back();
            }
            else if (line == "[/entity]")
            {
                cur = nullptr;
            }
            else if (cur)
            {
                const size_t eq = line.find('=');
                if (eq != std::string::npos)
                    cur->kv[line.substr(0, eq)] = line.substr(eq + 1);
            }
            else
            {
                // Top-level header line
                const size_t eq = line.find('=');
                if (eq != std::string::npos && outSceneName)
                {
                    if (line.substr(0, eq) == "name")
                        *outSceneName = Unescape(line.substr(eq + 1));
                }
            }
        }

        // ── Pass 2: create entities, build file-id → new-entity map ──────────
        std::unordered_map<Entity, Entity> idMap;
        std::vector<Entity> newEntities;
        newEntities.reserve(blocks.size());

        for (auto& b : blocks)
        {
            Entity newE = reg.Create();
            newEntities.push_back(newE);
            Entity fileId = 0;
            if (b.kv.count("id")) fileId = static_cast<Entity>(std::stoul(b.kv.at("id")));
            idMap[fileId] = newE;
        }

        // ── Pass 3: populate components ───────────────────────────────────────
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            auto& kv = blocks[i].kv;
            Entity e  = newEntities[i];

            // TagComponent
            if (kv.count("tag.name"))
                reg.Add<TagComponent>(e).name = Unescape(kv.at("tag.name"));

            // TransformComponent
            if (kv.count("transform.pos"))
            {
                auto& t   = reg.Add<TransformComponent>(e);
                t.position = ParseVec3(kv.at("transform.pos"));
                if (kv.count("transform.rot"))   t.rotation = ParseVec3(kv.at("transform.rot"));
                if (kv.count("transform.scale")) t.scale    = ParseVec3(kv.at("transform.scale"));
            }

            // HierarchyComponent
            if (kv.count("hierarchy.parent"))
            {
                auto& h   = reg.Add<HierarchyComponent>(e);
                Entity pid = static_cast<Entity>(std::stoul(kv.at("hierarchy.parent")));
                h.parent  = (pid == 0 || !idMap.count(pid)) ? NULL_ENTITY : idMap.at(pid);
                if (kv.count("hierarchy.children"))
                {
                    std::istringstream ss(kv.at("hierarchy.children"));
                    Entity cid;
                    while (ss >> cid)
                        if (idMap.count(cid)) h.children.push_back(idMap.at(cid));
                }
            }

            // MeshRendererComponent
            if (kv.count("mesh.guid"))
            {
                auto& m = reg.Add<MeshRendererComponent>(e);
                try { m.meshGuid     = std::stoull(kv.at("mesh.guid"), nullptr, 16); } catch (...) {}
                if (kv.count("mesh.path"))    m.meshPath     = Unescape(kv.at("mesh.path"));
                if (kv.count("mesh.matGuid")) { try { m.materialGuid = std::stoull(kv.at("mesh.matGuid"), nullptr, 16); } catch (...) {} }
                if (kv.count("mesh.matName")) m.materialName = Unescape(kv.at("mesh.matName"));
                if (kv.count("mesh.castShadow")) m.castShadow = kv.at("mesh.castShadow") == "1";
                if (kv.count("mesh.visible"))    m.visible    = kv.at("mesh.visible")    == "1";
            }

            // LightComponent
            if (kv.count("light.type"))
            {
                auto& l  = reg.Add<LightComponent>(e);
                l.type   = static_cast<LightType>(std::stoi(kv.at("light.type")));
                if (kv.count("light.color"))      l.color          = ParseVec3(kv.at("light.color"));
                if (kv.count("light.intensity"))  l.intensity       = std::stof(kv.at("light.intensity"));
                if (kv.count("light.range"))      l.range           = std::stof(kv.at("light.range"));
                if (kv.count("light.innerCone"))  l.innerConeAngle  = std::stof(kv.at("light.innerCone"));
                if (kv.count("light.outerCone"))  l.outerConeAngle  = std::stof(kv.at("light.outerCone"));
                if (kv.count("light.castShadow")) l.castShadow      = kv.at("light.castShadow") == "1";
                // Volumetric
                if (kv.count("light.vol.enabled"))    l.volumetricEnabled = kv.at("light.vol.enabled") == "1";
                if (kv.count("light.vol.steps"))      l.volNumSteps       = std::stoi(kv.at("light.vol.steps"));
                if (kv.count("light.vol.density"))    l.volDensity        = std::stof(kv.at("light.vol.density"));
                if (kv.count("light.vol.absorption")) l.volAbsorption     = std::stof(kv.at("light.vol.absorption"));
                if (kv.count("light.vol.g"))          l.volG              = std::stof(kv.at("light.vol.g"));
                if (kv.count("light.vol.intensity"))  l.volIntensity      = std::stof(kv.at("light.vol.intensity"));
                if (kv.count("light.vol.maxDist"))    l.volMaxDistance    = std::stof(kv.at("light.vol.maxDist"));
                if (kv.count("light.vol.jitter"))     l.volJitter         = std::stof(kv.at("light.vol.jitter"));
                if (kv.count("light.vol.tint"))       l.volTint           = ParseVec3(kv.at("light.vol.tint"));
                // Sun Shafts
                if (kv.count("light.shafts.enabled"))   l.sunShaftsEnabled = kv.at("light.shafts.enabled") == "1";
                if (kv.count("light.shafts.density"))   l.shaftsDensity    = std::stof(kv.at("light.shafts.density"));
                if (kv.count("light.shafts.bloom"))     l.shaftsBloomScale = std::stof(kv.at("light.shafts.bloom"));
                if (kv.count("light.shafts.decay"))     l.shaftsDecay      = std::stof(kv.at("light.shafts.decay"));
                if (kv.count("light.shafts.weight"))    l.shaftsWeight     = std::stof(kv.at("light.shafts.weight"));
                if (kv.count("light.shafts.exposure"))  l.shaftsExposure   = std::stof(kv.at("light.shafts.exposure"));
                if (kv.count("light.shafts.sunRadius")) l.shaftsSunRadius  = std::stof(kv.at("light.shafts.sunRadius"));
                if (kv.count("light.shafts.tint"))      l.shaftsTint       = ParseVec3(kv.at("light.shafts.tint"));
            }

            // AnimationComponent
            if (kv.count("anim.index"))
            {
                auto& a         = reg.Add<AnimationComponent>(e);
                a.animIndex     = std::stoi(kv.at("anim.index"));
                if (kv.count("anim.speed"))         a.speed         = std::stof(kv.at("anim.speed"));
                if (kv.count("anim.loop"))          a.loop          = kv.at("anim.loop")    == "1";
                if (kv.count("anim.playing"))       a.playing       = kv.at("anim.playing") == "1";
                if (kv.count("anim.blendDuration")) a.blendDuration = std::stof(kv.at("anim.blendDuration"));
            }

            // VolumetricFogComponent
            if (kv.count("fog.enabled"))
            {
                auto& fog         = reg.Add<VolumetricFogComponent>(e);
                fog.enabled       = kv.at("fog.enabled") == "1";
                if (kv.count("fog.color"))         fog.fogColor         = ParseVec3(kv.at("fog.color"));
                if (kv.count("fog.horizonColor"))  fog.horizonColor     = ParseVec3(kv.at("fog.horizonColor"));
                if (kv.count("fog.sunScatter"))    fog.sunScatterColor  = ParseVec3(kv.at("fog.sunScatter"));
                if (kv.count("fog.density"))       fog.density          = std::stof(kv.at("fog.density"));
                if (kv.count("fog.heightFalloff")) fog.heightFalloff    = std::stof(kv.at("fog.heightFalloff"));
                if (kv.count("fog.heightOffset"))  fog.heightOffset     = std::stof(kv.at("fog.heightOffset"));
                if (kv.count("fog.fogStart"))      fog.fogStart         = std::stof(kv.at("fog.fogStart"));
                if (kv.count("fog.fogEnd"))        fog.fogEnd           = std::stof(kv.at("fog.fogEnd"));
                if (kv.count("fog.scatterStr"))    fog.scatterStrength  = std::stof(kv.at("fog.scatterStr"));
                if (kv.count("fog.maxFog"))        fog.maxFogAmount     = std::stof(kv.at("fog.maxFog"));
            }
        }

        return true;
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    //  Utility helpers
    // ─────────────────────────────────────────────────────────────────────────

    static std::string Vec3Str(const glm::vec3& v)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "%.6f %.6f %.6f", v.x, v.y, v.z);
        return buf;
    }

    static glm::vec3 ParseVec3(const std::string& s)
    {
        glm::vec3 v{};
        std::istringstream ss(s);
        ss >> v.x >> v.y >> v.z;
        return v;
    }

    static void TrimRight(std::string& s)
    {
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
    }

    // Escape newlines and backslashes so values stay single-line
    static std::string Escape(const std::string& s)
    {
        std::string r;
        r.reserve(s.size());
        for (char c : s)
        {
            if      (c == '\\') r += "\\\\";
            else if (c == '\n') r += "\\n";
            else if (c == '\r') r += "\\r";
            else                r += c;
        }
        return r;
    }

    static std::string Unescape(const std::string& s)
    {
        std::string r;
        r.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                ++i;
                switch (s[i])
                {
                    case '\\': r += '\\'; break;
                    case 'n':  r += '\n'; break;
                    case 'r':  r += '\r'; break;
                    default:   r += s[i]; break;
                }
            }
            else r += s[i];
        }
        return r;
    }
};

} // namespace Krayon
