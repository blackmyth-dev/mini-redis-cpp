#include "gateway/http/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <bits/basic_string.h>

namespace gateway::http {

HttpServer::HttpServer(const std::uint16_t port, const std::size_t worker_count,
                       core::KeyValueStore& store)
    : port_(port), store_(store), workers_(worker_count) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::run() {
    if (stopping_.load()) return;
    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) throw std::runtime_error("HTTP socket: " + std::string(std::strerror(errno)));
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
        throw std::runtime_error("HTTP bind/listen: " + message);
    }
    std::cout << "HTTP server listening on 0.0.0.0:" << port_ << '\n';
    while (!stopping_.load()) {
        const int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (stopping_.load()) break;
            if (errno == EINTR) continue;
            throw std::runtime_error("HTTP accept: " + std::string(std::strerror(errno)));
        }
        if (stopping_.load()) { ::close(client_fd); break; }
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

void HttpServer::stop() noexcept {
    stopping_.store(true);
    const int fd = listen_fd_.exchange(-1);
    if (fd >= 0) { ::shutdown(fd, SHUT_RDWR); ::close(fd); }
    std::lock_guard lock(clients_mutex_);
    for (const int client_fd : client_fds_) ::shutdown(client_fd, SHUT_RDWR);
}

void HttpServer::handle_client(const int client_fd) {
    std::string pending;
    std::array<char, 8192> buffer{};
    bool keep_open = true;
    while (!stopping_.load() && keep_open) {
        const auto received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) break;
        pending.append(buffer.data(), static_cast<std::size_t>(received));
        while (!pending.empty()) {
            const auto result = parser_.parse(pending);
            if (std::holds_alternative<Incomplete>(result)) break;
            if (const auto* error = std::get_if<ParseError>(&result)) {
                send_all(client_fd, serialize({400, "Bad Request", error->message + "\n"}, false));
                keep_open = false;
                break;
            }
            auto parsed = std::get<ParsedRequest>(result);
            pending.erase(0, parsed.consumed);
            keep_open = parsed.request.keep_alive;
            if (!send_all(client_fd, serialize(route(parsed.request), keep_open))) {
                keep_open = false;
                break;
            }
        }
        if (pending.size() > HttpParser::max_header_size + HttpParser::max_body_size) {
            send_all(client_fd, serialize({413, "Payload Too Large", "request too large\n"}, false));
            break;
        }
    }
    {
        std::lock_guard lock(clients_mutex_);
        client_fds_.erase(client_fd);
        ::close(client_fd);
    }
}

HttpServer::Response HttpServer::route(const Request& request) {
    if (request.method == "GET" && request.target == "/health") return {200, "OK", "ok\n"};
    constexpr std::string_view prefix = "/kv/";
    if (!request.target.starts_with(prefix)) return {404, "Not Found", "not found\n"};
    const std::string key = request.target.substr(prefix.size());
    if (key.empty() || key.find('/') != std::string::npos || key.find('?') != std::string::npos)
        return {400, "Bad Request", "invalid key\n"};
    if (request.method == "GET") {
        const auto value = store_.get(key);
        return value ? Response{200, "OK", *value} : Response{404, "Not Found", "key not found\n"};
    }
    if (request.method == "PUT") {
        store_.set(key, request.body);
        return {204, "No Content", ""};
    }
    if (request.method == "DELETE") {
        return store_.erase(key) ? Response{204, "No Content", ""}
                                 : Response{404, "Not Found", "key not found\n"};
    }
    return {405, "Method Not Allowed", "method not allowed\n"};
}

std::string HttpServer::serialize(const Response& response, const bool keep_alive) {
    return "HTTP/1.1 " + std::to_string(response.status) + " " + response.reason + "\r\n" +
           "Content-Type: " + response.content_type + "\r\n" +
           "Content-Length: " + std::to_string(response.body.size()) + "\r\n" +
           "Connection: " + (keep_alive ? "keep-alive" : "close") + "\r\n\r\n" + response.body;
}

bool HttpServer::send_all(const int fd, const std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto count = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

}  // namespace gateway::http
