#include "gateway/http/http_parser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>

namespace gateway::http {
namespace {
std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return std::string(value.substr(first, last - first + 1));
}
std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}  // namespace

ParseResult HttpParser::parse(const std::string_view bytes) const {
    const auto header_end = bytes.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        if (bytes.size() > max_header_size) return ParseError{"headers too large"};
        return Incomplete{};
    }
    if (header_end + 4 > max_header_size) return ParseError{"headers too large"};
    const auto request_line_end = bytes.find("\r\n");
    if (request_line_end == std::string_view::npos || request_line_end > header_end)
        return ParseError{"missing request line"};

    Request request;
    std::string version;
    {
        std::istringstream line{std::string(bytes.substr(0, request_line_end))};
        std::string extra;
        if (!(line >> request.method >> request.target >> version) || (line >> extra))
            return ParseError{"invalid request line"};
    }
    if (version != "HTTP/1.1") return ParseError{"only HTTP/1.1 is supported"};
    if (request.target.empty() || request.target.front() != '/') return ParseError{"invalid request target"};

    std::size_t cursor = request_line_end + 2;
    while (cursor < header_end) {
        const auto line_end = bytes.find("\r\n", cursor);
        if (line_end == std::string_view::npos || line_end > header_end) return ParseError{"invalid header line"};
        const auto line = bytes.substr(cursor, line_end - cursor);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return ParseError{"invalid header line"};
        auto name = lowercase(trim(line.substr(0, colon)));
        auto value = trim(line.substr(colon + 1));
        if (name.empty() || request.headers.contains(name)) return ParseError{"invalid or duplicate header"};
        request.headers.emplace(std::move(name), std::move(value));
        cursor = line_end + 2;
    }

    std::size_t body_size = 0;
    if (const auto it = request.headers.find("content-length"); it != request.headers.end()) {
        const auto& text = it->second;
        const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), body_size);
        if (error != std::errc{} || ptr != text.data() + text.size()) return ParseError{"invalid Content-Length"};
        if (body_size > max_body_size) return ParseError{"body too large"};
    }
    const std::size_t consumed = header_end + 4 + body_size;
    if (bytes.size() < consumed) return Incomplete{};
    request.body = std::string(bytes.substr(header_end + 4, body_size));
    if (const auto it = request.headers.find("connection"); it != request.headers.end())
        request.keep_alive = lowercase(it->second) != "close";
    return ParsedRequest{std::move(request), consumed};
}

}  // namespace gateway::http
