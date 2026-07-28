#include "gateway/domain/state_event_bus.hpp"

#include <utility>
#include <vector>

namespace gateway::domain {

StateEventBus::Subscription::Subscription(Subscription&& other) noexcept
    : bus_(std::exchange(other.bus_, nullptr)), id_(other.id_) {}

StateEventBus::Subscription& StateEventBus::Subscription::operator=(
    Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        bus_ = std::exchange(other.bus_, nullptr);
        id_ = other.id_;
    }
    return *this;
}

void StateEventBus::Subscription::reset() noexcept {
    if (bus_) {
        bus_->unsubscribe(id_);
        bus_ = nullptr;
    }
}

StateEventBus::Subscription StateEventBus::subscribe(Handler handler) {
    std::lock_guard lock(mutex_);
    const auto id = next_id_++;
    handlers_.emplace(id, std::move(handler));
    return Subscription{*this, id};
}

void StateEventBus::publish(const StateChanged& event) {
    std::vector<Handler> handlers;
    {
        std::lock_guard lock(mutex_);
        handlers.reserve(handlers_.size());
        for (const auto& [id, handler] : handlers_) {
            (void)id;
            handlers.push_back(handler);
        }
    }
    for (const auto& handler : handlers) handler(event);
}

void StateEventBus::unsubscribe(const std::size_t id) noexcept {
    std::lock_guard lock(mutex_);
    handlers_.erase(id);
}

}  // namespace gateway::domain

