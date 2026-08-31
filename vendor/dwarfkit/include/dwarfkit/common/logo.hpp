// Port of common src/common/logo.ts
#pragma once

#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit {

struct Logo {
    DK_STRUCT("logo")
    std::string dark;
    std::string light;
    DK_FIELDS(dark, light)

    static Logo from(std::string_view value) {
        return Logo{std::string(value), std::string(value)};
    }
    static Result<Logo> from(const json& value) {
        if (value.is_string()) {
            return from(std::string_view(value.get_ref<const std::string&>()));
        }
        return structFrom<Logo>(value);
    }

    std::string getVariant(std::string_view variant) const {
        return variant == "dark" ? dark : light;
    }

    std::string toString() const { return light; }
};

}  // namespace dwarfkit
