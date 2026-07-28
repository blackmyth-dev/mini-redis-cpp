#pragma once

#include <filesystem>
#include <utility>

#include "gateway/domain/state_store.hpp"

namespace gateway::infrastructure {

class FileSnapshotRepository {
public:
    explicit FileSnapshotRepository(std::filesystem::path path)
        : path_(std::move(path)) {}
    void load(domain::StateStore& store) const;
    void save(domain::StateStore& store) const;

private:
    std::filesystem::path path_;
};

}  // namespace gateway::infrastructure
