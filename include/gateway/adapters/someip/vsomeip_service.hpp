#pragma once

#include <vsomeip/vsomeip.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gateway/domain/state_event_bus.hpp"
#include "gateway/domain/state_store.hpp"

namespace gateway::adapters::someip {

struct SomeIpIds {
    static constexpr vsomeip::service_t service = 0x1234;
    static constexpr vsomeip::instance_t instance = 0x5678;
    static constexpr vsomeip::method_t get_method = 0x0001;
    static constexpr vsomeip::method_t set_method = 0x0002;
    static constexpr vsomeip::event_t state_event = 0x8001;
    static constexpr vsomeip::eventgroup_t state_eventgroup = 0x0001;
};

class VSomeIpService {
public:
    VSomeIpService(std::string application_name, domain::StateStore& store,
                   domain::StateEventBus& events);
    ~VSomeIpService();
    VSomeIpService(const VSomeIpService&) = delete;
    VSomeIpService& operator=(const VSomeIpService&) = delete;

    void run();
    void stop() noexcept;

private:
    void on_state(vsomeip::state_type_e state);
    void on_get(const std::shared_ptr<vsomeip::message>& request);
    void on_set(const std::shared_ptr<vsomeip::message>& request);
    void send_response(const std::shared_ptr<vsomeip::message>& request,
                       std::vector<std::uint8_t> data,
                       vsomeip::return_code_e return_code);
    void notify_change(const domain::StateChanged& event);

    std::string application_name_;
    domain::StateStore& store_;
    domain::StateEventBus& events_;
    std::shared_ptr<vsomeip::application> application_;
    std::optional<domain::StateEventBus::Subscription> subscription_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> offered_{false};
    std::atomic<bool> stopping_{false};
};

}  // namespace gateway::adapters::someip
