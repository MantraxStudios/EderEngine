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
#include <stdexcept>
#include <iostream>
#include "KRCommon.h"

namespace Krayon
{
    // ─────────────────────────────────────────────────────────────
    //  KRCompiler
    // ─────────────────────────────────────────────────────────────

    class KRCompiler
    {
    public:
        /// <summary>
        /// Builds a PAK file at <paramref name="pakPath"/> from a map of
        /// { assetName -> filePath } pairs.
        /// Throws std::runtime_error on hash collisions or I/O errors.
        /// </summary>
        static void Build(const std::string& pakPath,
                          const std::unordered_map<std::string, std::string>& assets)
        {
            // ── Collision check ───────────────────────────────────
            std::unordered_map<uint64_t, std::string> hashToName;
            hashToName.reserve(assets.size());

            for (const auto& [name, path] : assets)
            {
                std::string normalized = HashUtil::Normalize(name);
                uint64_t    id         = HashUtil::Hash(name);

                auto it = hashToName.find(id);
                if (it != hashToName.end())
                {
                    if (it->second != normalized)
                        throw std::runtime_error(
                            "[KRCompiler] Hash collision between '" + it->second +
                            "' and '" + normalized + "'");
                    else
                        std::cout << "[KRCompiler] Warning: duplicate asset '" << normalized
                                  << "', skipped.\n";
                    continue;
                }
                hashToName[id] = normalized;
            }

            // ── Open output file ──────────────────────────────────
            std::ofstream fs(pakPath, std::ios::binary | std::ios::trunc);
            if (!fs.is_open())
                throw std::runtime_error("[KRCompiler] Cannot open output file: " + pakPath);

            // ── Write placeholder header (20 bytes) ───────────────
            // magic
            const char magic[4] = { 'M','P','A','K' };
            fs.write(magic, 4);
            // version = 1
            WriteI32(fs, 1);
            // count placeholder
            WriteI32(fs, 0);
            // tocOffset placeholder
            WriteI64(fs, 0LL);

            // ── Write asset data blobs ────────────────────────────
            std::vector<AssetEntry> toc;
            toc.reserve(assets.size());

            for (const auto& [name, filePath] : assets)
            {
                // Check file exists
                std::ifstream src(filePath, std::ios::binary);
                if (!src.is_open())
                {
                    std::cout << "[KRCompiler] Warning: file not found '" << filePath
                              << "', skipped.\n";
                    continue;
                }

                // Read bytes
                src.seekg(0, std::ios::end);
                std::streamsize bytes = src.tellg();
                src.seekg(0, std::ios::beg);

                std::vector<uint8_t> data(static_cast<size_t>(bytes));
                if (!src.read(reinterpret_cast<char*>(data.data()), bytes))
                    throw std::runtime_error("[KRCompiler] Failed to read: " + filePath);

                // Encrypt
                Crypto::Apply(data);

                // Record TOC entry BEFORE writing
                AssetEntry entry;
                entry.id     = HashUtil::Hash(name);
                entry.offset = static_cast<int64_t>(fs.tellp());
                entry.size   = static_cast<int32_t>(data.size());
                toc.push_back(entry);

                fs.write(reinterpret_cast<const char*>(data.data()),
                         static_cast<std::streamsize>(data.size()));
            }

            // ── Write TOC ─────────────────────────────────────────
            int64_t tocOffset = static_cast<int64_t>(fs.tellp());

            for (const AssetEntry& e : toc)
            {
                WriteU64(fs, e.id);
                WriteI64(fs, e.offset);
                WriteI32(fs, e.size);
            }

            // ── Patch header: count + tocOffset ──────────────────
            fs.seekp(8, std::ios::beg);
            WriteI32(fs, static_cast<int32_t>(toc.size()));
            WriteI64(fs, tocOffset);

            fs.close();
            std::cout << "[KRCompiler] Built '" << pakPath << "' — "
                      << toc.size() << " assets.\n";
        }

    private:
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
