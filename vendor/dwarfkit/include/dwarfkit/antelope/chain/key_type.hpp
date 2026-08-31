// Port of antelope src/chain/key-type.ts
#pragma once

#include <string_view>

#include <dwarfkit/core/result.hpp>

namespace dwarfkit {

// Supported Antelope/EOSIO curve types.
enum class KeyType { K1, R1, WA };

namespace keytype {

constexpr std::string_view toString(KeyType value) {
    switch (value) {
        case KeyType::K1: return "K1";
        case KeyType::R1: return "R1";
        case KeyType::WA: return "WA";
    }
    return "";
}

inline Result<int> indexFor(KeyType value) {
    switch (value) {
        case KeyType::K1: return 0;
        case KeyType::R1: return 1;
        case KeyType::WA: return 2;
    }
    return err(ErrorKind::Invalid, "Unknown curve type");
}

inline Result<KeyType> from(int index) {
    switch (index) {
        case 0: return KeyType::K1;
        case 1: return KeyType::R1;
        case 2: return KeyType::WA;
    }
    return err(ErrorKind::Invalid, "Unknown curve type");
}

inline Result<KeyType> from(std::string_view value) {
    if (value == "K1") return KeyType::K1;
    if (value == "R1") return KeyType::R1;
    if (value == "WA") return KeyType::WA;
    return err(ErrorKind::Invalid, "Unknown curve type: " + std::string(value));
}

}  // namespace keytype

}  // namespace dwarfkit
