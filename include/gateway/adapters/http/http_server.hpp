#pragma once

#include <utility>
#include <boost/asio.hpp>

#include <cstdint>

#include "gateway/domain/state_store.hpp"

namespace gateway::adapters::http {

class HttpServer {
public:
    HttpServer(boost::asio::io_context& io, std::uint16_t port,
               domain::StateStore& store);
    void start();
    void stop();

private:
    void accept_next();

    boost::asio::ip::tcp::acceptor acceptor_;
    domain::StateStore& store_;
};

}  // namespace gateway::adapters::http
