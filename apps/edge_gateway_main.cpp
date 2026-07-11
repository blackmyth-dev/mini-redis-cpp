#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "gateway/core/key_value_store.hpp"
#include "gateway/http/http_server.hpp"
#include "gateway/redis/tcp_server.hpp"
#include "gateway/storage/snapshot_store.hpp"

namespace {
std::atomic<bool> interrupted{false};
void on_signal(int) { interrupted.store(true); }
}

int main(int argc, char** argv) {
    std::uint16_t port = 6379;
    std::uint16_t http_port = 8080;
    std::size_t threads = std::max(2U, std::thread::hardware_concurrency());
    std::filesystem::path db = "gateway.db";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        else if (argument == "--http-port" && i + 1 < argc) http_port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
        else if (argument == "--threads" && i + 1 < argc) threads = std::stoul(argv[++i]);
        else if (argument == "--db" && i + 1 < argc) db = argv[++i];
        else {
            std::cerr << "Usage: edge_gateway [--port N] [--http-port N] [--threads N] [--db PATH]\n";
            return 2;
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    try {
        gateway::core::KeyValueStore store;
        gateway::storage::SnapshotStore snapshot(db);
        snapshot.load(store);
        gateway::redis::TcpServer redis_server(port, threads, store, snapshot);
        gateway::http::HttpServer http_server(http_port, threads, store);
        std::exception_ptr redis_error;
        std::exception_ptr http_error;
        std::thread redis_thread([&] {
            try { redis_server.run(); }
            catch (...) { redis_error = std::current_exception(); interrupted.store(true); http_server.stop(); }
        });
        std::thread http_thread([&] {
            try { http_server.run(); }
            catch (...) { http_error = std::current_exception(); interrupted.store(true); redis_server.stop(); }
        });
        while (!interrupted.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{250});
            store.purge_expired();
        }
        redis_server.stop();
        http_server.stop();
        redis_thread.join();
        http_thread.join();
        snapshot.save(store);
        if (redis_error) std::rethrow_exception(redis_error);
        if (http_error) std::rethrow_exception(http_error);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
