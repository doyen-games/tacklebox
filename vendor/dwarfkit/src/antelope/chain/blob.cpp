#include <dwarfkit/antelope/chain/blob.hpp>

#include <dwarfkit/core/base64.hpp>

namespace dwarfkit {

Result<Blob> Blob::fromString(std::string_view value) {
    // fix up base64 padding from nodeos
    std::string fixed(value);
    switch (fixed.size() % 4) {
        case 2:
            fixed += "==";
            break;
        case 3:
            fixed += "=";
            break;
        case 1:
            fixed.pop_back();
            break;
    }
    DK_TRY(array, base64Decode(fixed));
    return Blob(std::move(array));
}

std::string Blob::base64String() const { return base64Encode(array); }

}  // namespace dwarfkit
