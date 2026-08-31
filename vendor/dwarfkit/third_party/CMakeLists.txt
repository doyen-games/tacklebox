# Vendored dependencies. See README.md in this directory for versions, sources and local patches.
# dk_warnings is never applied here; consumers see these headers as SYSTEM includes.

add_library(dk_expected INTERFACE)
target_include_directories(dk_expected SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/expected/include)

add_library(dk_json INTERFACE)
target_include_directories(dk_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann/include)

add_library(dk_doctest INTERFACE)
target_include_directories(dk_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/doctest)

# zlib rather than miniz: pako (the zlib JS port Wharfkit compresses with)
# produces byte-identical streams to real zlib, which keeps packed transaction
# fixture hashes and ESR URIs byte-compatible.
add_library(dk_zlib STATIC
  zlib/adler32.c
  zlib/compress.c
  zlib/crc32.c
  zlib/deflate.c
  zlib/infback.c
  zlib/inffast.c
  zlib/inflate.c
  zlib/inftrees.c
  zlib/trees.c
  zlib/uncompr.c
  zlib/zutil.c)
target_include_directories(dk_zlib SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/zlib)
target_compile_definitions(dk_zlib PUBLIC ZLIB_CONST)
if(MSVC)
  target_compile_options(dk_zlib PRIVATE /wd4996 /wd4131 /wd4127 /wd4244 /wd4245 /wd4267)
endif()

add_library(dk_qrcodegen STATIC qrcodegen/qrcodegen.cpp)
target_include_directories(dk_qrcodegen SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/qrcodegen)
target_compile_features(dk_qrcodegen PRIVATE cxx_std_20)

# R1 (NIST P-256) sign/verify/recover plus sha2/sha512/ripemd160/hmac/hmac_drbg,
# and Gladman AES (aes/) for sealed messages (AES-256-CBC).
# DK_TREZOR_MINIMAL compiles only the ecdsa math: no bip32, no address/base58
# helpers, no hasher.c family (see README.md patches). trezor's rand.c is
# intentionally not compiled; dwarfkit's CSPRNG shim provides the random_buffer
# symbol declared in rand.h.
add_library(dk_trezor_crypto STATIC
  trezor-crypto/bignum.c
  trezor-crypto/ecdsa.c
  trezor-crypto/hmac.c
  trezor-crypto/hmac_drbg.c
  trezor-crypto/memzero.c
  trezor-crypto/nist256p1.c
  trezor-crypto/rfc6979.c
  trezor-crypto/ripemd160.c
  trezor-crypto/secp256k1.c
  trezor-crypto/sha2.c
  trezor-crypto/aes/aescrypt.c
  trezor-crypto/aes/aeskey.c
  trezor-crypto/aes/aestab.c
  trezor-crypto/aes/aes_modes.c)
target_include_directories(dk_trezor_crypto SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/trezor-crypto)
# AES_128/AES_192/AES_VAR match trezor's own Makefile (AES_256 is always on)
target_compile_definitions(dk_trezor_crypto PUBLIC DK_TREZOR_MINIMAL=1 AES_128 AES_192 AES_VAR)
if(MSVC)
  target_compile_options(dk_trezor_crypto PRIVATE /wd4018 /wd4244 /wd4267 /wd4146)
endif()

# K1 (secp256k1) with recovery + ecdh, pinned release, fetched at configure time
include(FetchContent)
set(SECP256K1_ENABLE_MODULE_ECDH ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLSWIFT OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SECP256K1_DISABLE_SHARED ON CACHE BOOL "" FORCE)
FetchContent_Declare(secp256k1
  GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1
  GIT_TAG v0.6.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(secp256k1)
