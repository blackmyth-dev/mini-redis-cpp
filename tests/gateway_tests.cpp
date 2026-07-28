#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gateway/application/state_wire_codec.hpp"
#include "gateway/domain/state_event_bus.hpp"
#include "gateway/domain/state_store.hpp"
#include "gateway/infrastructure/file_snapshot_repository.hpp"

namespace {
int failures = 0;
void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main() {
    using namespace std::chrono_literals;
    gateway::domain::StateEventBus events;
    std::vector<gateway::domain::StateChanged> changes;
    std::mutex changes_mutex;
    auto subscription = events.subscribe([&](const auto& event) {
        std::lock_guard lock(changes_mutex);
        changes.push_back(event);
    });
    gateway::domain::StateStore store(events);

    store.set("name", "Duy", gateway::domain::ChangeOrigin::http);
    check(store.get("name") == "Duy", "set/get");
    check(changes.size() == 1 && changes[0].key == "name" &&
              changes[0].origin == gateway::domain::ChangeOrigin::http,
          "domain event includes origin");
    check(store.erase("name", gateway::domain::ChangeOrigin::mqtt),
          "erase existing value");
    check(changes.size() == 2 && !changes[1].value,
          "erase publishes tombstone event");

    store.set("temporary", "value");
    check(store.expire("temporary", 0s), "set expiry");
    check(!store.get("temporary"), "lazy expiry");

    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([&store, worker] {
            for (int i = 0; i < 100; ++i) {
                const auto key = "worker-" + std::to_string(worker) + "-" +
                                 std::to_string(i);
                store.set(key, std::to_string(i));
            }
        });
    }
    for (auto& writer : writers) writer.join();
    check(store.get("worker-3-99") == "99", "concurrent writers");

    const auto key_value =
        gateway::application::StateWireCodec::encode_key_value("rpm", "2500");
    const auto decoded =
        gateway::application::StateWireCodec::decode_key_value(key_value);
    check(decoded && decoded->key == "rpm" && decoded->value == "2500",
          "deployment payload round trip");
    check(!gateway::application::StateWireCodec::decode_key_value(
              std::vector<std::uint8_t>{0x00, 0x04, 'r'}),
          "reject truncated deployment payload");

    const auto path =
        std::filesystem::temp_directory_path() / "gateway-v2-test.db";
    gateway::infrastructure::FileSnapshotRepository repository(path);
    store.set("persisted", "yes");
    repository.save(store);
    gateway::domain::StateEventBus restored_events;
    gateway::domain::StateStore restored(restored_events);
    repository.load(restored);
    check(restored.get("persisted") == "yes", "snapshot round trip");
    std::filesystem::remove(path);

    subscription.reset();
    const auto previous_count = changes.size();
    store.set("after-unsubscribe", "ignored");
    check(changes.size() == previous_count, "RAII event unsubscription");

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
