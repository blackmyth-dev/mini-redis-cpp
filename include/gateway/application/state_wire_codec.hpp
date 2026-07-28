#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gateway::application {

struct KeyValuePayload {
    std::string key;
    std::string value;
};

class StateWireCodec {
public:
    static std::vector<std::uint8_t> encode_key(std::string_view key);
    static std::vector<std::uint8_t> encode_value(std::string_view value);
    static std::vector<std::uint8_t> encode_key_value(std::string_view key,
                                                      std::string_view value);
    static std::optional<std::string> decode_key(
        std::span<const std::uint8_t> bytes);
    static std::optional<KeyValuePayload> decode_key_value(
        std::span<const std::uint8_t> bytes);
};

}  // namespace gateway::application

