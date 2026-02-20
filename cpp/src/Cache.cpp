#include "../include/Cache.hpp"

Cache::Cache() = default;

Cache::Cache(const Cache& other) : entries_(other.entries_) {}

Cache& Cache::operator=(const Cache& other) {
    if (this != &other) {
        entries_ = other.entries_;
    }
    return *this;
}

Cache::~Cache() { entries_.clear(); }

void Cache::put(const std::string& key, const std::string& value,
                int ttlSeconds) {
    Entry entry;
    entry.value = value;
    entry.expiresAt =
        std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
    entries_[key] = entry;
}

bool Cache::get(const std::string& key, std::string* outValue) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return false;
    }
    if (std::chrono::steady_clock::now() > it->second.expiresAt) {
        entries_.erase(it);
        return false;
    }
    if (outValue) {
        *outValue = it->second.value;
    }
    return true;
}

void Cache::purgeExpired() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now > it->second.expiresAt) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t Cache::size() const { return entries_.size(); }