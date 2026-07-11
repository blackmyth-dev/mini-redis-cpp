#include "gateway/redis/command_parser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>
#include <vector>

namespace gateway::redis {
namespace {
std::vector<std::string> words(std::string_view line) {
    std::istringstream input{std::string(line)};
    std::vector<std::string> result;
    for (std::string word; input >> word;) result.push_back(std::move(word));
    return result;
}
ParseError arity(std::string_view command) {
    return {"wrong number of arguments for " + std::string(command)};
}
}  // namespace

ParseResult CommandParser::parse(const std::string_view line) const {
    auto tokens = words(line);
    if (tokens.empty()) return ParseError{"empty command"};
    std::transform(tokens[0].begin(), tokens[0].end(), tokens[0].begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const auto& name = tokens[0];
    if (name == "SET") {
        if (tokens.size() != 3) return arity(name);
        return Command{Set{std::move(tokens[1]), std::move(tokens[2])}};
    }
    if (name == "GET") {
        if (tokens.size() != 2) return arity(name);
        return Command{Get{std::move(tokens[1])}};
    }
    if (name == "DEL") {
        if (tokens.size() != 2) return arity(name);
        return Command{Del{std::move(tokens[1])}};
    }
    if (name == "EXISTS") {
        if (tokens.size() != 2) return arity(name);
        return Command{Exists{std::move(tokens[1])}};
    }
    if (name == "EXPIRE") {
        if (tokens.size() != 3) return arity(name);
        std::int64_t seconds{};
        const auto [ptr, error] = std::from_chars(tokens[2].data(), tokens[2].data() + tokens[2].size(), seconds);
        if (error != std::errc{} || ptr != tokens[2].data() + tokens[2].size() || seconds < 0)
            return ParseError{"expiry must be a non-negative integer"};
        return Command{Expire{std::move(tokens[1]), seconds}};
    }
    if (name == "TTL") {
        if (tokens.size() != 2) return arity(name);
        return Command{Ttl{std::move(tokens[1])}};
    }
    if (name == "SAVE") {
        if (tokens.size() != 1) return arity(name);
        return Command{Save{}};
    }
    if (name == "QUIT") {
        if (tokens.size() != 1) return arity(name);
        return Command{Quit{}};
    }
    return ParseError{"unknown command: " + name};
}

}  // namespace gateway::redis

