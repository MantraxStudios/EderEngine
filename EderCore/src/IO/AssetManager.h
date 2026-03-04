#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  AssetManager.h  — Central asset registry + byte loader
//  Header-only C++17 (part of EderCore INTERFACE library)
//
//  Sidecar model (like Unity .meta):
//    Every asset  foo.ext  has a companion  foo.ext.data  in the SAME folder.
//    Moving/sharing a folder keeps metadata with the asset automatically.
//
//  Sidecar format  (plain text, 4 key=value lines):
//      guid=A3B2C1D4E5F60718
//      type=Texture
//      name=stone
//      path=textures/stone.png
//
//  Two runtime modes:
//    Compiled = false  → bytes read from loose files on disk  (editor / dev)
//    Compiled = true   → bytes served from a PakFile          (shipping)
//
//  Typical usage:
//      AssetManager::Get().Init("C:/Game/assets", false);
//      auto bytes = AssetManager::Get().GetBytes("textures/stone.png");
// ─────────────────────────────────────────────────────────────────────────────

#include "KRCommon.h"
#include "PakFile.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdint>

namespace Krayon
{
    // ─────────────────────────────────────────────────────────────
    //  AssetType
    // ─────────────────────────────────────────────────────────────

    enum class AssetType : uint8_t
    {
        Unknown = 0,
        Mesh,       // .fbx  .obj  .gltf  .glb  .dae
        Texture,    // .png  .jpg  .jpeg  .bmp  .tga  .hdr
        Audio,      // .mp3  .wav  .ogg
        Shader,     // .vert .frag .comp  .spv  .glsl
        Data,       // .bin  .json .xml   .txt
        PAK,        // .pak
        Material,   // .mat  (editor surface asset)
        Scene,      // .scene (editor serialised scene)
        Script      // .lua  (Lua script)
    };

    inline std::string AssetTypeToString(AssetType t)
    {
        switch (t)
        {
            case AssetType::Mesh:     return "Mesh";
            case AssetType::Texture:  return "Texture";
            case AssetType::Audio:    return "Audio";
            case AssetType::Shader:   return "Shader";
            case AssetType::Data:     return "Data";
            case AssetType::PAK:      return "PAK";
            case AssetType::Material: return "Material";
            case AssetType::Scene:    return "Scene";
            case AssetType::Script:   return "Script";
            default:                  return "Unknown";
        }
    }

    inline AssetType AssetTypeFromString(const std::string& s)
    {
        if (s == "Mesh")     return AssetType::Mesh;
        if (s == "Texture")  return AssetType::Texture;
        if (s == "Audio")    return AssetType::Audio;
        if (s == "Shader")   return AssetType::Shader;
        if (s == "Data")     return AssetType::Data;
        if (s == "PAK")      return AssetType::PAK;
        if (s == "Material") return AssetType::Material;
        if (s == "Scene")    return AssetType::Scene;
        if (s == "Script")   return AssetType::Script;
        return AssetType::Unknown;
    }

    inline AssetType DetectTypeByExtension(const std::string& ext)
    {
        std::string e = ext;
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

        if (e == ".fbx" || e == ".obj" || e == ".gltf" || e == ".glb" || e == ".dae")
            return AssetType::Mesh;
        if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".hdr")
            return AssetType::Texture;
        if (e == ".mp3" || e == ".wav" || e == ".ogg")
            return AssetType::Audio;
        if (e == ".vert" || e == ".frag" || e == ".comp" || e == ".spv" || e == ".glsl")
            return AssetType::Shader;
        if (e == ".bin" || e == ".json" || e == ".xml" || e == ".txt")
            return AssetType::Data;
        if (e == ".pak")
            return AssetType::PAK;
        if (e == ".mat")
            return AssetType::Material;
        if (e == ".scene")
            return AssetType::Scene;
        if (e == ".lua")
            return AssetType::Script;
        return AssetType::Unknown;
    }

    // ─────────────────────────────────────────────────────────────
    //  MaterialAsset  — lightweight surface description stored in .mat files
    //  All shader refs are GUIDs so they survive renames/moves.
    // ─────────────────────────────────────────────────────────────
    struct MaterialAsset
    {
        uint64_t    guid             = 0;
        std::string name;
        uint64_t    vertShaderGuid   = 0;
        uint64_t    fragShaderGuid   = 0;
        float       albedo[4]        = { 1.0f, 1.0f, 1.0f, 1.0f };  // RGBA
        float       roughness        = 0.5f;
        float       metallic         = 0.0f;
        float       emissive[3]      = { 0.0f, 0.0f, 0.0f };
        // Texture slots — GUID 0 means "no texture"
        uint64_t    albedoTexGuid    = 0;   // slot 0  (base color)
        uint64_t    normalTexGuid    = 0;   // slot 1  (normal map)
        uint64_t    roughnessTexGuid = 0;   // slot 2  (roughness/metallic)
        uint64_t    emissiveTexGuid  = 0;   // slot 3  (emissive)
    };

    // ─────────────────────────────────────────────────────────────
    //  AssetMeta  — one registry entry
    // ─────────────────────────────────────────────────────────────

    struct AssetMeta
    {
        uint64_t    guid = 0;
        AssetType   type = AssetType::Unknown;
        std::string name;   // file stem (no extension)
        std::string path;   // relative to workDir, forward-slashes, lower-case
    };

    // ─────────────────────────────────────────────────────────────
    //  AssetManager
    // ─────────────────────────────────────────────────────────────

    class AssetManager
    {
    public:
        // ── Singleton ────────────────────────────────────────────
        static AssetManager& Get()
        {
            static AssetManager s_instance;
            return s_instance;
        }

        // ── Init ─────────────────────────────────────────────────
        /// workDir  : root folder with loose asset files (editor/dev mode).
        /// compiled : false → read from disk; true → read from PAK only.
        /// pakPath  : path to .pak (only used when compiled = true).
        void Init(const std::string& workDir,
                  bool               compiled,
                  const std::string& pakPath = "")
        {
            m_compiled = compiled;
            m_workDir  = NormDir(workDir);

            // Collect all existing sidecars first
            LoadAllSidecars();

            if (m_compiled)
            {
                if (!pakPath.empty())
                {
                    m_pak = std::make_unique<PakFile>(pakPath);
                    LoadManifestFromPak();
                }
            }
            else
            {
                // Create sidecars for any new files found on disk
                Scan();
            }
        }

        // ── Byte loading ──────────────────────────────────────────

        /// Returns decrypted/raw bytes for the asset at `assetPath`.
        /// `assetPath` is relative to workDir (e.g. "textures/stone.png").
        /// Returns empty vector on failure.
        std::vector<uint8_t> GetBytes(const std::string& assetPath)
        {
            const std::string norm = NormalizePath(assetPath);

            if (m_compiled)
            {
                if (!m_pak)
                {
                    std::cerr << "[AssetManager] Compiled mode but no PAK loaded.\n";
                    return {};
                }
                return m_pak->Load(norm);
            }

            // Loose-file mode: read straight from disk
            const std::string full = m_workDir + "/" + norm;
            std::ifstream f(full, std::ios::binary);
            if (!f.is_open())
            {
                std::cerr << "[AssetManager] File not found: " << full << "\n";
                return {};
            }
            f.seekg(0, std::ios::end);
            const std::streamsize sz = f.tellg();
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(static_cast<size_t>(sz));
            f.read(reinterpret_cast<char*>(data.data()), sz);
            return data;
        }

        /// Load by GUID — looks up path in registry first.
        std::vector<uint8_t> GetBytesByGuid(uint64_t guid)
        {
            auto it = m_byGuid.find(guid);
            if (it == m_byGuid.end())
            {
                std::cerr << "[AssetManager] GUID not found: " << std::hex << guid << std::dec << "\n";
                return {};
            }
            return GetBytes(it->second.path);
        }

        // ── Registry queries ──────────────────────────────────────

        bool Contains(const std::string& assetPath) const
        {
            return m_byPath.count(NormalizePath(assetPath)) > 0;
        }

        bool ContainsGuid(uint64_t guid) const
        {
            return m_byGuid.count(guid) > 0;
        }

        const AssetMeta* Find(const std::string& assetPath) const
        {
            auto it = m_byPath.find(NormalizePath(assetPath));
            if (it == m_byPath.end()) return nullptr;
            auto it2 = m_byGuid.find(it->second);
            return (it2 != m_byGuid.end()) ? &it2->second : nullptr;
        }

        const AssetMeta* FindByGuid(uint64_t guid) const
        {
            auto it = m_byGuid.find(guid);
            return (it != m_byGuid.end()) ? &it->second : nullptr;
        }

        uint64_t GetGuid(const std::string& assetPath) const
        {
            auto it = m_byPath.find(NormalizePath(assetPath));
            return (it != m_byPath.end()) ? it->second : 0;
        }

        const std::unordered_map<uint64_t, AssetMeta>& GetAll() const { return m_byGuid; }

        bool IsCompiled() const { return m_compiled; }
        const std::string& GetWorkDir() const { return m_workDir; }

        // ── Scanning ─────────────────────────────────────────────

        /// Walk workDir recursively.
        /// For every recognised asset WITHOUT a sidecar, create one.
        /// Returns the number of newly created sidecars.
        int Scan()
        {
            if (m_workDir.empty()) return 0;
            namespace fs = std::filesystem;
            std::error_code ec;
            int added = 0;

            for (const auto& entry :
                 fs::recursive_directory_iterator(m_workDir, ec))
            {
                if (!entry.is_regular_file(ec)) continue;

                const std::string ext = entry.path().extension().string();
                if (ext == ".data") continue;   // skip sidecars themselves

                const AssetType t = DetectTypeByExtension(ext);
                if (t == AssetType::Unknown) continue;

                fs::path relPath = fs::relative(entry.path(), m_workDir, ec);
                if (ec) continue;
                const std::string rel = NormalizePath(relPath.string());

                if (m_byPath.count(rel)) continue;   // sidecar already loaded

                const uint64_t  guid = GenerateGuid(rel);
                const AssetMeta meta { guid, t,
                    entry.path().stem().string(), rel };

                RegisterMeta(meta);
                WriteSidecar(entry.path().string(), meta);

                std::cout << "[AssetManager] New sidecar created: ["
                          << std::hex << guid << std::dec << "] "
                          << AssetTypeToString(t) << "  " << rel << "\n";
                ++added;
            }
            return added;
        }

        /// Manually register an asset and write its sidecar (editor drag-drop).
        /// Returns the assigned GUID.
        uint64_t Register(const std::string& assetPath)
        {
            const std::string norm = NormalizePath(assetPath);
            if (m_byPath.count(norm)) return m_byPath[norm];

            namespace fs = std::filesystem;
            const std::string ext  = fs::path(norm).extension().string();
            const AssetType   t    = DetectTypeByExtension(ext);
            const uint64_t    guid = GenerateGuid(norm);
            const std::string name = fs::path(norm).stem().string();
            const AssetMeta   meta { guid, t, name, norm };

            RegisterMeta(meta);
            WriteSidecar(m_workDir + "/" + norm, meta);
            return guid;
        }

        // ── GUID-first GetBytes overload ─────────────────────────
        /// Load bytes by GUID directly — preferred API so moves/renames never break.
        std::vector<uint8_t> GetBytes(uint64_t guid)
        {
            return GetBytesByGuid(guid);
        }

        // ── File operations (editor) ──────────────────────────────

        /// Rename an asset's file stem (keep dir & extension).
        /// Updates the physical file + sidecar + in-memory registry.
        /// Returns false on error (GUID not found, IO fail, name conflict).
        bool RenameAsset(uint64_t guid, const std::string& newStem)
        {
            auto it = m_byGuid.find(guid);
            if (it == m_byGuid.end()) return false;

            AssetMeta meta = it->second;
            namespace fs = std::filesystem;

            fs::path oldRel  = meta.path;                                // "textures/stone.png"
            fs::path oldAbs  = fs::path(m_workDir) / oldRel;
            fs::path newAbs  = oldAbs.parent_path() / (newStem + oldAbs.extension().string());
            fs::path newRel  = fs::relative(newAbs, m_workDir);

            std::error_code ec;

            // Rename physical asset file
            if (!fs::exists(oldAbs, ec)) return false;
            fs::rename(oldAbs, newAbs, ec);
            if (ec) { std::cerr << "[AssetManager] RenameAsset: " << ec.message() << "\n"; return false; }

            // Rename sidecar
            fs::path oldSidecar = fs::path(oldAbs.string() + ".data");
            fs::path newSidecar = fs::path(newAbs.string() + ".data");
            if (fs::exists(oldSidecar, ec))
                fs::rename(oldSidecar, newSidecar, ec);

            // Update meta
            m_byPath.erase(meta.path);
            meta.name = newStem;
            meta.path = NormalizePath(newRel.string());
            m_byGuid[guid] = meta;
            m_byPath[meta.path] = guid;

            // Rewrite sidecar content (path field changed)
            WriteSidecar(newAbs.string(), meta);
            return true;
        }

        /// Move an asset to a different sub-directory (relative to workDir).
        /// `newRelDir` is the target directory path (e.g. "textures/environment").
        /// Creates the directory if it doesn't exist.
        bool MoveAsset(uint64_t guid, const std::string& newRelDir)
        {
            auto it = m_byGuid.find(guid);
            if (it == m_byGuid.end()) return false;

            AssetMeta meta = it->second;
            namespace fs = std::filesystem;

            fs::path oldRel   = meta.path;
            fs::path oldAbs   = fs::path(m_workDir) / oldRel;
            fs::path destDir  = fs::path(m_workDir) / NormalizePath(newRelDir);
            fs::path newAbs   = destDir / oldAbs.filename();

            std::error_code ec;
            fs::create_directories(destDir, ec);

            if (!fs::exists(oldAbs, ec)) return false;
            fs::rename(oldAbs, newAbs, ec);
            if (ec) { std::cerr << "[AssetManager] MoveAsset: " << ec.message() << "\n"; return false; }

            // Move sidecar
            fs::path oldSidecar = fs::path(oldAbs.string() + ".data");
            fs::path newSidecar = fs::path(newAbs.string() + ".data");
            if (fs::exists(oldSidecar, ec))
                fs::rename(oldSidecar, newSidecar, ec);

            // Update registry
            m_byPath.erase(meta.path);
            meta.path = NormalizePath(fs::relative(newAbs, m_workDir).string());
            m_byGuid[guid] = meta;
            m_byPath[meta.path] = guid;

            WriteSidecar(newAbs.string(), meta);
            return true;
        }

        /// Delete an asset file, its sidecar, and remove from registry.
        bool DeleteAsset(uint64_t guid)
        {
            auto it = m_byGuid.find(guid);
            if (it == m_byGuid.end()) return false;

            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path absPath = fs::path(m_workDir) / it->second.path;

            fs::remove(absPath, ec);
            fs::remove(fs::path(absPath.string() + ".data"), ec);

            m_byPath.erase(it->second.path);
            m_byGuid.erase(it);
            return true;
        }

        /// Create a new folder under workDir (recursive).
        bool CreateFolder(const std::string& relPath)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(fs::path(m_workDir) / NormalizePath(relPath), ec);
            return !ec;
        }

        /// Delete an empty (or force-remove) folder.
        bool DeleteFolder(const std::string& relPath, bool force = false)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path abs = fs::path(m_workDir) / NormalizePath(relPath);
            if (force)
                fs::remove_all(abs, ec);
            else
                fs::remove(abs, ec);
            return !ec;
        }

        /// Force a re-scan (picks up new files added outside the editor).
        int Refresh() { return Scan(); }

        // ── Scene asset helpers (editor) ──────────────────────────

        /// Register an existing scene file at `absPath` (absolute) into
        /// the AssetManager registry.  Creates the sidecar if missing.
        /// Returns the GUID (creates a new one if not yet registered).
        uint64_t RegisterSceneFile(const std::string& absPath,
                                   const std::string& name)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path abs(absPath);
            fs::path rel = fs::relative(abs, m_workDir, ec);
            if (ec) return 0;
            const std::string norm = NormalizePath(rel.string());

            if (m_byPath.count(norm)) return m_byPath.at(norm);

            const uint64_t  guid = GenerateGuid(norm);
            const AssetMeta meta { guid, AssetType::Scene, name, norm };
            RegisterMeta(meta);
            WriteSidecar(absPath, meta);
            return guid;
        }

        // ── Material asset CRUD (editor) ──────────────────────────

        /// Create a new empty .mat file in `relDir` (relative to workDir).
        /// The file is registered and a sidecar is written.
        /// Returns the new asset's GUID (0 on failure).
        uint64_t CreateMaterialAsset(const std::string& relDir,
                                     const std::string& name = "NewMaterial")
        {
            if (m_workDir.empty()) return 0;
            namespace fs = std::filesystem;

            std::string safeDir = NormalizePath(relDir);
            fs::path    absDir  = fs::path(m_workDir) / safeDir;
            std::error_code ec;
            fs::create_directories(absDir, ec);

            // Ensure unique filename
            std::string stem = name;
            fs::path    absFile;
            int         suffix  = 0;
            do {
                absFile = absDir / (stem + ".mat");
                if (!fs::exists(absFile)) break;
                stem = name + "_" + std::to_string(++suffix);
            } while (true);

            // Write default .mat content
            std::ofstream f(absFile.string(), std::ios::trunc);
            if (!f.is_open()) return 0;
            f << "name="           << stem              << "\n";
            f << "vertShaderGuid=" << "0"               << "\n";
            f << "fragShaderGuid=" << "0"               << "\n";
            f << "albedo="         << "1.0,1.0,1.0,1.0" << "\n";
            f << "roughness="      << "0.5"             << "\n";
            f << "metallic="       << "0.0"             << "\n";
            f << "emissive="       << "0.0,0.0,0.0"     << "\n";
            f << "albedoTexGuid="  << "0"               << "\n";
            f << "normalTexGuid="  << "0"               << "\n";
            f << "roughnessTexGuid=" << "0"             << "\n";
            f << "emissiveTexGuid=" << "0"              << "\n";
            f.close();

            return Register(
                NormalizePath(
                    fs::relative(absFile, m_workDir, ec).string()));
        }

        /// Read a .mat file into a MaterialAsset struct.
        /// Returns false if the GUID is not found or the file is malformed.
        bool ReadMaterialAsset(uint64_t guid, MaterialAsset& out) const
        {
            auto it = m_byGuid.find(guid);
            if (it == m_byGuid.end() || it->second.type != AssetType::Material)
                return false;

            const std::string absPath = m_workDir + "/" + it->second.path;
            std::ifstream f(absPath);
            if (!f.is_open()) return false;

            out = MaterialAsset{};
            out.guid = guid;
            out.name = it->second.name;

            std::string line;
            while (std::getline(f, line))
            {
                if (line.empty() || line[0] == '#') continue;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = Trim(line.substr(0, eq));
                const std::string v = Trim(line.substr(eq + 1));

                if (k == "name")           { out.name = v; }
                else if (k == "vertShaderGuid") { try { out.vertShaderGuid = std::stoull(v, nullptr, 16); } catch (...){} }
                else if (k == "fragShaderGuid") { try { out.fragShaderGuid = std::stoull(v, nullptr, 16); } catch (...){} }
                else if (k == "albedoTexGuid")    { try { out.albedoTexGuid    = std::stoull(v, nullptr, 16); } catch (...){} }
                else if (k == "normalTexGuid")     { try { out.normalTexGuid     = std::stoull(v, nullptr, 16); } catch (...){} }
                else if (k == "roughnessTexGuid")  { try { out.roughnessTexGuid  = std::stoull(v, nullptr, 16); } catch (...){} }
                else if (k == "emissiveTexGuid")   { try { out.emissiveTexGuid   = std::stoull(v, nullptr, 16); } catch (...){} }
                else if (k == "roughness") { try { out.roughness = std::stof(v); } catch (...){} }
                else if (k == "metallic")  { try { out.metallic  = std::stof(v); } catch (...){} }
                else if (k == "albedo")
                {
                    float r=1,g=1,b=1,a=1;
                    sscanf(v.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a);
                    out.albedo[0]=r; out.albedo[1]=g; out.albedo[2]=b; out.albedo[3]=a;
                }
                else if (k == "emissive")
                {
                    float r=0,g=0,b=0;
                    sscanf(v.c_str(), "%f,%f,%f", &r, &g, &b);
                    out.emissive[0]=r; out.emissive[1]=g; out.emissive[2]=b;
                }
            }
            return true;
        }

        /// Save a MaterialAsset back to its .mat file on disk.
        bool SaveMaterialAsset(uint64_t guid, const MaterialAsset& ma)
        {
            auto it = m_byGuid.find(guid);
            if (it == m_byGuid.end() || it->second.type != AssetType::Material)
                return false;

            const std::string absPath = m_workDir + "/" + it->second.path;
            std::ofstream f(absPath, std::ios::trunc);
            if (!f.is_open()) return false;

            char buf[64];
            f << "name="           << ma.name             << "\n";
            snprintf(buf, sizeof(buf), "%llx", (unsigned long long)ma.vertShaderGuid);
            f << "vertShaderGuid=" << buf                 << "\n";
            snprintf(buf, sizeof(buf), "%llx", (unsigned long long)ma.fragShaderGuid);
            f << "fragShaderGuid=" << buf                 << "\n";
            snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f,%.6f",
                     ma.albedo[0], ma.albedo[1], ma.albedo[2], ma.albedo[3]);
            f << "albedo="         << buf                 << "\n";
            f << "roughness="      << ma.roughness        << "\n";
            f << "metallic="       << ma.metallic         << "\n";
            snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f",
                     ma.emissive[0], ma.emissive[1], ma.emissive[2]);
            f << "emissive="       << buf                 << "\n";
            snprintf(buf, sizeof(buf), "%llx", (unsigned long long)ma.albedoTexGuid);
            f << "albedoTexGuid="  << buf                 << "\n";
            snprintf(buf, sizeof(buf), "%llx", (unsigned long long)ma.normalTexGuid);
            f << "normalTexGuid="  << buf                 << "\n";
            snprintf(buf, sizeof(buf), "%llx", (unsigned long long)ma.roughnessTexGuid);
            f << "roughnessTexGuid=" << buf               << "\n";
            snprintf(buf, sizeof(buf), "%llx", (unsigned long long)ma.emissiveTexGuid);
            f << "emissiveTexGuid=" << buf                << "\n";
            return true;
        }

        /// Update the in-memory path for an asset whose sidecar path= was
        /// already changed by another channel (not needed when using the
        /// above helpers, but useful for external tools).
        void NotifyPathChanged(uint64_t guid, const std::string& newRelPath)
        {
            auto it = m_byGuid.find(guid);
            if (it == m_byGuid.end()) return;
            m_byPath.erase(it->second.path);
            it->second.path = NormalizePath(newRelPath);
            m_byPath[it->second.path] = guid;
        }

        // ── PAK helper ────────────────────────────────────────────
        void    SetPak(const std::string& pakPath) { m_pak = std::make_unique<PakFile>(pakPath); }
        PakFile* GetPak() const { return m_pak.get(); }

    private:
        // ── Member variables ─────────────────────────────────────
        bool        m_compiled = false;
        std::string m_workDir;
        std::unique_ptr<PakFile>                        m_pak;
        std::unordered_map<uint64_t, AssetMeta>         m_byGuid;
        std::unordered_map<std::string, uint64_t>       m_byPath;

        //
        //  File content (4 key=value lines):
        //      guid=A3B2C1D4E5F60718
        //      type=Texture
        //      name=stone
        //      path=textures/stone.png

        static std::string SidecarPath(const std::string& absAssetPath)
        {
            return absAssetPath + ".data";
        }

        void WriteSidecar(const std::string& absAssetPath, const AssetMeta& meta) const
        {
            std::ofstream f(SidecarPath(absAssetPath), std::ios::trunc);
            if (!f.is_open())
            {
                std::cerr << "[AssetManager] Cannot write sidecar: "
                          << SidecarPath(absAssetPath) << "\n";
                return;
            }
            f << "guid=" << std::hex << meta.guid << "\n";
            f << "type=" << AssetTypeToString(meta.type) << "\n";
            f << "name=" << meta.name << "\n";
            f << "path=" << meta.path << "\n";
        }

        bool ReadSidecar(const std::string& sidecarAbsPath, AssetMeta& out) const
        {
            std::ifstream f(sidecarAbsPath);
            if (!f.is_open()) return false;

            std::string line;
            std::unordered_map<std::string, std::string> kv;
            while (std::getline(f, line))
            {
                if (line.empty() || line[0] == '#') continue;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
            }

            if (!kv.count("guid") || !kv.count("type") ||
                !kv.count("name") || !kv.count("path"))
                return false;

            try { out.guid = std::stoull(kv["guid"], nullptr, 16); }
            catch (...) { return false; }

            out.type = AssetTypeFromString(kv["type"]);
            out.name = kv["name"];
            out.path = NormalizePath(kv["path"]);
            return true;
        }

        // ── Boot: load every existing sidecar ─────────────────────

        // Load GUID manifest from an already-mounted PAK (compiled/player mode)
        void LoadManifestFromPak()
        {
            if (!m_pak) return;
            const auto bytes = m_pak->Load("assets.manifest");
            if (bytes.empty())
            {
                std::cerr << "[AssetManager] No assets.manifest in PAK — GUID lookups will fail.\n";
                return;
            }
            std::istringstream ss(std::string(bytes.begin(), bytes.end()));
            std::string line;
            int loaded = 0;
            while (std::getline(ss, line))
            {
                if (line.empty() || line[0] == '#') continue;
                // format: <guid_hex>\t<type>\t<name>\t<path>
                std::istringstream ls(line);
                std::string guidHex, typeStr, name, path;
                if (!std::getline(ls, guidHex, '\t')) continue;
                if (!std::getline(ls, typeStr,  '\t')) continue;
                if (!std::getline(ls, name,     '\t')) continue;
                if (!std::getline(ls, path,     '\t')) continue;
                AssetMeta meta;
                try { meta.guid = std::stoull(guidHex, nullptr, 16); } catch (...) { continue; }
                meta.type = AssetTypeFromString(typeStr);
                meta.name = name;
                meta.path = NormalizePath(path);
                RegisterMeta(meta);
                ++loaded;
            }
            std::cout << "[AssetManager] Loaded " << loaded << " entries from assets.manifest\n";
        }

        void LoadAllSidecars()
        {
            if (m_workDir.empty()) return;
            namespace fs = std::filesystem;
            std::error_code ec;
            int loaded = 0;

            for (const auto& entry :
                 fs::recursive_directory_iterator(m_workDir, ec))
            {
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() != ".data") continue;

                AssetMeta meta;
                if (!ReadSidecar(entry.path().string(), meta))
                {
                    std::cerr << "[AssetManager] Malformed sidecar: "
                              << entry.path().string() << "\n";
                    continue;
                }

                // GUID collision: two sidecars with same GUID but different paths
                if (m_byGuid.count(meta.guid) &&
                    m_byGuid[meta.guid].path != meta.path)
                {
                    std::cerr << "[AssetManager] GUID collision:\n"
                              << "  existing: " << m_byGuid[meta.guid].path << "\n"
                              << "  conflict: " << meta.path << "\n"
                              << "  Reassigning GUID for conflict.\n";
                    meta.guid = GenerateGuid(meta.path);
                    // Rewrite sidecar with corrected GUID
                    // asset abs path = sidecar path minus the trailing ".data"
                    std::string assetAbs = entry.path().string();
                    assetAbs.erase(assetAbs.size() - 5); // remove ".data"
                    WriteSidecar(assetAbs, meta);
                }

                RegisterMeta(meta);
                ++loaded;
            }

            std::cout << "[AssetManager] Loaded " << loaded
                      << " sidecars from " << m_workDir << "\n";
        }

        // ── Helpers ───────────────────────────────────────────────

        void RegisterMeta(const AssetMeta& meta)
        {
            m_byGuid[meta.guid] = meta;
            m_byPath[meta.path] = meta.guid;
        }

        uint64_t GenerateGuid(const std::string& normPath) const
        {
            uint64_t c = HashUtil::Hash(normPath);
            if (c == 0) c = 1;
            while (m_byGuid.count(c))
            {
                c ^= c >> 30; c *= 0xBF58476D1CE4E5B9ULL;
                c ^= c >> 27; c *= 0x94D049BB133111EBULL;
                c ^= c >> 31;
                if (c == 0) c = 1;
            }
            return c;
        }

        static std::string NormalizePath(const std::string& p)
        {
            std::string out = p;
            std::replace(out.begin(), out.end(), '\\', '/');
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            while (!out.empty() && out[0] == '/') out.erase(0, 1);
            return out;
        }

        static std::string NormDir(const std::string& d)
        {
            std::string out = d;
            std::replace(out.begin(), out.end(), '\\', '/');
            while (!out.empty() && out.back() == '/') out.pop_back();
            return out;
        }

        static std::string Trim(const std::string& s)
        {
            const std::string ws = " \t\r\n";
            const size_t b = s.find_first_not_of(ws);
            if (b == std::string::npos) return "";
            return s.substr(b, s.find_last_not_of(ws) - b + 1);
        }
    };

} // namespace Krayon
