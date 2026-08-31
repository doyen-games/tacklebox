# Vendored third-party dependencies

| Dependency | Version | Source | License | Files |
|---|---|---|---|---|
| tl::expected | v1.1.0 | github.com/TartanLlama/expected | CC0 | expected/include/tl/expected.hpp |
| nlohmann/json | v3.11.3 | github.com/nlohmann/json | MIT | nlohmann/include/nlohmann/json.hpp |
| doctest | v2.4.11 | github.com/doctest/doctest | MIT | doctest/doctest/doctest.h |
| zlib | 1.3.1 | github.com/madler/zlib | zlib | zlib/ (gz* file API omitted) |
| qrcodegen | v1.8.0 | github.com/nayuki/QR-Code-generator (cpp/) | MIT | qrcodegen/qrcodegen.{hpp,cpp} |
| trezor-crypto | commit da48ad0c0980411d5f3f1f78084c65255c5213ba | github.com/trezor/trezor-firmware (crypto/) | MIT | trezor-crypto/ (subset, see COMMIT) |
| libsecp256k1 | v0.6.0 | github.com/bitcoin-core/secp256k1 | MIT | fetched at configure time (FetchContent), not vendored |
| libcurl | 8.10.1 | github.com/curl/curl | curl | fetched at configure time when DK_WITH_CURL and no system curl |

## Local patches (marked `dwarfkit patch` in the source)

MSVC compatibility, guarded by `#ifdef _MSC_VER`:
- `trezor-crypto/bignum.c`: `__builtin_clz` shim via `_BitScanReverse`.
- `trezor-crypto/blake2b.c`: `__attribute__((packed))` replaced with `#pragma pack`.
- `trezor-crypto/options.h`: `__wur` defined empty.

Minimal-build trim, guarded by `#ifndef DK_TREZOR_MINIMAL` (always defined by the build):
- `trezor-crypto/ecdsa.c`: compiles out `ecdsa_sign`, `ecdsa_verify` (message-hashing variants) and the address/WIF helpers, which need `hasher.c`, `address.c` and `base58.c`. Dwarfkit only signs and verifies 32-byte digests.
- `trezor-crypto/{nist256p1,secp256k1}.{c,h}`: compiles out the `curve_info` constants and the `bip32.h` include.

## Intentionally not compiled

Only these trezor files build: `bignum.c ecdsa.c hmac.c hmac_drbg.c memzero.c nist256p1.c rfc6979.c ripemd160.c secp256k1.c sha2.c` plus the Gladman AES subset `aes/{aescrypt,aeskey,aestab,aes_modes}.c` (defines `AES_128 AES_192 AES_VAR`, matching trezor's Makefile) for sealed messages. `aes/{aesccm,aesgcm,aestst,gf128mul}` and friends are not vendored. Everything else in the directory is vendored for header completeness only.

- `trezor-crypto/rand.c` is not vendored; its `random_buffer` (declared in `rand.h`) is provided by dwarfkit's OS CSPRNG shim so no insecure fallback PRNG can ever link in.
- trezor's `base58.c` uses VLAs (no MSVC) and is unused; dwarfkit's public base58 is a direct port of wharfkit `crypto/base58.ts` (error messages and all four check variants).
