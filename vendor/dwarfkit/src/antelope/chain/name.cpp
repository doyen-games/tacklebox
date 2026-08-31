#include <dwarfkit/antelope/chain/name.hpp>

namespace dwarfkit {

std::string Name::toString() const {
    std::string result;
    for (int bit = 63; bit >= 0;) {
        uint64_t c = 0;
        for (int i = 0; i < 5; ++i) {
            if (bit >= 0) {
                c = (c << 1) | ((value >> bit) & 1ull);
                --bit;
            }
        }
        if (c >= 6) {
            result += static_cast<char>(c + 'a' - 6);
        } else if (c >= 1) {
            result += static_cast<char>(c + '1' - 1);
        } else {
            result += '.';
        }
    }
    while (result.ends_with('.')) {
        result.pop_back();
    }
    return result;
}

}  // namespace dwarfkit
