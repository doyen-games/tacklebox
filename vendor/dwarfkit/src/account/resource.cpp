#include <dwarfkit/account/resource.hpp>

namespace dwarfkit {

namespace {

const char* resourceName(ResourceType resource) {
    switch (resource) {
        case ResourceType::cpu:
            return "cpu";
        case ResourceType::net:
            return "net";
        case ResourceType::ram:
            return "ram";
    }
    return "";
}

}  // namespace

json Resource::toJSON() const {
    json rv = {{"resource", resourceName(resource)},
               {"available", available},
               {"used", used},
               {"max", max}};
    if (current_used) {
        rv["current_used"] = *current_used;
    }
    if (weight) {
        rv["weight"] = *weight;
    }
    return rv;
}

}  // namespace dwarfkit
