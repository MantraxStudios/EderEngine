#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  KRCommon.h — shared types for the Krayon PAK system
//  Included by both KRCompiler.h and PakFile.h
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace Krayon
{
    // ─────────────────────────────────────────────────────────────
    //  HashUtil  — FNV-1a 64-bit, case-insensitive forward-slash paths
    // ─────────────────────────────────────────────────────────────

    namespace HashUtil
    {
        inline std::string Normalize(const std::string& assetName)
        {
            std::string out = assetName;
            std::replace(out.begin(), out.end(), '\\', '/');
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            size_t start = out.find_first_not_of('/');
            return (start == std::string::npos) ? "" : out.substr(start);
        }

        inline uint64_t Hash(const std::string& text)
        {
            const std::string norm = Normalize(text);
            uint64_t h = 14695981039346656037ULL;
            for (unsigned char c : norm) { h ^= c; h *= 1099511628211ULL; }
            return h;
        }
    }

    // ─────────────────────────────────────────────────────────────
    //  Crypto  — single-byte XOR cipher (key = 0xAC)
    // ─────────────────────────────────────────────────────────────

    namespace Crypto
    {
        constexpr uint8_t KEY = 0xAC;

        inline void Apply(uint8_t* data, size_t size)
        {
            for (size_t i = 0; i < size; ++i) data[i] ^= KEY;
        }

        inline void Apply(std::vector<uint8_t>& data)
        {
            Apply(data.data(), data.size());
        }
    }

    // ─────────────────────────────────────────────────────────────
    //  AssetEntry  — one TOC record (20 bytes on disk)
    // ─────────────────────────────────────────────────────────────

    struct AssetEntry
    {
        uint64_t id     = 0;
        int64_t  offset = 0;
        int32_t  size   = 0;
    };

} // namespace Krayon
