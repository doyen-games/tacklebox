add_library(dwarfkit STATIC
  core/base64.cpp
  core/hash.cpp
  core/random.cpp
  core/version.cpp
  core/zlib.cpp
  antelope/base58.cpp
  antelope/crypto.cpp
  antelope/utils.cpp
  antelope/chain/asset.cpp
  antelope/chain/blob.cpp
  antelope/chain/block_id.cpp
  antelope/chain/bytes.cpp
  antelope/chain/checksum.cpp
  antelope/chain/float.cpp
  antelope/chain/integer.cpp
  antelope/chain/name.cpp
  antelope/chain/private_key.cpp
  antelope/chain/public_key.cpp
  antelope/chain/abi.cpp
  antelope/chain/action.cpp
  antelope/chain/authority.cpp
  antelope/chain/permission_level.cpp
  antelope/chain/signature.cpp
  antelope/chain/time.cpp
  antelope/chain/transaction.cpp
  antelope/serializer/builtins.cpp
  antelope/serializer/decoder.cpp
  antelope/serializer/serializer.cpp
  antelope/serializer/traits.cpp
  antelope/api/client.cpp
  antelope/api/provider.cpp
  antelope/api/v1/chain.cpp
  antelope/api/v1/history.cpp
  antelope/api/v1/types.cpp
  antelope/p2p/p2p.cpp
  abicache/abicache.cpp
  common/chains.cpp
  signing_request/base64u.cpp
  signing_request/chain_id.cpp
  signing_request/identity_proof.cpp
  signing_request/signing_request.cpp
  protocol_esr/buoy.cpp
  protocol_esr/callback.cpp
  protocol_esr/esr.cpp
  protocol_esr/sealed_messages.cpp
  protocol_esr/utils.cpp
  session/account_creation.cpp
  session/interfaces.cpp
  session/kit.cpp
  session/session.cpp
  session/storage.cpp
  session/transact.cpp
  session/utils.cpp
  session/wallet.cpp
  plugins/wallet/privatekey.cpp
  plugins/account_creation/anchor.cpp
  plugins/account_creation/jungle4.cpp
  plugins/transact/autocorrect.cpp
  plugins/transact/cosigner.cpp
  plugins/transact/explorerlink.cpp
  plugins/transact/finality.cpp
  plugins/transact/resource_provider.cpp
  resources/powerup.cpp
  resources/ram.cpp
  resources/resources.cpp
  resources/rex.cpp
  msigs/msigs.cpp
  contract/contract.cpp
  contract/kit.cpp
  contract/table.cpp
  contract/utils.cpp
  actionstream/client.cpp
  atomicassets/endpoints.cpp
  atomicassets/kits.cpp
  atomicassets/objects.cpp
  atomicassets/utility.cpp
  token/system_token.cpp
  token/token.cpp
  account/system_contract.cpp
  account/permission.cpp
  account/resource.cpp
  plugins/wallet/anchor.cpp
  plugins/wallet/anchor_chains.cpp
  plugins/wallet/anchor_mode.cpp
  plugins/wallet/anchor_translations.cpp
  plugins/wallet/anchor_transports.cpp
  plugins/wallet/cleos.cpp
  plugins/wallet/cloudwallet.cpp
  plugins/wallet/cloudwallet_translations.cpp
  plugins/wallet/cloudwallet_types.cpp
  plugins/wallet/cloudwallet_utils.cpp
)

target_include_directories(dwarfkit PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_link_libraries(dwarfkit
  PUBLIC dk_expected dk_json
  PRIVATE dk_trezor_crypto secp256k1 dk_zlib)
if(WIN32)
  target_link_libraries(dwarfkit PRIVATE bcrypt)
endif()

target_compile_features(dwarfkit PUBLIC cxx_std_20)
# consumers compile DK_FIELDS variadic macros too
target_compile_options(dwarfkit PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>)
dk_warnings(dwarfkit)

if(DK_WITH_CURL)
  find_package(CURL QUIET)
  if(NOT CURL_FOUND)
    include(FetchContent)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
    # ws:// support for CurlWebSocketProvider (experimental option in 8.10)
    set(ENABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
    if(WIN32)
      set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(curl
      GIT_REPOSITORY https://github.com/curl/curl
      GIT_TAG curl-8_10_1
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(curl)
  endif()
  add_library(dwarfkit_curl STATIC
    transport/curl_fetch_provider.cpp
    transport/curl_websocket_provider.cpp)
  target_link_libraries(dwarfkit_curl PUBLIC dwarfkit PRIVATE CURL::libcurl)
  dk_warnings(dwarfkit_curl)
endif()
