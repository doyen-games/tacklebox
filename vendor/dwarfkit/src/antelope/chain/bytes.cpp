#include <dwarfkit/antelope/chain/bytes.hpp>

namespace dwarfkit {

Result<Bytes> Bytes::fromString(std::string_view value, BytesEncoding encoding) {
    if (encoding == BytesEncoding::hex) {
        DK_TRY(array, hexToArray(value));
        return Bytes(std::move(array));
    }
    return Bytes(std::vector<uint8_t>(value.begin(), value.end()));
}

Result<Bytes> Bytes::random(size_t length) {
    DK_TRY(array, secureRandom(length));
    return Bytes(std::move(array));
}

void Bytes::zeropad(size_t n, bool truncate) {
    const size_t newSize = truncate ? n : std::max(n, array.size());
    std::vector<uint8_t> padded(newSize, 0);
    if (truncate && array.size() > newSize) {
        std::copy(array.begin(), array.begin() + newSize, padded.begin());
    } else {
        std::copy(array.begin(), array.end(), padded.begin() + (newSize - array.size()));
    }
    array = std::move(padded);
}

}  // namespace dwarfkit
