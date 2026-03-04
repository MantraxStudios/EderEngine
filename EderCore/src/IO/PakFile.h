#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  PakFile.h  — PAK runtime (LRU cache + Flyweight PakCore + public PakFile)
//  Header-only C++17 — Windows & POSIX (Android/Linux).
//
//  Usage:
//    PakFile pak("Game.pak");
//    std::vector<uint8_t> bytes = pak.Load("textures/stone.png");
//    auto fut = pak.LoadAsync("models/mesh.bin");
//    PakFile::ReleaseAll();   // call once when the game closes
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <future>
#include <atomic>
#include <stdexcept>
#include <iostream>
#include <cassert>

#include "KRCommon.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Platform memory-mapped file
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace Krayon
{
    // ─────────────────────────────────────────────────────────────
    //  MappedFile — RAII cross-platform read-only mmap
    // ─────────────────────────────────────────────────────────────

    class MappedFile
    {
    public:
        MappedFile() = default;

        explicit MappedFile(const std::string& path)
        {
            Open(path);
        }

        ~MappedFile() { Close(); }

        MappedFile(const MappedFile&)            = delete;
        MappedFile& operator=(const MappedFile&) = delete;

        MappedFile(MappedFile&& o) noexcept { Steal(o); }
        MappedFile& operator=(MappedFile&& o) noexcept
        {
            if (this != &o) { Close(); Steal(o); }
            return *this;
        }

        void Open(const std::string& path)
        {
            Close();
#ifdef _WIN32
            _fileHandle = ::CreateFileA(
                path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (_fileHandle == INVALID_HANDLE_VALUE)
                throw std::runtime_error("[MappedFile] Cannot open: " + path);

            LARGE_INTEGER li;
            if (!::GetFileSizeEx(_fileHandle, &li))
                throw std::runtime_error("[MappedFile] Cannot stat: " + path);
            _size = static_cast<size_t>(li.QuadPart);

            _mapHandle = ::CreateFileMappingA(
                _fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (!_mapHandle)
                throw std::runtime_error("[MappedFile] CreateFileMapping failed: " + path);

            _data = static_cast<const uint8_t*>(
                ::MapViewOfFile(_mapHandle, FILE_MAP_READ, 0, 0, 0));
            if (!_data)
                throw std::runtime_error("[MappedFile] MapViewOfFile failed: " + path);
#else
            _fd = ::open(path.c_str(), O_RDONLY);
            if (_fd < 0)
                throw std::runtime_error("[MappedFile] Cannot open: " + path);

            struct ::stat st {};
            if (::fstat(_fd, &st) < 0)
                throw std::runtime_error("[MappedFile] Cannot stat: " + path);
            _size = static_cast<size_t>(st.st_size);

            void* p = ::mmap(nullptr, _size, PROT_READ, MAP_PRIVATE, _fd, 0);
            if (p == MAP_FAILED)
                throw std::runtime_error("[MappedFile] mmap failed: " + path);
            _data = static_cast<const uint8_t*>(p);
#endif
        }

        void Close() noexcept
        {
#ifdef _WIN32
            if (_data)       { ::UnmapViewOfFile(_data);    _data = nullptr;       }
            if (_mapHandle)  { ::CloseHandle(_mapHandle);   _mapHandle = nullptr;  }
            if (_fileHandle && _fileHandle != INVALID_HANDLE_VALUE)
                             { ::CloseHandle(_fileHandle);  _fileHandle = INVALID_HANDLE_VALUE; }
#else
            if (_data && _size) { ::munmap(const_cast<uint8_t*>(_data), _size); _data = nullptr; }
            if (_fd >= 0)       { ::close(_fd);  _fd = -1; }
#endif
            _size = 0;
        }

        bool        IsOpen()  const noexcept { return _data != nullptr; }
        size_t      Size()    const noexcept { return _size; }
        const uint8_t* Data() const noexcept { return _data; }

        // Bounds-checked read helpers (used by TOC loader)
        template<typename T>
        T Read(size_t offset) const
        {
            assert(offset + sizeof(T) <= _size && "MappedFile: read out of bounds");
            T v;
            std::memcpy(&v, _data + offset, sizeof(T));
            return v;
        }

    private:
        const uint8_t* _data = nullptr;
        size_t         _size = 0;
#ifdef _WIN32
        HANDLE _fileHandle = INVALID_HANDLE_VALUE;
        HANDLE _mapHandle  = nullptr;
#else
        int _fd = -1;
#endif
        void Steal(MappedFile& o) noexcept
        {
            _data = o._data; o._data = nullptr;
            _size = o._size; o._size = 0;
#ifdef _WIN32
            _fileHandle = o._fileHandle; o._fileHandle = INVALID_HANDLE_VALUE;
            _mapHandle  = o._mapHandle;  o._mapHandle  = nullptr;
#else
            _fd = o._fd; o._fd = -1;
#endif
        }
    };

    // ─────────────────────────────────────────────────────────────
    //  LruCache — thread-safe byte-budget LRU
    //  Stores owned copies; returns owned copies to callers.
    // ─────────────────────────────────────────────────────────────

    class LruCache
    {
        struct Node
        {
            uint64_t             key;
            std::vector<uint8_t> data;
        };

        using Iter = std::list<Node>::iterator;

        std::list<Node>                          _list;
        std::unordered_map<uint64_t, Iter>       _map;
        int64_t                                  _maxBytes;
        int64_t                                  _usedBytes = 0;
        mutable std::shared_mutex                _rwLock;

    public:
        explicit LruCache(int64_t maxBytes) : _maxBytes(maxBytes) {}

        // Returns a copy; returns false if miss
        bool TryGet(uint64_t key, std::vector<uint8_t>& out) const
        {
            std::shared_lock lock(_rwLock);
            auto it = _map.find(key);
            if (it == _map.end()) return false;
            out = it->second->data;   // copy
            return true;
        }

        void Put(uint64_t key, std::vector<uint8_t> data)
        {
            std::unique_lock lock(_rwLock);
            if (_map.count(key)) { MoveToFront(_map[key]); return; }

            int64_t bytes = static_cast<int64_t>(data.size());
            _list.push_front({ key, std::move(data) });
            _map[key] = _list.begin();
            _usedBytes += bytes;

            while (_usedBytes > _maxBytes && !_list.empty())
                Evict(std::prev(_list.end()));
        }

        void Invalidate(uint64_t key)
        {
            std::unique_lock lock(_rwLock);
            auto it = _map.find(key);
            if (it != _map.end()) Evict(it->second);
        }

        void Clear()
        {
            std::unique_lock lock(_rwLock);
            _list.clear();
            _map.clear();
            _usedBytes = 0;
        }

    private:
        void MoveToFront(Iter it)
        {
            if (it != _list.begin())
                _list.splice(_list.begin(), _list, it);
        }

        void Evict(Iter it)
        {
            _usedBytes -= static_cast<int64_t>(it->data.size());
            _map.erase(it->key);
            _list.erase(it);
        }
    };

    // ─────────────────────────────────────────────────────────────
    //  PakCore — Flyweight: one instance per physical file
    // ─────────────────────────────────────────────────────────────

    class PakCore
    {
        // ── Static registry ─────────────────────────────────────
        inline static std::mutex                                    s_registryLock;
        inline static std::unordered_map<std::string,
                                         std::shared_ptr<PakCore>> s_registry;

    public:
        /// Returns (and caches) the core for the given path. Thread-safe.
        static std::shared_ptr<PakCore> Acquire(const std::string& pakPath)
        {
            std::lock_guard lock(s_registryLock);
            auto it = s_registry.find(pakPath);
            if (it != s_registry.end()) return it->second;

            auto core = std::shared_ptr<PakCore>(new PakCore(pakPath));
            s_registry[pakPath] = core;
            return core;
        }

        /// Call once at shutdown to free all memory-mapped files.
        static void ReleaseAll()
        {
            std::lock_guard lock(s_registryLock);
            s_registry.clear();
        }

        // ── Config ───────────────────────────────────────────────
        int64_t cacheMaxBytes = 128LL * 1024 * 1024;

        // ── Public API (called by PakFile) ───────────────────────

        bool ContainsAsset(uint64_t id) const
        {
            return _assets.count(id) > 0;
        }

        /// Returns decrypted asset bytes, or empty vector if not found.
        std::vector<uint8_t> Load(uint64_t id, const std::string& nameForLog)
        {
            EnsureCache();

            std::vector<uint8_t> cached;
            if (_cache->TryGet(id, cached)) return cached;

            auto it = _assets.find(id);
            if (it == _assets.end())
            {
                std::cerr << "[PakFile] Asset not found: '" << nameForLog
                          << "' (hash: " << std::hex << id << std::dec << ")\n";
                return {};
            }

            std::vector<uint8_t> data = ReadEntry(it->second);

            // Store a copy in the cache
            _cache->Put(id, data);  // Put takes by value, data stays valid

            return data;
        }

        void InvalidateCache(uint64_t id)
        {
            if (_cache) _cache->Invalidate(id);
        }

        void ClearCache()
        {
            if (_cache) _cache->Clear();
        }

    private:
        MappedFile                            _mmf;
        std::unordered_map<uint64_t, AssetEntry> _assets;
        std::unique_ptr<LruCache>             _cache;
        std::once_flag                        _cacheFlag;

        explicit PakCore(const std::string& pakPath)
        {
            _mmf.Open(pakPath);
            LoadToc(pakPath);
        }

        void LoadToc(const std::string& pakPath)
        {
            const uint8_t* d    = _mmf.Data();
            const size_t   size = _mmf.Size();

            if (size < 20)
                throw std::runtime_error("[PakFile] File too small: " + pakPath);

            // magic
            if (d[0]!='M' || d[1]!='P' || d[2]!='A' || d[3]!='K')
                throw std::runtime_error("[PakFile] Invalid magic: " + pakPath);

            // int32 version @ 4
            // int32 count   @ 8
            int32_t count = _mmf.Read<int32_t>(8);
            // int64 tocOffset @ 12
            int64_t tocOffset = _mmf.Read<int64_t>(12);

            size_t pos = static_cast<size_t>(tocOffset);
            for (int32_t i = 0; i < count; ++i)
            {
                if (pos + 20 > size)
                    throw std::runtime_error("[PakFile] TOC truncated: " + pakPath);

                AssetEntry e;
                e.id     = _mmf.Read<uint64_t>(pos);      pos += 8;
                e.offset = _mmf.Read<int64_t>(pos);       pos += 8;
                e.size   = _mmf.Read<int32_t>(pos);       pos += 4;

                if (_assets.count(e.id))
                    std::cerr << "[PakFile] Warning: duplicate hash in TOC: "
                              << std::hex << e.id << std::dec << "\n";

                _assets[e.id] = e;
            }

            std::cout << "[PakFile] Loaded: " << _assets.size()
                      << " assets from '" << pakPath << "'\n";
        }

        void EnsureCache()
        {
            std::call_once(_cacheFlag, [this]
            {
                _cache = std::make_unique<LruCache>(cacheMaxBytes);
            });
        }

        std::vector<uint8_t> ReadEntry(const AssetEntry& e) const
        {
            if (e.offset < 0 || e.size <= 0)
                return {};
            size_t off = static_cast<size_t>(e.offset);
            size_t sz  = static_cast<size_t>(e.size);
            if (off + sz > _mmf.Size())
                throw std::runtime_error("[PakFile] Asset data out of file bounds.");

            std::vector<uint8_t> data(_mmf.Data() + off, _mmf.Data() + off + sz);
            Crypto::Apply(data.data(), data.size());
            return data;
        }
    };

    // ─────────────────────────────────────────────────────────────
    //  PakFile — public façade (lightweight, can copy / stack-alloc)
    // ─────────────────────────────────────────────────────────────

    class PakFile
    {
    public:
        // ── Shutdown ─────────────────────────────────────────────
        /// Releases ALL loaded PAK files. Call once at game shutdown.
        static void ReleaseAll() { PakCore::ReleaseAll(); }

        // ── Construction ─────────────────────────────────────────
        /// O(1) if the PAK was already loaded by another PakFile instance.
        explicit PakFile(const std::string& pakPath)
            : _core(PakCore::Acquire(pakPath))
        {}

        // ── Cache budget (per-PAK)  ───────────────────────────────
        int64_t GetCacheMaxBytes()  const { return _core->cacheMaxBytes; }
        void    SetCacheMaxBytes(int64_t v) { _core->cacheMaxBytes = v; }

        // ── Synchronous load ─────────────────────────────────────
        /// Returns decrypted bytes, or empty vector if the asset is missing.
        std::vector<uint8_t> Load(const std::string& assetName)
        {
            ThrowIfNull();
            return _core->Load(HashUtil::Hash(assetName), assetName);
        }

        // ── Asynchronous load ────────────────────────────────────
        std::future<std::vector<uint8_t>> LoadAsync(const std::string& assetName)
        {
            ThrowIfNull();
            uint64_t id = HashUtil::Hash(assetName);

            // Check contains before scheduling, to avoid a spurious thread start
            if (!_core->ContainsAsset(id))
                return std::async(std::launch::deferred,
                    []() -> std::vector<uint8_t> { return {}; });

            std::shared_ptr<PakCore> core = _core;
            return std::async(std::launch::async,
                [core, id, assetName]() { return core->Load(id, assetName); });
        }

        // ── Prefetch ──────────────────────────────────────────────
        /// Loads a batch of assets into the cache in a background thread.
        std::future<void> PrefetchAsync(std::vector<std::string> names)
        {
            ThrowIfNull();
            std::shared_ptr<PakCore> core = _core;
            return std::async(std::launch::async,
                [core, names = std::move(names)]()
                {
                    for (const std::string& name : names)
                        core->Load(HashUtil::Hash(name), name);
                });
        }

        // ── Existence check ───────────────────────────────────────
        bool Contains(const std::string& assetName) const
        {
            ThrowIfNull();
            return _core->ContainsAsset(HashUtil::Hash(assetName));
        }

        // ── Cache control ─────────────────────────────────────────
        void InvalidateCache(const std::string& assetName)
        {
            if (_core) _core->InvalidateCache(HashUtil::Hash(assetName));
        }

        void ClearCache()
        {
            if (_core) _core->ClearCache();
        }

        bool IsValid() const { return _core != nullptr; }

    private:
        std::shared_ptr<PakCore> _core;

        void ThrowIfNull() const
        {
            if (!_core)
                throw std::runtime_error("[PakFile] PakFile instance is invalid.");
        }
    };

} // namespace Krayon
