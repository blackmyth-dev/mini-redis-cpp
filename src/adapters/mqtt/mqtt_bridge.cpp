#include "gateway/adapters/mqtt/mqtt_bridge.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace gateway::adapters::mqtt {

MqttBridge::MqttBridge(MqttConfig config, domain::StateStore& store,
                       domain::StateEventBus& events)
    : config_(std::move(config)),
      store_(store),
      events_(events),
      client_(config_.broker_uri, config_.client_id) {
    client_.set_callback(*this);
}

MqttBridge::~MqttBridge() { stop(); }

void MqttBridge::start() {
    stopping_.store(false);
    subscription_.emplace(events_.subscribe(
        [this](const domain::StateChanged& event) {
            publish_change(event);
        }));
    const auto options = ::mqtt::connect_options_builder()
                             .clean_session(true)
                             .automatic_reconnect(true)
                             .keep_alive_interval(std::chrono::seconds{20})
                             .finalize();
    client_.connect(options)->wait();
    client_.subscribe(config_.command_prefix + "#", config_.qos)->wait();
}

void MqttBridge::stop() noexcept {
    if (stopping_.exchange(true)) return;
    subscription_.reset();
    try {
        if (client_.is_connected()) client_.disconnect()->wait();
    } catch (const ::mqtt::exception& error) {
        std::cerr << "MQTT disconnect failed: " << error.what() << '\n';
    }
}

void MqttBridge::connected(const std::string&) {
    if (stopping_.load()) return;
    try {
        client_.subscribe(config_.command_prefix + "#", config_.qos);
    } catch (const ::mqtt::exception& error) {
        std::cerr << "MQTT resubscribe failed: " << error.what() << '\n';
    }
}

void MqttBridge::connection_lost(const std::string& cause) {
    if (!stopping_.load())
        std::cerr << "MQTT connection lost: " << cause << '\n';
}

void MqttBridge::message_arrived(::mqtt::const_message_ptr message) {
    const auto& topic = message->get_topic();
    if (!topic.starts_with(config_.command_prefix)) return;
    const auto key = topic.substr(config_.command_prefix.size());
    if (key.empty()) return;
    store_.set(key, message->get_payload_str(), domain::ChangeOrigin::mqtt);
}

void MqttBridge::publish_change(const domain::StateChanged& event) {
    if (stopping_.load() || !client_.is_connected()) return;
    auto message = ::mqtt::make_message(config_.state_prefix + event.key,
                                        event.value.value_or(""));
    message->set_qos(config_.qos);
    message->set_retained(true);
    try {
        client_.publish(message);
    } catch (const ::mqtt::exception& error) {
        std::cerr << "MQTT publish failed: " << error.what() << '\n';
    }
}

}  // namespace gateway::adapters::mqtt
