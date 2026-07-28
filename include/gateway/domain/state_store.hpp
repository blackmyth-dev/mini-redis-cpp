#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gateway/domain/state_event.hpp"
#include "gateway/domain/state_event_bus.hpp"

namespace gateway::domain {

struct StateRecord {
    std::string key;
    std::string value;
    std::optional<std::chrono::system_clock::time_point> expires_at;
};

class StateStore {
public:
    explicit StateStore(StateEventBus& events) : events_(events) {}

    void set(std::string key, std::string value,
             ChangeOrigin origin = ChangeOrigin::internal);
    std::optional<std::string> get(std::string_view key);
    bool erase(std::string_view key,
               ChangeOrigin origin = ChangeOrigin::internal);
    bool expire(std::string_view key, std::chrono::seconds ttl);
    std::size_t size();
    std::size_t purge_expired();
    std::vector<StateRecord> snapshot();
    void restore(std::vector<StateRecord> records);

private:
    struct Entry {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
    };
    static bool expired(const Entry& entry,
                        std::chrono::steady_clock::time_point now);

    StateEventBus& events_;
    std::unordered_map<std::string, Entry> entries_;
    mutable std::shared_mutex mutex_;
};

}  // namespace gateway::domain

