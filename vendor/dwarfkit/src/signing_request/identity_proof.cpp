#include <dwarfkit/signing_request/identity_proof.hpp>

#include <chrono>

namespace dwarfkit {

Result<IdentityProof> IdentityProof::fromString(std::string_view value) {
    const size_t space = value.find(' ');
    if (space == std::string_view::npos || value.substr(0, space) != "EOSIO" ||
        value.find(' ', space + 1) != std::string_view::npos) {
        return err(ErrorKind::Invalid, "Invalid IdentityProof string");
    }
    const auto data = base64u::decode(value.substr(space + 1));
    return Serializer::decode<IdentityProof>(std::span<const uint8_t>(data));
}

Result<IdentityProof> IdentityProof::from(const json& value) {
    if (value.is_string()) {
        return fromString(std::string_view(value.get_ref<const std::string&>()));
    }
    return structFrom<IdentityProof>(value);
}

Result<IdentityProof> IdentityProof::fromPayload(const json& payload,
                                                 const SigningRequestEncodingOptions& options) {
    DK_TRY(request, SigningRequest::from(payload.value("req", ""), options));
    if (!(request.version >= 3 && request.isIdentity())) {
        return err(ErrorKind::Invalid, "Not an identity request");
    }
    IdentityProof proof;
    if (payload.contains("cid")) {
        DK_TRY(cid, ChainId::from(std::string_view(payload.value("cid", ""))));
        proof.chainId = cid;
    } else {
        DK_TRY(cid, request.getChainId());
        proof.chainId = cid;
    }
    const auto scope = request.getIdentityScope();
    if (!scope) {
        return err(ErrorKind::Invalid, "Missing identity scope");
    }
    proof.scope = *scope;
    DK_TRY(expiration, TimePointSec::from(std::string_view(payload.value("ex", ""))));
    proof.expiration = expiration;
    proof.signer = PermissionLevel{Name::from(payload.value("sa", "")),
                                   Name::from(payload.value("sp", ""))};
    DK_TRY(sig, Signature::from(payload.value("sig", "")));
    proof.signature = sig;
    return proof;
}

Result<Transaction> IdentityProof::transaction() const {
    IdentityV3 identity;
    identity.scope = scope;
    identity.permission = signer;
    DK_TRY(data, Serializer::encode(identity));
    Action action;
    action.account = Name::from("");
    action.name = Name::from("identity");
    action.authorization = {signer};
    action.data = data;
    Transaction tx;
    tx.expiration = expiration;
    tx.actions = {std::move(action)};
    return tx;
}

Result<PublicKey> IdentityProof::recover() const {
    DK_TRY(tx, transaction());
    return signature.recoverDigest(tx.signingDigest(chainId));
}

Result<bool> IdentityProof::verify(const Authority& auth,
                                   std::optional<TimePointSec> currentTime) const {
    const int64_t now =
        currentTime
            ? currentTime->toMilliseconds()
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    if (now >= expiration.toMilliseconds()) {
        return false;
    }
    DK_TRY(key, recover());
    return auth.hasPermission(key);
}

std::string IdentityProof::toString() const {
    ABIEncoder encoder;
    (void)abi_traits<IdentityProof>::toABI(*this, encoder);
    return "EOSIO " + base64u::encode(encoder.getData(), false);
}

}  // namespace dwarfkit
