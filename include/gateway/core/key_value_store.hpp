#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gateway::core {

struct SnapshotEntry {
    std::string key;
    std::string value;
    std::optional<std::chrono::system_clock::time_point> expires_at;
};

class KeyValueStore {
public:
    void set(std::string key, std::string value);
    std::optional<std::string> get(std::string_view key);
    bool erase(std::string_view key);
    bool exists(std::string_view key);
    bool expire(std::string_view key, std::chrono::seconds ttl);
    std::optional<std::chrono::seconds> ttl(std::string_view key);
    std::size_t purge_expired();
    std::vector<SnapshotEntry> snapshot();
    void restore(std::vector<SnapshotEntry> entries);

private:
    struct Entry {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
    };
    static bool expired(const Entry& entry, std::chrono::steady_clock::time_point now);
    std::unordered_map<std::string, Entry> entries_;
    mutable std::shared_mutex mutex_;
};

}  // namespace gateway::core

