#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include "gateway/core/key_value_store.hpp"
#include "gateway/core/thread_pool.hpp"
#include "gateway/redis/command_parser.hpp"
#include "gateway/storage/snapshot_store.hpp"

namespace gateway::redis {

class TcpServer {
public:
    TcpServer(std::uint16_t port, std::size_t workers, core::KeyValueStore& store,
              storage::SnapshotStore& snapshot);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    void run();
    void stop() noexcept;
private:
    void handle_client(int client_fd);
    std::string execute(const Command& command, bool& should_quit);
    static bool send_all(int fd, std::string_view data);
    std::uint16_t port_;
    core::KeyValueStore& store_;
    storage::SnapshotStore& snapshot_;
    core::ThreadPool workers_;
    CommandParser parser_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> listen_fd_{-1};
    std::mutex clients_mutex_;
    std::unordered_set<int> client_fds_;
};

}  // namespace gateway::redis
