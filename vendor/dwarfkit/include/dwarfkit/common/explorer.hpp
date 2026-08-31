// Port of common src/common/explorer.ts
#pragma once

#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit {

struct ExplorerDefinition {
    DK_STRUCT("explorer_definition")
    std::string prefix;
    std::string suffix;
    DK_FIELDS(prefix, suffix)

    std::string url(std::string_view id) const {
        return prefix + std::string(id) + suffix;
    }
};

}  // namespace dwarfkit
