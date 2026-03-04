#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  KRCompiler.h  — PAK builder
//  Header-only C++17.
//
//  PAK layout
//  ┌──────────────────────────────────────────────────────┐
//  │  Header (20 bytes)                                   │
//  │    [0..3]  magic      = "MPAK"                       │
//  │    [4..7]  version    = 1  (int32_t)                 │
//  │    [8..11] count      = N  (int32_t, assets stored)  │
//  │   [12..19] tocOffset  = T  (int64_t)                 │
//  ├──────────────────────────────────────────────────────┤
//  │  Data blobs  (XOR-encrypted, variable length)        │
//  ├──────────────────────────────────────────────────────┤
//  │  TOC  (20 bytes × N)                                 │
//  │    id     : uint64_t                                 │
//  │    offset : int64_t                                  │
//  │    size   : int32_t                                  │
//  └──────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <functional>
#include "KRCommon.h"

namespace Krayon
{
    // ─────────────────────────────────────────────────────────────
    //  GameConfig  — embedded as "game.conf" inside every PAK.
    //  Stores the startup scene path and the display name of the game.
    // ─────────────────────────────────────────────────────────────

    struct GameConfig
    {
        std::string gameName      = "EderGame";
        std::string initialScene;             // relative e.g. "scenes/main.scene"

        // Serialize to plain-text key=value lines
        std::string Serialize() const
        {
            return "gameName="     + gameName     + "\n"
                 + "initialScene=" + initialScene + "\n";
        }

        // Deserialize from raw bytes (decrypted by PakFile before arriving here)
        static GameConfig Deserialize(const std::vector<uint8_t>& bytes)
        {
            GameConfig cfg;
            std::istringstream ss(std::string(bytes.begin(), bytes.end()));
            std::string line;
            while (std::getline(ss, line))
            {
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq);
                const std::string val = line.substr(eq + 1);
                if (key == "gameName")     cfg.gameName     = val;
                if (key == "initialScene") cfg.initialScene = val;
            }
            return cfg;
        }

        // Convenience: deserialise directly from a string
        static GameConfig FromString(const std::string& txt)
        {
            const std::vector<uint8_t> b(txt.begin(), txt.end());
            return Deserialize(b);
        }
    };

    // ─────────────────────────────────────────────────────────────
    //  KRCompiler
    // ─────────────────────────────────────────────────────────────

    class KRCompiler
    {
    public:
        // ── progress callback: (assetsWritten, assetsTotal, currentName) ──────
        using ProgressCb = std::function<void(int, int, const std::string&)>;

        // ── Build overload 1: purely from files (original API kept intact) ────
        static void Build(const std::string& pakPath,
                          const std::unordered_map<std::string, std::string>& assets,
                          ProgressCb progressCb = nullptr)
        {
            BuildImpl(pakPath, assets, {}, progressCb);
        }

        // ── Build overload 2: files + in-memory blobs (e.g. game.conf) ───────
        static void Build(const std::string& pakPath,
                          const std::unordered_map<std::string, std::string>& fileAssets,
                          const std::unordered_map<std::string, std::vector<uint8_t>>& memAssets,
                          ProgressCb progressCb = nullptr)
        {
            BuildImpl(pakPath, fileAssets, memAssets, progressCb);
        }

    private:
        static void BuildImpl(const std::string& pakPath,
                              const std::unordered_map<std::string, std::string>& fileAssets,
                              const std::unordered_map<std::string, std::vector<uint8_t>>& memAssets,
                              ProgressCb progressCb)
        {
            // ── Collision check ───────────────────────────────────
            std::unordered_map<uint64_t, std::string> hashToName;
            hashToName.reserve(fileAssets.size() + memAssets.size());

            auto checkCollision = [&](const std::string& name) {
                const std::string normalized = HashUtil::Normalize(name);
                const uint64_t    id         = HashUtil::Hash(name);
                auto it = hashToName.find(id);
                if (it != hashToName.end())
                {
                    if (it->second != normalized)
                        throw std::runtime_error(
                            "[KRCompiler] Hash collision between '" + it->second +
                            "' and '" + normalized + "'");
                    return false; // duplicate — skip
                }
                hashToName[id] = normalized;
                return true;
            };

            for (const auto& [name, path] : fileAssets) checkCollision(name);
            for (const auto& [name, data] : memAssets)  checkCollision(name);

            // ── Open output file ──────────────────────────────────
            std::ofstream out(pakPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                throw std::runtime_error("[KRCompiler] Cannot open output file: " + pakPath);

            // ── Write placeholder header (20 bytes) ───────────────
            const char magic[4] = { 'M','P','A','K' };
            out.write(magic, 4);
            WriteI32(out, 1);   // version
            WriteI32(out, 0);   // count  (patched later)
            WriteI64(out, 0LL); // tocOffset (patched later)

            // ── Write asset data blobs ────────────────────────────
            std::vector<AssetEntry> toc;
            toc.reserve(fileAssets.size() + memAssets.size());

            const int total = static_cast<int>(fileAssets.size() + memAssets.size());
            int written = 0;

            // ── File-backed assets ────────────────────────────────
            for (const auto& [name, filePath] : fileAssets)
            {
                if (progressCb) progressCb(written, total, name);

                std::ifstream src(filePath, std::ios::binary);
                if (!src.is_open())
                {
                    std::cout << "[KRCompiler] Warning: file not found '" << filePath << "', skipped.\n";
                    ++written; continue;
                }

                src.seekg(0, std::ios::end);
                const std::streamsize sz = src.tellg();
                src.seekg(0, std::ios::beg);

                std::vector<uint8_t> data(static_cast<size_t>(sz));
                if (!src.read(reinterpret_cast<char*>(data.data()), sz))
                    throw std::runtime_error("[KRCompiler] Failed to read: " + filePath);

                Crypto::Apply(data);

                AssetEntry e;
                e.id     = HashUtil::Hash(name);
                e.offset = static_cast<int64_t>(out.tellp());
                e.size   = static_cast<int32_t>(data.size());
                toc.push_back(e);

                out.write(reinterpret_cast<const char*>(data.data()),
                          static_cast<std::streamsize>(data.size()));
                ++written;
            }

            // ── In-memory assets (e.g. game.conf) ────────────────
            for (const auto& [name, rawData] : memAssets)
            {
                if (progressCb) progressCb(written, total, name);

                std::vector<uint8_t> data = rawData; // copy, then encrypt
                Crypto::Apply(data);

                AssetEntry e;
                e.id     = HashUtil::Hash(name);
                e.offset = static_cast<int64_t>(out.tellp());
                e.size   = static_cast<int32_t>(data.size());
                toc.push_back(e);

                out.write(reinterpret_cast<const char*>(data.data()),
                          static_cast<std::streamsize>(data.size()));
                ++written;
            }

            if (progressCb) progressCb(total, total, "");

            // ── Write TOC ─────────────────────────────────────────
            const int64_t tocOffset = static_cast<int64_t>(out.tellp());
            for (const AssetEntry& e : toc)
            {
                WriteU64(out, e.id);
                WriteI64(out, e.offset);
                WriteI32(out, e.size);
            }

            // ── Patch header ──────────────────────────────────────
            out.seekp(8, std::ios::beg);
            WriteI32(out, static_cast<int32_t>(toc.size()));
            WriteI64(out, tocOffset);

            out.close();
            std::cout << "[KRCompiler] Built '" << pakPath << "' — "
                      << toc.size() << " assets.\n";
        }

        static void WriteI32(std::ostream& s, int32_t v)
        {
            s.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }
        static void WriteI64(std::ostream& s, int64_t v)
        {
            s.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }
        static void WriteU64(std::ostream& s, uint64_t v)
        {
            s.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }
    };

} // namespace Krayon
