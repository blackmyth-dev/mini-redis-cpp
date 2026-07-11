#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace gateway::redis {

struct Set { std::string key; std::string value; };
struct Get { std::string key; };
struct Del { std::string key; };
struct Exists { std::string key; };
struct Expire { std::string key; std::int64_t seconds; };
struct Ttl { std::string key; };
struct Save {};
struct Quit {};
using Command = std::variant<Set, Get, Del, Exists, Expire, Ttl, Save, Quit>;
struct ParseError { std::string message; };
using ParseResult = std::variant<Command, ParseError>;

class CommandParser {
public:
    ParseResult parse(std::string_view line) const;
};

}  // namespace gateway::redis

