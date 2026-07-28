#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "gateway/domain/state_event.hpp"

namespace gateway::domain {

class StateEventBus {
public:
    using Handler = std::function<void(const StateChanged&)>;

    class Subscription {
    public:
        Subscription() = default;
        Subscription(StateEventBus& bus, std::size_t id) : bus_(&bus), id_(id) {}
        ~Subscription() { reset(); }
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        void reset() noexcept;

    private:
        StateEventBus* bus_{};
        std::size_t id_{};
    };

    Subscription subscribe(Handler handler);
    void publish(const StateChanged& event);

private:
    friend class Subscription;
    void unsubscribe(std::size_t id) noexcept;

    std::mutex mutex_;
    std::unordered_map<std::size_t, Handler> handlers_;
    std::size_t next_id_{1};
};

}  // namespace gateway::domain

