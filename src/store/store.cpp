#include "store/store.h"

namespace miniredis {

namespace {

std::string to_unix_ms(TimePoint deadline) {
    return std::to_string(deadline.time_since_epoch().count());
}

constexpr std::size_t kSweepSampleSize = 20;
constexpr auto kSweepTimeBudget = std::chrono::milliseconds(2);

}  // namespace

Store::Store(StoreOptions options)
    : options_(std::move(options)), random_(std::random_device{}()) {}

void Store::set(const std::string& key, std::string value, std::optional<TimePoint> expires_at) {
    std::lock_guard lock(mutex_);
    const TimePoint current = now();
    Entry* entry = find_live(key, current);
    if (expires_at && *expires_at <= current) {
        if (entry != nullptr) {
            log_and_erase(*entry);
        }
        return;
    }

    // The eviction must reach the log before the SET that caused it: a replay sees no GETs, so
    // its LRU order differs and it must not be left to pick a victim of its own.
    if (entry == nullptr && options_.max_keys > 0 && entries_.size() >= options_.max_keys) {
        evict_least_recent();
    }
    // Deadlines are logged as absolute times so a replay does not restart the countdown.
    if (log_ != nullptr) {
        log_->append(expires_at ? Command{"SET", key, value, "PXAT", to_unix_ms(*expires_at)}
                                : Command{"SET", key, value});
    }
    if (entry == nullptr) {
        entry = &insert(key);
    } else {
        lru_.move_to_front(entry);
        clear_deadline(*entry);
    }
    entry->value = std::move(value);
    if (expires_at) {
        set_deadline(*entry, *expires_at);
    }
}

std::optional<std::string> Store::get(const std::string& key) {
    std::lock_guard lock(mutex_);
    Entry* entry = find_live(key, now());
    if (entry == nullptr) {
        return std::nullopt;
    }
    lru_.move_to_front(entry);
    return entry->value;
}

bool Store::expire_at(const std::string& key, TimePoint deadline) {
    std::lock_guard lock(mutex_);
    const TimePoint current = now();
    Entry* entry = find_live(key, current);
    if (entry == nullptr) {
        return false;
    }
    if (deadline <= current) {
        log_and_erase(*entry);
        return true;
    }
    log({"PEXPIREAT", key, to_unix_ms(deadline)});
    lru_.move_to_front(entry);
    clear_deadline(*entry);
    set_deadline(*entry, deadline);
    return true;
}

Ttl Store::ttl(const std::string& key) {
    std::lock_guard lock(mutex_);
    const TimePoint current = now();
    const Entry* entry = find_live(key, current);
    if (entry == nullptr) {
        return {Ttl::State::Missing, {}};
    }
    if (!entry->expires_at) {
        return {Ttl::State::NoExpiry, {}};
    }
    return {Ttl::State::Expiring, *entry->expires_at - current};
}

std::size_t Store::sweep_expired() {
    const auto deadline = std::chrono::steady_clock::now() + kSweepTimeBudget;
    std::size_t removed = 0;
    for (;;) {
        std::size_t expired = 0;
        std::size_t sampled = 0;
        {
            // The lock is released between samples so clients are not stalled by a long sweep.
            std::lock_guard lock(mutex_);
            sampled = std::min(kSweepSampleSize, volatile_.size());
            expired = sweep_sample(now());
        }
        removed += expired;
        // Redis's heuristic: keep going only while more than a quarter of the sample had expired.
        if (sampled == 0 || expired * 4 <= sampled ||
            std::chrono::steady_clock::now() >= deadline) {
            return removed;
        }
    }
}

std::size_t Store::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

std::size_t Store::sweep_sample(TimePoint current) {
    std::size_t expired = 0;
    for (std::size_t i = 0; i < kSweepSampleSize && !volatile_.empty(); ++i) {
        std::uniform_int_distribution<std::size_t> pick(0, volatile_.size() - 1);
        Entry* entry = volatile_[pick(random_)];
        if (is_expired(*entry, current)) {
            log_and_erase(*entry);
            ++expired;
        }
    }
    return expired;
}

Store::Entry* Store::find_live(const std::string& key, TimePoint current) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return nullptr;
    }
    if (is_expired(it->second, current)) {
        log_and_erase(it->second);
        return nullptr;
    }
    return &it->second;
}

Store::Entry& Store::insert(const std::string& key) {
    auto [it, inserted] = entries_.try_emplace(key);
    Entry& entry = it->second;
    entry.key = &it->first;
    lru_.push_front(&entry);
    return entry;
}

void Store::erase(Entry& entry) {
    lru_.remove(&entry);
    clear_deadline(entry);
    entries_.erase(entries_.find(*entry.key));
}

// Expirations and evictions are logged as DELs, so a replay never depends on clock timing or on
// the read pattern that shaped the LRU order.
void Store::log_and_erase(Entry& entry) {
    log({"DEL", *entry.key});
    erase(entry);
}

void Store::log(const Command& command) {
    if (log_ != nullptr) {
        log_->append(command);
    }
}

void Store::evict_least_recent() {
    if (Entry* victim = lru_.least_recent()) {
        log_and_erase(*victim);
    }
}

void Store::set_deadline(Entry& entry, TimePoint deadline) {
    entry.expires_at = deadline;
    entry.volatile_index = volatile_.size();
    volatile_.push_back(&entry);
}

void Store::clear_deadline(Entry& entry) {
    if (!entry.expires_at) {
        return;
    }
    // Swap-remove keeps removal O(1); the moved entry learns its new position.
    Entry* last = volatile_.back();
    volatile_[entry.volatile_index] = last;
    last->volatile_index = entry.volatile_index;
    volatile_.pop_back();
    entry.expires_at.reset();
}

}  // namespace miniredis
