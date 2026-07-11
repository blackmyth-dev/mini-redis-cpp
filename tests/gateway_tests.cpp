#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

#include "gateway/core/key_value_store.hpp"
#include "gateway/http/http_parser.hpp"
#include "gateway/redis/command_parser.hpp"
#include "gateway/storage/snapshot_store.hpp"

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}

int main() {
    using namespace std::chrono_literals;
    gateway::core::KeyValueStore store;
    store.set("name", "Duy");
    check(store.get("name") == "Duy", "SET then GET");
    check(store.exists("name"), "EXISTS true");
    check(store.erase("name"), "DEL existing key");
    check(!store.get("name"), "GET missing key");
    store.set("short", "lived");
    check(store.expire("short", 0s), "EXPIRE existing key");
    check(!store.get("short"), "lazy expiration");

    gateway::redis::CommandParser parser;
    check(std::holds_alternative<gateway::redis::Command>(parser.parse("SET language cpp")), "parse SET");
    check(std::holds_alternative<gateway::redis::ParseError>(parser.parse("EXPIRE key nope")), "reject bad TTL");
    check(std::holds_alternative<gateway::redis::ParseError>(parser.parse("UNKNOWN")), "reject unknown command");

    gateway::http::HttpParser http_parser;
    check(std::holds_alternative<gateway::http::Incomplete>(
              http_parser.parse("GET /health HTTP/1.1\r\nHost: localhost\r\n")),
          "HTTP parser waits for complete headers");
    const std::string pipelined =
        "PUT /kv/name HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\nDuy"
        "GET /kv/name HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const auto http_result = http_parser.parse(pipelined);
    check(std::holds_alternative<gateway::http::ParsedRequest>(http_result), "parse HTTP request with body");
    if (const auto* parsed = std::get_if<gateway::http::ParsedRequest>(&http_result)) {
        check(parsed->request.method == "PUT", "parse HTTP method");
        check(parsed->request.body == "Duy", "parse body using Content-Length");
        check(parsed->consumed < pipelined.size(), "leave pipelined request unconsumed");
    }
    check(std::holds_alternative<gateway::http::ParseError>(
              http_parser.parse("GET / HTTP/1.0\r\nHost: x\r\n\r\n")),
          "reject unsupported HTTP version");

    const auto path = std::filesystem::temp_directory_path() / "gateway_test_snapshot.db";
    gateway::storage::SnapshotStore snapshot(path);
    store.set("persisted", "yes");
    snapshot.save(store);
    gateway::core::KeyValueStore restored;
    snapshot.load(restored);
    check(restored.get("persisted") == "yes", "snapshot round trip");
    std::filesystem::remove(path);

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
