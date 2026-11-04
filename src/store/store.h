#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "store/lru_list.h"

namespace miniredis {

// Wall-clock time, so that absolute expiry deadlines survive a restart once they are persisted.
using Clock = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock, std::chrono::milliseconds>;

inline TimePoint system_now() {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(Clock::now());
}

struct StoreOptions {
    std::size_t max_keys = 0;  // 0 disables eviction
    std::function<TimePoint()> now = system_now;
};

struct Ttl {
    enum class State { Missing, NoExpiry, Expiring };
    State state = State::Missing;
    std::chrono::milliseconds remaining{0};
};

class Store {
public:
    explicit Store(StoreOptions options = {});

    TimePoint now() const { return options_.now(); }

    // Replaces the value and any previous TTL. A deadline already in the past deletes the key.
    void set(const std::string& key, std::string value,
             std::optional<TimePoint> expires_at = std::nullopt);
    std::optional<std::string> get(const std::string& key);

    // Returns false if the key does not exist. A deadline already in the past deletes the key.
    bool expire_at(const std::string& key, TimePoint deadline);
    Ttl ttl(const std::string& key);

    // Multi-key operations hold the lock for the whole batch so they are atomic, as in Redis.
    template <typename KeyIt>
    std::size_t del(KeyIt first, KeyIt last) {
        std::lock_guard lock(mutex_);
        const TimePoint current = now();
        std::size_t removed = 0;
        for (; first != last; ++first) {
            if (Entry* entry = find_live(*first, current)) {
                erase(*entry);
                ++removed;
            }
        }
        return removed;
    }

    template <typename KeyIt>
    std::size_t exists(KeyIt first, KeyIt last) {
        std::lock_guard lock(mutex_);
        const TimePoint current = now();
        std::size_t found = 0;
        for (; first != last; ++first) {
            found += find_live(*first, current) != nullptr ? 1 : 0;
        }
        return found;
    }

    // One cycle of active expiry; returns how many keys it removed.
    std::size_t sweep_expired();

    // Includes keys that have expired but not yet been reclaimed, like Redis's DBSIZE.
    std::size_t size() const;

private:
    struct Entry {
        std::string value;
        std::optional<TimePoint> expires_at;
        std::size_t volatile_index = 0;
        const std::string* key = nullptr;
        Entry* lru_prev = nullptr;
        Entry* lru_next = nullptr;
    };

    Entry* find_live(const std::string& key, TimePoint current);
    Entry& insert(const std::string& key);
    void erase(Entry& entry);
    void evict_least_recent();
    void set_deadline(Entry& entry, TimePoint deadline);
    void clear_deadline(Entry& entry);
    std::size_t sweep_sample(TimePoint current);

    static bool is_expired(const Entry& entry, TimePoint current) {
        return entry.expires_at && *entry.expires_at <= current;
    }

    StoreOptions options_;
    mutable std::mutex mutex_;
    // Element addresses stay valid across rehashing, which is what lets the LRU list and the
    // volatile-key index hold plain pointers into the map.
    std::unordered_map<std::string, Entry> entries_;
    LruList<Entry> lru_;
    std::vector<Entry*> volatile_;  // keys with a TTL, for O(1) random sampling
    std::mt19937 random_;
};

}  // namespace miniredis
