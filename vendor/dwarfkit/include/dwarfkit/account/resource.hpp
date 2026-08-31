// Port of account src/resource.ts. The resource type is an enum rather than a
// string union and the source AccountObject is not retained on the instance.
#pragma once

#include <dwarfkit/antelope/api/v1/types.hpp>

namespace dwarfkit {

enum class ResourceType {
    cpu,
    net,
    ram,
};

class Resource {
public:
    template <typename Data>
    Resource(ResourceType resource, const Data& data) : resource(resource) {
        switch (resource) {
            case ResourceType::cpu:
                available = data.cpu_limit.available;
                current_used = data.cpu_limit.current_used;
                used = data.cpu_limit.used;
                max = data.cpu_limit.max;
                weight = data.cpu_weight;
                break;
            case ResourceType::net:
                available = data.net_limit.available;
                current_used = data.net_limit.current_used;
                used = data.net_limit.used;
                max = data.net_limit.max;
                weight = data.net_weight;
                break;
            case ResourceType::ram:
                available = data.ram_quota - static_cast<int64_t>(data.ram_usage);
                used = static_cast<int64_t>(data.ram_usage);
                max = data.ram_quota;
                break;
        }
    }

    ResourceType resource;
    int64_t available = 0;
    int64_t used = 0;
    int64_t max = 0;
    std::optional<int64_t> current_used;
    std::optional<int64_t> weight;

    json toJSON() const;
};

}  // namespace dwarfkit
