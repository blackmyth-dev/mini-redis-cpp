#include "gateway/adapters/someip/vsomeip_service.hpp"

#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gateway/application/state_wire_codec.hpp"

namespace gateway::adapters::someip {

VSomeIpService::VSomeIpService(std::string application_name,
                               domain::StateStore& store,
                               domain::StateEventBus& events)
    : application_name_(std::move(application_name)),
      store_(store),
      events_(events),
      application_(vsomeip::runtime::get()->create_application(
          application_name_)) {}

VSomeIpService::~VSomeIpService() { stop(); }

void VSomeIpService::run() {
    if (stopping_.load()) return;
    if (!application_->init())
        throw std::runtime_error("vsomeip application initialization failed");
    initialized_.store(true);
    if (stopping_.load()) return;
    application_->register_state_handler(
        [this](const vsomeip::state_type_e state) { on_state(state); });
    application_->register_message_handler(
        SomeIpIds::service, SomeIpIds::instance, SomeIpIds::get_method,
        [this](const auto& request) { on_get(request); });
    application_->register_message_handler(
        SomeIpIds::service, SomeIpIds::instance, SomeIpIds::set_method,
        [this](const auto& request) { on_set(request); });
    subscription_.emplace(events_.subscribe(
        [this](const domain::StateChanged& event) { notify_change(event); }));
    if (stopping_.load()) {
        subscription_.reset();
        application_->clear_all_handler();
        return;
    }
    application_->start();
}

void VSomeIpService::stop() noexcept {
    if (stopping_.exchange(true)) return;
    subscription_.reset();
    if (!initialized_.load()) return;
    if (offered_.exchange(false)) {
        application_->stop_offer_event(
            SomeIpIds::service, SomeIpIds::instance, SomeIpIds::state_event);
        application_->stop_offer_service(
            SomeIpIds::service, SomeIpIds::instance);
    }
    application_->clear_all_handler();
    application_->stop();
}

void VSomeIpService::on_state(const vsomeip::state_type_e state) {
    if (state != vsomeip::state_type_e::ST_REGISTERED ||
        stopping_.load() || offered_.exchange(true))
        return;
    const std::set<vsomeip::eventgroup_t> groups{
        SomeIpIds::state_eventgroup};
    application_->offer_event(
        SomeIpIds::service, SomeIpIds::instance, SomeIpIds::state_event,
        groups, vsomeip::event_type_e::ET_FIELD);
    application_->offer_service(
        SomeIpIds::service, SomeIpIds::instance, 1, 0);
}

void VSomeIpService::on_get(
    const std::shared_ptr<vsomeip::message>& request) {
    const auto payload = request->get_payload();
    const std::span<const std::uint8_t> bytes{
        payload->get_data(), static_cast<std::size_t>(payload->get_length())};
    const auto key = application::StateWireCodec::decode_key(bytes);
    if (!key) {
        send_response(request, {},
                      vsomeip::return_code_e::E_MALFORMED_MESSAGE);
        return;
    }
    const auto value = store_.get(*key);
    if (!value) {
        send_response(request, {}, vsomeip::return_code_e::E_NOT_OK);
        return;
    }
    send_response(request,
                  application::StateWireCodec::encode_value(*value),
                  vsomeip::return_code_e::E_OK);
}

void VSomeIpService::on_set(
    const std::shared_ptr<vsomeip::message>& request) {
    const auto payload = request->get_payload();
    const std::span<const std::uint8_t> bytes{
        payload->get_data(), static_cast<std::size_t>(payload->get_length())};
    const auto state = application::StateWireCodec::decode_key_value(bytes);
    if (!state || state->key.empty()) {
        send_response(request, {},
                      vsomeip::return_code_e::E_MALFORMED_MESSAGE);
        return;
    }
    store_.set(state->key, state->value, domain::ChangeOrigin::someip);
    send_response(request, {}, vsomeip::return_code_e::E_OK);
}

void VSomeIpService::send_response(
    const std::shared_ptr<vsomeip::message>& request,
    std::vector<std::uint8_t> data,
    const vsomeip::return_code_e return_code) {
    auto response = vsomeip::runtime::get()->create_response(request);
    response->set_return_code(return_code);
    auto payload = vsomeip::runtime::get()->create_payload();
    payload->set_data(std::move(data));
    response->set_payload(payload);
    application_->send(response);
}

void VSomeIpService::notify_change(const domain::StateChanged& event) {
    if (!offered_.load() || stopping_.load()) return;
    auto data = application::StateWireCodec::encode_key_value(
        event.key, event.value.value_or(""));
    auto payload = vsomeip::runtime::get()->create_payload();
    payload->set_data(std::move(data));
    application_->notify(SomeIpIds::service, SomeIpIds::instance,
                         SomeIpIds::state_event, payload);
}

}  // namespace gateway::adapters::someip
