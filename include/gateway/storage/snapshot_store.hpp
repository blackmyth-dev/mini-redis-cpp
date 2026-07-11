#pragma once

#include <filesystem>
#include <utility>
#include "gateway/core/key_value_store.hpp"

namespace gateway::storage {

class SnapshotStore {
public:
    explicit SnapshotStore(std::filesystem::path path) : path_(std::move(path)) {}
    void save(core::KeyValueStore& store) const;
    void load(core::KeyValueStore& store) const;
private:
    std::filesystem::path path_;
};

}  // namespace gateway::storage

