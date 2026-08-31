// Port of protocol-esr src/anchor-types.ts and src/buoy-types.ts.
#pragma once

#include <dwarfkit/antelope.hpp>

namespace dwarfkit {

struct LinkCreate {
    DK_STRUCT("link_create")
    Name session_name;
    PublicKey request_key;
    BinaryExtension<std::string> user_agent;
    DK_FIELDS(session_name, request_key, user_agent)
};

struct LinkInfo {
    DK_STRUCT("link_info")
    TimePointSec expiration;
    DK_FIELDS(expiration)
};

struct BuoyMessage {
    DK_STRUCT("buoy_message")
    PublicKey from;
    uint64_t nonce = 0;
    Bytes ciphertext;
    uint32_t checksum = 0;
    DK_FIELDS(from, nonce, ciphertext, checksum)
};

struct BuoySession {
    DK_STRUCT("buoy_session")
    Name session_name;
    PublicKey request_key;
    BinaryExtension<std::string> user_agent;
    DK_FIELDS(session_name, request_key, user_agent)
};

struct BuoyInfo {
    DK_STRUCT("buoy_info")
    TimePointSec expiration;
    DK_FIELDS(expiration)
};

}  // namespace dwarfkit
