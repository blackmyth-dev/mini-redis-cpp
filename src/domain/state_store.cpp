#include "gateway/domain/state_store.hpp"

#include <mutex>
#include <utility>

namespace gateway::domain {

bool StateStore::expired(const Entry& entry,
                         const std::chrono::steady_clock::time_point now) {
    return entry.expires_at && *entry.expires_at <= now;
}

void StateStore::set(std::string key, std::string value,
                     const ChangeOrigin origin) {
    std::string event_key = key;
    std::string event_value = value;
    {
        std::unique_lock lock(mutex_);
        entries_.insert_or_assign(std::move(key),
                                  Entry{std::move(value), std::nullopt});
    }
    events_.publish({std::move(event_key), std::move(event_value), origin});
}

std::optional<std::string> StateStore::get(const std::string_view key) {
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

bool StateStore::erase(const std::string_view key, const ChangeOrigin origin) {
    bool removed{};
    {
        std::unique_lock lock(mutex_);
        removed = entries_.erase(std::string(key)) != 0;
    }
    if (removed) events_.publish({std::string(key), std::nullopt, origin});
    return removed;
}

bool StateStore::expire(const std::string_view key,
                        const std::chrono::seconds ttl) {
    std::unique_lock lock(mutex_);
    const auto it = entries_.find(std::string(key));
    if (it == entries_.end()) return false;
    it->second.expires_at = std::chrono::steady_clock::now() + ttl;
    return true;
}

std::size_t StateStore::purge_expired() {
    std::unique_lock lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    return std::erase_if(entries_,
                         [now](const auto& item) {
                             return expired(item.second, now);
                         });
}

std::size_t StateStore::size() {
    purge_expired();
    std::shared_lock lock(mutex_);
    return entries_.size();
}

std::vector<StateRecord> StateStore::snapshot() {
    std::shared_lock lock(mutex_);
    const auto steady_now = std::chrono::steady_clock::now();
    const auto system_now = std::chrono::system_clock::now();
    std::vector<StateRecord> records;
    records.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        if (expired(entry, steady_now)) continue;
        std::optional<std::chrono::system_clock::time_point> expiry;
        if (entry.expires_at)
            expiry = system_now + (*entry.expires_at - steady_now);
        records.push_back({key, entry.value, expiry});
    }
    return records;
}

void StateStore::restore(std::vector<StateRecord> records) {
    std::unique_lock lock(mutex_);
    entries_.clear();
    const auto system_now = std::chrono::system_clock::now();
    const auto steady_now = std::chrono::steady_clock::now();
    for (auto& record : records) {
        std::optional<std::chrono::steady_clock::time_point> expiry;
        if (record.expires_at) {
            if (*record.expires_at <= system_now) continue;
            expiry = steady_now + (*record.expires_at - system_now);
        }
        entries_.emplace(std::move(record.key),
                         Entry{std::move(record.value), expiry});
    }
}

}  // namespace gateway::domain

