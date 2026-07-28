#include "gateway/application/state_wire_codec.hpp"

#include <limits>

namespace gateway::application {
namespace {
void append_string(std::vector<std::uint8_t>& output,
                   const std::string_view value) {
    const auto length = static_cast<std::uint16_t>(value.size());
    output.push_back(static_cast<std::uint8_t>(length >> 8U));
    output.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    output.insert(output.end(), value.begin(), value.end());
}

bool read_string(const std::span<const std::uint8_t> input,
                 std::size_t& cursor, std::string& value) {
    if (cursor + 2 > input.size()) return false;
    const auto length = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[cursor]) << 8U) |
        input[cursor + 1]);
    cursor += 2;
    if (cursor + length > input.size()) return false;
    value.assign(reinterpret_cast<const char*>(input.data() + cursor), length);
    cursor += length;
    return true;
}
}  // namespace

std::vector<std::uint8_t> StateWireCodec::encode_key(
    const std::string_view key) {
    if (key.size() > std::numeric_limits<std::uint16_t>::max()) return {};
    std::vector<std::uint8_t> output;
    append_string(output, key);
    return output;
}

std::vector<std::uint8_t> StateWireCodec::encode_value(
    const std::string_view value) {
    return encode_key(value);
}

std::vector<std::uint8_t> StateWireCodec::encode_key_value(
    const std::string_view key, const std::string_view value) {
    if (key.size() > std::numeric_limits<std::uint16_t>::max() ||
        value.size() > std::numeric_limits<std::uint16_t>::max())
        return {};
    std::vector<std::uint8_t> output;
    append_string(output, key);
    append_string(output, value);
    return output;
}

std::optional<std::string> StateWireCodec::decode_key(
    const std::span<const std::uint8_t> bytes) {
    std::size_t cursor = 0;
    std::string key;
    if (!read_string(bytes, cursor, key) || cursor != bytes.size())
        return std::nullopt;
    return key;
}

std::optional<KeyValuePayload> StateWireCodec::decode_key_value(
    const std::span<const std::uint8_t> bytes) {
    std::size_t cursor = 0;
    KeyValuePayload result;
    if (!read_string(bytes, cursor, result.key) ||
        !read_string(bytes, cursor, result.value) ||
        cursor != bytes.size())
        return std::nullopt;
    return result;
}

}  // namespace gateway::application
