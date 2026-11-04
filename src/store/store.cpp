#include "store/store.h"

namespace miniredis {

void Store::set(const std::string& key, std::string value) {
    std::lock_guard lock(mutex_);
    data_.insert_or_assign(key, std::move(value));
}

std::optional<std::string> Store::get(const std::string& key) const {
    std::lock_guard lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t Store::size() const {
    std::lock_guard lock(mutex_);
    return data_.size();
}

}  // namespace miniredis
