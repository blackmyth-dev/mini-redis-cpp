#include "gateway/core/key_value_store.hpp"

#include <mutex>

namespace gateway::core {

bool KeyValueStore::expired(const Entry& entry, const std::chrono::steady_clock::time_point now) {
    return entry.expires_at && *entry.expires_at <= now;
}

void KeyValueStore::set(std::string key, std::string value) {
    std::unique_lock lock(mutex_);
    entries_.insert_or_assign(std::move(key), Entry{std::move(value), std::nullopt});
}

std::optional<std::string> KeyValueStore::get(const std::string_view key) {
    const auto now = std::chrono::steady_clock::now();
    {
        std::shared_lock lock(mutex_);
        const auto it = entries_.find(std::string(key));
        if (it == entries_.end()) return std::nullopt;
        if (!expired(it->second, now)) return it->second.value;
    }
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(std::string(key));
    if (it != entries_.end() && expired(it->second, now)) entries_.erase(it);
    return std::nullopt;
}

bool KeyValueStore::erase(const std::string_view key) {
    std::unique_lock lock(mutex_);
    return entries_.erase(std::string(key)) != 0;
}

bool KeyValueStore::exists(const std::string_view key) { return get(key).has_value(); }

bool KeyValueStore::expire(const std::string_view key, const std::chrono::seconds ttl) {
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(std::string(key));
    if (it == entries_.end() || expired(it->second, std::chrono::steady_clock::now())) {
        if (it != entries_.end()) entries_.erase(it);
        return false;
    }
    it->second.expires_at = std::chrono::steady_clock::now() + ttl;
    return true;
}

std::optional<std::chrono::seconds> KeyValueStore::ttl(const std::string_view key) {
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(std::string(key));
    if (it == entries_.end()) return std::nullopt;
    const auto now = std::chrono::steady_clock::now();
    if (expired(it->second, now)) {
        entries_.erase(it);
        return std::nullopt;
    }
    if (!it->second.expires_at) return std::chrono::seconds{-1};
    return std::chrono::duration_cast<std::chrono::seconds>(*it->second.expires_at - now);
}

std::size_t KeyValueStore::purge_expired() {
    std::unique_lock lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    return std::erase_if(entries_, [now](const auto& item) { return expired(item.second, now); });
}

std::vector<SnapshotEntry> KeyValueStore::snapshot() {
    std::unique_lock lock(mutex_);
    const auto steady_now = std::chrono::steady_clock::now();
    const auto system_now = std::chrono::system_clock::now();
    std::vector<SnapshotEntry> result;
    result.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        if (expired(entry, steady_now)) continue;
        std::optional<std::chrono::system_clock::time_point> system_expiry;
        if (entry.expires_at) system_expiry = system_now + (*entry.expires_at - steady_now);
        result.push_back({key, entry.value, system_expiry});
    }
    return result;
}

void KeyValueStore::restore(std::vector<SnapshotEntry> entries) {
    std::unique_lock lock(mutex_);
    entries_.clear();
    const auto system_now = std::chrono::system_clock::now();
    const auto steady_now = std::chrono::steady_clock::now();
    for (auto& entry : entries) {
        std::optional<std::chrono::steady_clock::time_point> steady_expiry;
        if (entry.expires_at) {
            if (*entry.expires_at <= system_now) continue;
            steady_expiry = steady_now + (*entry.expires_at - system_now);
        }
        entries_.emplace(std::move(entry.key), Entry{std::move(entry.value), steady_expiry});
    }
}

}  // namespace gateway::core

