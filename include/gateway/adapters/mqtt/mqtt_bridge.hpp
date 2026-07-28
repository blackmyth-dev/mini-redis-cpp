#pragma once

#include <mqtt/async_client.h>

#include <atomic>
#include <optional>
#include <string>

#include "gateway/domain/state_event_bus.hpp"
#include "gateway/domain/state_store.hpp"

namespace gateway::adapters::mqtt {

struct MqttConfig {
    std::string broker_uri{"tcp://127.0.0.1:1883"};
    std::string client_id{"cpp-edge-gateway"};
    std::string command_prefix{"gateway/command/"};
    std::string state_prefix{"gateway/state/"};
    int qos{1};
};

class MqttBridge : public ::mqtt::callback {
public:
    MqttBridge(MqttConfig config, domain::StateStore& store,
               domain::StateEventBus& events);
    ~MqttBridge() override;
    void start();
    void stop() noexcept;

private:
    void connected(const std::string& cause) override;
    void connection_lost(const std::string& cause) override;
    void message_arrived(::mqtt::const_message_ptr message) override;
    void publish_change(const domain::StateChanged& event);

    MqttConfig config_;
    domain::StateStore& store_;
    domain::StateEventBus& events_;
    ::mqtt::async_client client_;
    std::optional<domain::StateEventBus::Subscription> subscription_;
    std::atomic<bool> stopping_{false};
};

}  // namespace gateway::adapters::mqtt

