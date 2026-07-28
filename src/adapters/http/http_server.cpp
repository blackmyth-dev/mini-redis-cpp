#include "gateway/adapters/http/http_server.hpp"

#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include <memory>
#include <string>
#include <utility>

namespace gateway::adapters::http {
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

namespace {

http::response<http::string_body> route(
    const http::request<http::string_body>& request,
    domain::StateStore& store) {
    http::response<http::string_body> response;
    response.version(request.version());
    response.keep_alive(request.keep_alive());
    response.set(http::field::server, "cpp-edge-gateway");
    response.set(http::field::content_type, "text/plain; charset=utf-8");
    const std::string target(request.target().data(), request.target().size());

    if (request.method() == http::verb::get && target == "/health") {
        response.result(http::status::ok);
        response.body() = "ok\n";
    } else if (request.method() == http::verb::get &&
               target == "/metrics") {
        response.result(http::status::ok);
        response.set(http::field::content_type,
                     "text/plain; version=0.0.4");
        response.body() =
            "gateway_keys " + std::to_string(store.size()) + "\n";
    } else if (target.starts_with("/kv/") && target.size() > 4) {
        const auto key = target.substr(4);
        if (request.method() == http::verb::get) {
            const auto value = store.get(key);
            response.result(value ? http::status::ok
                                  : http::status::not_found);
            response.body() = value.value_or("key not found\n");
        } else if (request.method() == http::verb::put) {
            store.set(key, request.body(), domain::ChangeOrigin::http);
            response.result(http::status::no_content);
        } else if (request.method() == http::verb::delete_) {
            response.result(
                store.erase(key, domain::ChangeOrigin::http)
                    ? http::status::no_content
                    : http::status::not_found);
            if (response.result() == http::status::not_found)
                response.body() = "key not found\n";
        } else {
            response.result(http::status::method_not_allowed);
            response.set(http::field::allow, "GET, PUT, DELETE");
        }
    } else {
        response.result(http::status::not_found);
        response.body() = "not found\n";
    }
    response.prepare_payload();
    return response;
}

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, domain::StateStore& store)
        : stream_(std::move(socket)), store_(store) {}

    void start() { read(); }

private:
    void read() {
        request_ = {};
        http::async_read(
            stream_, buffer_, request_,
            [self = shared_from_this()](beast::error_code error, std::size_t) {
                if (error == http::error::end_of_stream) {
                    self->close();
                    return;
                }
                if (error) return;
                self->write(route(self->request_, self->store_));
            });
    }

    void write(http::response<http::string_body> response) {
        const bool close = response.need_eof();
        auto outgoing =
            std::make_shared<http::response<http::string_body>>(
                std::move(response));
        http::async_write(
            stream_, *outgoing,
            [self = shared_from_this(), outgoing, close](
                beast::error_code error, std::size_t) {
                if (error) return;
                if (close) self->close();
                else self->read();
            });
    }

    void close() {
        beast::error_code ignored;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
    }

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    domain::StateStore& store_;
};

}  // namespace

HttpServer::HttpServer(boost::asio::io_context& io, const std::uint16_t port,
                       domain::StateStore& store)
    : acceptor_(io, {tcp::v4(), port}), store_(store) {}

void HttpServer::start() { accept_next(); }

void HttpServer::stop() {
    beast::error_code ignored;
    acceptor_.close(ignored);
}

void HttpServer::accept_next() {
    acceptor_.async_accept(
        [this](beast::error_code error, tcp::socket socket) {
            if (!error) std::make_shared<Session>(std::move(socket), store_)->start();
            if (acceptor_.is_open()) accept_next();
        });
}

}  // namespace gateway::adapters::http
