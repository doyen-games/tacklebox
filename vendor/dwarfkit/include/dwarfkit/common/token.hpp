// Port of common src/common/token.ts
#pragma once

#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit {

struct TokenIdentifier {
    DK_STRUCT("token_identifier")
    Checksum256 chain;
    Name contract;
    Asset::Symbol symbol;
    DK_FIELDS(chain, contract, symbol)
};

struct TokenMeta {
    DK_STRUCT("token_meta")
    TokenIdentifier id;
    std::optional<std::string> logo;
    DK_FIELDS(id, logo)
};

struct TokenBalance {
    DK_STRUCT("token_balance")
    Asset asset;
    Name contract;
    TokenMeta metadata;
    DK_FIELDS(asset, contract, metadata)
};

}  // namespace dwarfkit
