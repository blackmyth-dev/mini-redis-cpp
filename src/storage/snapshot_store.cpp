#include "gateway/storage/snapshot_store.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace gateway::storage {

void SnapshotStore::save(core::KeyValueStore& store) const {
    const auto temporary = path_.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open snapshot for writing");
    output << "EDGE_GATEWAY_DB 1\n";
    for (const auto& entry : store.snapshot()) {
        const auto expiry = entry.expires_at
            ? std::chrono::duration_cast<std::chrono::milliseconds>(entry.expires_at->time_since_epoch()).count()
            : -1;
        output << std::quoted(entry.key) << ' ' << std::quoted(entry.value) << ' ' << expiry << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot write snapshot");
    std::filesystem::rename(temporary, path_);
}

void SnapshotStore::load(core::KeyValueStore& store) const {
    if (!std::filesystem::exists(path_)) return;
    std::ifstream input(path_);
    std::string magic;
    int version{};
    if (!(input >> magic >> version) || magic != "EDGE_GATEWAY_DB" || version != 1)
        throw std::runtime_error("invalid snapshot header");
    std::vector<core::SnapshotEntry> entries;
    std::string key;
    std::string value;
    std::int64_t expiry{};
    while (input >> std::quoted(key) >> std::quoted(value) >> expiry) {
        std::optional<std::chrono::system_clock::time_point> expires_at;
        if (expiry >= 0) expires_at = std::chrono::system_clock::time_point{std::chrono::milliseconds{expiry}};
        entries.push_back({key, value, expires_at});
    }
    if (!input.eof()) throw std::runtime_error("corrupt snapshot record");
    store.restore(std::move(entries));
}

}  // namespace gateway::storage
