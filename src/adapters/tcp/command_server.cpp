#include "gateway/adapters/tcp/command_server.hpp"

#include <chrono>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gateway::adapters::tcp {
namespace {

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket socket, domain::StateStore& store)
        : socket_(std::move(socket)), store_(store) {}

    void start() { read_command(); }

private:
    void read_command() {
        boost::asio::async_read_until(
            socket_, buffer_, '\n',
            [self = shared_from_this()](const boost::system::error_code& error,
                                        std::size_t) {
                if (error) return;
                std::istream input(&self->buffer_);
                std::string line;
                std::getline(input, line);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto [response, close] = self->execute(line);
                self->write(response, close);
            });
    }

    std::pair<std::string, bool> execute(const std::string& line) {
        std::istringstream input(line);
        std::vector<std::string> words;
        for (std::string word; input >> word;) words.push_back(std::move(word));
        if (words.empty()) return {"-ERR empty command\r\n", false};
        if (words[0] == "SET" && words.size() == 3) {
            store_.set(words[1], words[2], domain::ChangeOrigin::tcp);
            return {"+OK\r\n", false};
        }
        if (words[0] == "GET" && words.size() == 2) {
            const auto value = store_.get(words[1]);
            return {value ? "$" + std::to_string(value->size()) + "\r\n" +
                                *value + "\r\n"
                          : "$-1\r\n",
                    false};
        }
        if (words[0] == "DEL" && words.size() == 2)
            return {":" + std::to_string(
                              store_.erase(words[1],
                                           domain::ChangeOrigin::tcp)) +
                        "\r\n",
                    false};
        if (words[0] == "EXPIRE" && words.size() == 3) {
            try {
                const auto seconds = std::stoll(words[2]);
                if (seconds < 0) throw std::invalid_argument("negative");
                return {":" + std::to_string(store_.expire(
                                  words[1], std::chrono::seconds{seconds})) +
                            "\r\n",
                        false};
            } catch (...) {
                return {"-ERR invalid expiry\r\n", false};
            }
        }
        if (words[0] == "QUIT" && words.size() == 1)
            return {"+BYE\r\n", true};
        return {"-ERR unsupported command or arguments\r\n", false};
    }

    void write(std::string response, const bool close) {
        auto bytes = std::make_shared<std::string>(std::move(response));
        boost::asio::async_write(
            socket_, boost::asio::buffer(*bytes),
            [self = shared_from_this(), bytes, close](
                const boost::system::error_code& error, std::size_t) {
                if (!error && !close) self->read_command();
            });
    }

    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf buffer_;
    domain::StateStore& store_;
};

}  // namespace

CommandServer::CommandServer(boost::asio::io_context& io,
                             const std::uint16_t port,
                             domain::StateStore& store)
    : acceptor_(io, {boost::asio::ip::tcp::v4(), port}), store_(store) {}

void CommandServer::start() { accept_next(); }

void CommandServer::stop() {
    boost::system::error_code ignored;
    acceptor_.close(ignored);
}

void CommandServer::accept_next() {
    acceptor_.async_accept(
        [this](const boost::system::error_code& error,
               boost::asio::ip::tcp::socket socket) {
            if (!error) std::make_shared<Session>(std::move(socket), store_)->start();
            if (acceptor_.is_open()) accept_next();
        });
}

}  // namespace gateway::adapters::tcp
