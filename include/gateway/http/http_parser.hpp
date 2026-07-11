#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace gateway::http {

struct Request {
    std::string method;
    std::string target;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool keep_alive{true};
};

struct ParsedRequest { Request request; std::size_t consumed{}; };
struct Incomplete {};
struct ParseError { std::string message; };
using ParseResult = std::variant<ParsedRequest, Incomplete, ParseError>;

class HttpParser {
public:
    static constexpr std::size_t max_header_size = 16 * 1024;
    static constexpr std::size_t max_body_size = 1024 * 1024;
    ParseResult parse(std::string_view bytes) const;
};

}  // namespace gateway::http

