#include <dwarfkit/antelope/chain/permission_level.hpp>

namespace dwarfkit {

Result<PermissionLevel> PermissionLevel::from(std::string_view value) {
    const size_t at = value.find('@');
    if (at == std::string_view::npos || at == 0 || at == value.size() - 1 ||
        value.find('@', at + 1) != std::string_view::npos) {
        return err(ErrorKind::Invalid,
                   "Invalid permission level string, should be in the format <actor>@<permission>");
    }
    return PermissionLevel{Name::from(value.substr(0, at)), Name::from(value.substr(at + 1))};
}

}  // namespace dwarfkit
