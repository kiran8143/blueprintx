// Author: Udaykiran Atta
// License: MIT

#pragma once

#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

/// Thread-safe LRU cache with per-entry TTL support.
///
/// Uses a doubly-linked list (std::list) for LRU ordering and an
/// unordered_map for O(1) key lookup.  Reads acquire a shared lock;
/// writes (put, invalidate, evict) acquire a unique lock.
///
/// Expiry is checked lazily on every get() call.  For bulk cleanup
/// call evictExpired() periodically.
template <typename Key, typename Value>
class LruCache {
public:
    struct CacheEntry {
        Key key;
        Value value;
        std::chrono::steady_clock::time_point expiresAt;
    };

    /// @param maxSize   Maximum number of entries before LRU eviction.
    /// @param defaultTtl Default time-to-live applied when put() is
    ///                    called without an explicit TTL.
    explicit LruCache(size_t maxSize,
                      std::chrono::seconds defaultTtl = std::chrono::seconds(60))
        : maxSize_(maxSize)
        , defaultTtl_(defaultTtl) {}

    /// Insert or update an entry.
    /// If the cache is at capacity the least-recently-used entry is evicted.
    void put(const Key& key, Value value,
             std::optional<std::chrono::seconds> ttl = std::nullopt) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(key);
        if (it != index_.end()) {
            // Move existing entry to front (most recently used)
            entries_.splice(entries_.begin(), entries_, it->second);
            it->second->value = std::move(value);
            it->second->expiresAt = std::chrono::steady_clock::now() +
                                    ttl.value_or(defaultTtl_);
            return;
        }

        // Evict LRU entry if at capacity
        if (entries_.size() >= maxSize_) {
            auto& back = entries_.back();
            index_.erase(back.key);
            entries_.pop_back();
        }

        // Insert at front
        entries_.emplace_front(CacheEntry{
            key,
            std::move(value),
            std::chrono::steady_clock::now() + ttl.value_or(defaultTtl_)});
        index_[key] = entries_.begin();
    }

    /// Retrieve a value.  Returns std::nullopt if the key is absent or
    /// the entry has expired (lazy expiration -- expired entries are
    /// removed on access).
    std::optional<Value> get(const Key& key) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }

        // Check expiry
        if (std::chrono::steady_clock::now() >= it->second->expiresAt) {
            entries_.erase(it->second);
            index_.erase(it);
            return std::nullopt;
        }

        // Promote to front (most recently used)
        entries_.splice(entries_.begin(), entries_, it->second);
        return it->second->value;
    }

    /// Remove a single entry by key.
    void invalidate(const Key& key) {
        std::unique_lock lock(mutex_);

        auto it = index_.find(key);
        if (it != index_.end()) {
            entries_.erase(it->second);
            index_.erase(it);
        }
    }

    /// Remove all entries whose key starts with @p prefix.
    /// Only available when Key is std::string (tag-based invalidation).
    void invalidateByPrefix(const std::string& prefix) {
        static_assert(std::is_same_v<Key, std::string>,
                      "invalidateByPrefix requires Key = std::string");

        std::unique_lock lock(mutex_);

        for (auto it = index_.begin(); it != index_.end(); ) {
            if (it->first.substr(0, prefix.size()) == prefix) {
                entries_.erase(it->second);
                it = index_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Remove all entries.
    void clear() {
        std::unique_lock lock(mutex_);
        entries_.clear();
        index_.clear();
    }

    /// Number of entries currently stored (including possibly-expired ones).
    size_t size() const {
        std::shared_lock lock(mutex_);
        return entries_.size();
    }

    /// Bulk-remove every expired entry.
    void evictExpired() {
        std::unique_lock lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (now >= it->expiresAt) {
                index_.erase(it->key);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    size_t maxSize_;
    std::chrono::seconds defaultTtl_;
    mutable std::shared_mutex mutex_;

    // Front = most recently used, back = least recently used
    std::list<CacheEntry> entries_;

    // Maps key -> iterator into entries_ list for O(1) lookup
    std::unordered_map<Key, typename std::list<CacheEntry>::iterator> index_;
};
