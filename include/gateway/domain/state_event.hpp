#pragma once

#include <optional>
#include <string>

namespace gateway::domain {

enum class ChangeOrigin { tcp, http, mqtt, someip, restore, internal };

struct StateChanged {
    std::string key;
    std::optional<std::string> value;
    ChangeOrigin origin{ChangeOrigin::internal};
};

}  // namespace gateway::domain

