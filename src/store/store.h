#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace miniredis {

class Store {
public:
    void set(const std::string& key, std::string value);
    std::optional<std::string> get(const std::string& key) const;
    std::size_t size() const;

    // Multi-key operations hold the lock for the whole batch so they are atomic, as in Redis.
    template <typename KeyIt>
    std::size_t del(KeyIt first, KeyIt last) {
        std::lock_guard lock(mutex_);
        std::size_t removed = 0;
        for (; first != last; ++first) {
            removed += data_.erase(*first);
        }
        return removed;
    }

    template <typename KeyIt>
    std::size_t exists(KeyIt first, KeyIt last) const {
        std::lock_guard lock(mutex_);
        std::size_t found = 0;
        for (; first != last; ++first) {
            found += data_.count(*first);
        }
        return found;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

}  // namespace miniredis
