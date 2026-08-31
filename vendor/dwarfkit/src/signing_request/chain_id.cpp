#include <dwarfkit/signing_request/chain_id.hpp>

#include <utility>

namespace dwarfkit {

namespace {

constexpr std::pair<ChainName, std::string_view> chainIdLookup[] = {
    {ChainName::EOS, "aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906"},
    {ChainName::TELOS, "4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11"},
    {ChainName::JUNGLE, "e70aaab8997e1dfce58fbfac80cbbb8fecec7b99cf982a9444273cbc64c41473"},
    {ChainName::KYLIN, "5fff1dae8dc8e2fc4d5b23b2c7665c97f9e9d8edf2b6485a86ba311c25639191"},
    {ChainName::WORBLI, "73647cde120091e0a4b85bced2f3cfdb3041e266cbbe95cee59b73235a1b3b6f"},
    {ChainName::BOS, "d5a3d18fbb3c084e3b1f3fa98c21014b5f3db536cc15d08f9f6479517c6a3d86"},
    {ChainName::MEETONE, "cfe6486a83bad4962f232d48003b1824ab5665c36778141034d75e57b956e422"},
    {ChainName::INSIGHTS, "b042025541e25a472bffde2d62edd457b7e70cee943412b1ea0f044f88591664"},
    {ChainName::BEOS, "b912d19a6abd2b1b05611ae5be473355d64d95aeff0c09bedc8c166cd6468fe4"},
    {ChainName::WAX, "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4"},
    {ChainName::PROTON, "384da888112027f0321850a169f737c33e53b388aad48b5adace4bab97f437e0"},
    {ChainName::FIO, "21dcae42c0182200e93f954a074011f9048a7624c6fe81d3c9541a614a88bd1c"},
};

}  // namespace

Result<ChainId> ChainId::from(ChainName alias) {
    for (const auto& [name, id] : chainIdLookup) {
        if (name == alias) {
            return from(id);
        }
    }
    return err(ErrorKind::Invalid, "Unknown chain id alias");
}

Result<ChainId> ChainId::from(std::string_view hex) {
    DK_TRY(sum, Checksum256::from(hex));
    return ChainId(sum);
}

ChainIdVariant ChainId::chainVariant() const {
    const ChainName name = chainName();
    if (name != ChainName::UNKNOWN) {
        return ChainIdVariant(ChainAlias(static_cast<uint8_t>(name)));
    }
    return ChainIdVariant(*this);
}

ChainName ChainId::chainName() const {
    const std::string cid = hexString();
    for (const auto& [name, id] : chainIdLookup) {
        if (id == cid) {
            return name;
        }
    }
    return ChainName::UNKNOWN;
}

Result<ChainId> variantChainId(const ChainIdVariant& variant) {
    if (const ChainId* id = variant.get_if<ChainId>()) {
        return *id;
    }
    const ChainAlias* alias = variant.get_if<ChainAlias>();
    return ChainId::from(alias->value);
}

}  // namespace dwarfkit
