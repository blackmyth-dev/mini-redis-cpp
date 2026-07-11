#include "gateway/redis/tcp_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace gateway::redis {

TcpServer::TcpServer(const std::uint16_t port, const std::size_t worker_count,
                     core::KeyValueStore& store, storage::SnapshotStore& snapshot)
    : port_(port), store_(store), snapshot_(snapshot), workers_(worker_count) {}

TcpServer::~TcpServer() { stop(); }

void TcpServer::run() {
    if (stopping_.load()) return;
    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
    listen_fd_.store(server_fd);
    int reuse = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(server_fd, SOMAXCONN) < 0) {
        const auto message = std::string(std::strerror(errno));
        stop();
        throw std::runtime_error("bind/listen: " + message);
    }
    std::cout << "Redis-like server listening on 0.0.0.0:" << port_ << '\n';
    while (!stopping_.load()) {
        const int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (stopping_.load()) break;
            if (errno == EINTR) continue;
            throw std::runtime_error("accept: " + std::string(std::strerror(errno)));
        }
        if (stopping_.load()) {
            ::close(client_fd);
            break;
        }
        {
            std::lock_guard lock(clients_mutex_);
            client_fds_.insert(client_fd);
        }
        try {
            workers_.submit([this, client_fd] { handle_client(client_fd); });
        } catch (...) {
            std::lock_guard lock(clients_mutex_);
            client_fds_.erase(client_fd);
            ::close(client_fd);
            throw;
        }
    }
}

void TcpServer::stop() noexcept {
    stopping_.store(true);
    const int fd = listen_fd_.exchange(-1);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
    std::lock_guard lock(clients_mutex_);
    for (const int client_fd : client_fds_) ::shutdown(client_fd, SHUT_RDWR);
}

void TcpServer::handle_client(const int client_fd) {
    std::string pending;
    std::array<char, 4096> buffer{};
    bool quit = false;
    while (!stopping_.load() && !quit) {
        const auto received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) break;
        pending.append(buffer.data(), static_cast<std::size_t>(received));
        if (pending.size() > 64 * 1024) {
            send_all(client_fd, "-ERR request buffer too large\r\n");
            break;
        }
        for (std::size_t newline; (newline = pending.find('\n')) != std::string::npos;) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto result = parser_.parse(line);
            std::string response;
            if (const auto* error = std::get_if<ParseError>(&result))
                response = "-ERR " + error->message + "\r\n";
            else
                response = execute(std::get<Command>(result), quit);
            if (!send_all(client_fd, response)) quit = true;
        }
    }
    {
        std::lock_guard lock(clients_mutex_);
        client_fds_.erase(client_fd);
        ::close(client_fd);
    }
}

std::string TcpServer::execute(const Command& command, bool& should_quit) {
    return std::visit([this, &should_quit](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Set>) {
            store_.set(value.key, value.value);
            return "+OK\r\n";
        } else if constexpr (std::is_same_v<T, Get>) {
            const auto found = store_.get(value.key);
            return found ? "$" + std::to_string(found->size()) + "\r\n" + *found + "\r\n" : "$-1\r\n";
        } else if constexpr (std::is_same_v<T, Del>) {
            return ":" + std::to_string(store_.erase(value.key)) + "\r\n";
        } else if constexpr (std::is_same_v<T, Exists>) {
            return ":" + std::to_string(store_.exists(value.key)) + "\r\n";
        } else if constexpr (std::is_same_v<T, Expire>) {
            return ":" + std::to_string(store_.expire(value.key, std::chrono::seconds{value.seconds})) + "\r\n";
        } else if constexpr (std::is_same_v<T, Ttl>) {
            const auto result = store_.ttl(value.key);
            return ":" + std::to_string(result ? result->count() : -2) + "\r\n";
        } else if constexpr (std::is_same_v<T, Save>) {
            try { snapshot_.save(store_); return "+OK\r\n"; }
            catch (const std::exception& error) { return "-ERR " + std::string(error.what()) + "\r\n"; }
        } else {
            should_quit = true;
            return "+BYE\r\n";
        }
    }, command);
}

bool TcpServer::send_all(const int fd, const std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto count = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

}  // namespace gateway::redis
