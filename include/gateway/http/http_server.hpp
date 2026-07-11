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
#include "gateway/http/http_parser.hpp"

namespace gateway::http {

class HttpServer {
public:
    HttpServer(std::uint16_t port, std::size_t workers, core::KeyValueStore& store);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    void run();
    void stop() noexcept;

private:
    struct Response {
        int status;
        std::string reason;
        std::string body;
        std::string content_type{"text/plain; charset=utf-8"};
    };
    void handle_client(int client_fd);
    Response route(const Request& request);
    static std::string serialize(const Response& response, bool keep_alive);
    static bool send_all(int fd, std::string_view data);

    std::uint16_t port_;
    core::KeyValueStore& store_;
    core::ThreadPool workers_;
    HttpParser parser_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> listen_fd_{-1};
    std::mutex clients_mutex_;
    std::unordered_set<int> client_fds_;
};

}  // namespace gateway::http

