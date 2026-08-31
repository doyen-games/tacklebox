#include <dwarfkit/antelope/serializer/decoder.hpp>

namespace dwarfkit {

namespace {

// TextDecoder('utf-8', {fatal: true}) equivalent
bool isValidUTF8(std::span<const uint8_t> data) {
    size_t i = 0;
    while (i < data.size()) {
        const uint8_t byte = data[i];
        size_t extra = 0;
        uint32_t code = 0;
        if (byte < 0x80) {
            i++;
            continue;
        } else if ((byte & 0xe0) == 0xc0) {
            extra = 1;
            code = byte & 0x1f;
        } else if ((byte & 0xf0) == 0xe0) {
            extra = 2;
            code = byte & 0x0f;
        } else if ((byte & 0xf8) == 0xf0) {
            extra = 3;
            code = byte & 0x07;
        } else {
            return false;
        }
        if (i + extra >= data.size()) {
            return false;
        }
        for (size_t j = 1; j <= extra; j++) {
            if ((data[i + j] & 0xc0) != 0x80) {
                return false;
            }
            code = (code << 6) | (data[i + j] & 0x3f);
        }
        // overlong encodings, surrogates and out of range
        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
            (extra == 3 && code < 0x10000) || (code >= 0xd800 && code <= 0xdfff) ||
            code > 0x10ffff) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

}  // namespace

Result<std::string> ABIDecoder::readString() {
    DK_TRY(length, readVaruint32());
    DK_TRY(data, readArray(length));
    if (!ignoreInvalidUTF8_ && !isValidUTF8(data)) {
        return err(ErrorKind::Invalid, "Invalid UTF-8 data");
    }
    return std::string(data.begin(), data.end());
}

}  // namespace dwarfkit
