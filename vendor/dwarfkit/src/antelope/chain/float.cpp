#include <dwarfkit/antelope/chain/float.hpp>

namespace dwarfkit {

Result<Float128> Float128::from(const Bytes& value) {
    if (value.array.size() != byteWidth) {
        return err(ErrorKind::Invalid, "Invalid float128");
    }
    return Float128(value);
}

Result<Float128> Float128::from(std::string_view value) {
    if (value.starts_with("0x")) {
        value = value.substr(2);
    }
    DK_TRY(bytes, Bytes::from(value));
    return from(bytes);
}

Result<Float128> Float128::random() {
    DK_TRY(bytes, Bytes::random(byteWidth));
    return Float128(std::move(bytes));
}

}  // namespace dwarfkit
