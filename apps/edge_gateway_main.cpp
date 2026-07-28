#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include "gateway/adapters/http/http_server.hpp"
#include "gateway/adapters/mqtt/mqtt_bridge.hpp"
#include "gateway/adapters/someip/vsomeip_service.hpp"
#include "gateway/adapters/tcp/command_server.hpp"
#include "gateway/domain/state_event_bus.hpp"
#include "gateway/domain/state_store.hpp"
#include "gateway/infrastructure/file_snapshot_repository.hpp"

namespace {
std::atomic<bool> interrupted{false};
void on_signal(int) { interrupted.store(true); }
}

int main(int argc, char** argv) {
    std::uint16_t tcp_port = 6379;
    std::uint16_t http_port = 8080;
    std::size_t io_threads =
        std::max(2U, std::thread::hardware_concurrency());
    std::filesystem::path database = "gateway.db";
    gateway::adapters::mqtt::MqttConfig mqtt;
    std::string vsomeip_application = "edge-gateway";

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--tcp-port" && i + 1 < argc)
            tcp_port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        else if (argument == "--http-port" && i + 1 < argc)
            http_port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        else if (argument == "--mqtt-uri" && i + 1 < argc)
            mqtt.broker_uri = argv[++i];
        else if (argument == "--vsomeip-app" && i + 1 < argc)
            vsomeip_application = argv[++i];
        else if (argument == "--threads" && i + 1 < argc)
            io_threads = std::stoul(argv[++i]);
        else if (argument == "--db" && i + 1 < argc)
            database = argv[++i];
        else {
            std::cerr
                << "Usage: edge_gateway [--tcp-port N] [--http-port N] "
                   "[--mqtt-uri URI] [--vsomeip-app NAME] [--threads N] "
                   "[--db PATH]\n";
            return 2;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        gateway::domain::StateEventBus events;
        gateway::domain::StateStore store(events);
        gateway::infrastructure::FileSnapshotRepository snapshots(database);
        snapshots.load(store);

        boost::asio::io_context io;
        auto work = boost::asio::make_work_guard(io);
        gateway::adapters::tcp::CommandServer tcp(io, tcp_port, store);
        gateway::adapters::http::HttpServer http(io, http_port, store);
        gateway::adapters::mqtt::MqttBridge mqtt_bridge(mqtt, store, events);
        gateway::adapters::someip::VSomeIpService someip(
            vsomeip_application, store, events);

        tcp.start();
        http.start();
        mqtt_bridge.start();

        std::exception_ptr someip_error;
        std::thread someip_thread([&] {
            try {
                someip.run();
            } catch (...) {
                someip_error = std::current_exception();
                interrupted.store(true);
            }
        });

        std::vector<std::thread> workers;
        workers.reserve(io_threads);
        for (std::size_t i = 0; i < io_threads; ++i)
            workers.emplace_back([&] { io.run(); });

        while (!interrupted.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{250});
            store.purge_expired();
        }

        someip.stop();
        mqtt_bridge.stop();
        http.stop();
        tcp.stop();
        work.reset();
        io.stop();
        for (auto& worker : workers) worker.join();
        someip_thread.join();
        snapshots.save(store);
        if (someip_error) std::rethrow_exception(someip_error);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
