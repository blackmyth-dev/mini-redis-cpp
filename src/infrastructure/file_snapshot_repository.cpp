#include "gateway/infrastructure/file_snapshot_repository.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace gateway::infrastructure {

void FileSnapshotRepository::save(domain::StateStore& store) const {
    const auto temporary = path_.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open snapshot for writing");
    output << "EDGE_GATEWAY_DB 2\n";
    for (const auto& record : store.snapshot()) {
        const auto expiry = record.expires_at
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  record.expires_at->time_since_epoch()).count()
            : -1;
        output << std::quoted(record.key) << ' ' << std::quoted(record.value)
               << ' ' << expiry << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot write snapshot");
    std::filesystem::rename(temporary, path_);
}

void FileSnapshotRepository::load(domain::StateStore& store) const {
    if (!std::filesystem::exists(path_)) return;
    std::ifstream input(path_);
    std::string magic;
    int version{};
    if (!(input >> magic >> version) || magic != "EDGE_GATEWAY_DB" ||
        (version != 1 && version != 2))
        throw std::runtime_error("invalid snapshot header");
    std::vector<domain::StateRecord> records;
    std::string key;
    std::string value;
    std::int64_t expiry{};
    while (input >> std::quoted(key) >> std::quoted(value) >> expiry) {
        std::optional<std::chrono::system_clock::time_point> expires_at;
        if (expiry >= 0)
            expires_at = std::chrono::system_clock::time_point{
                std::chrono::milliseconds{expiry}};
        records.push_back({key, value, expires_at});
    }
    if (!input.eof()) throw std::runtime_error("corrupt snapshot record");
    store.restore(std::move(records));
}

}  // namespace gateway::infrastructure
