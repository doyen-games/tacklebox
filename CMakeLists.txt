cmake_minimum_required(VERSION 3.24)
project(TackleBox VERSION 0.2.0 LANGUAGES C CXX)

# One source of truth for the version string (sidebar, About, updater, CPack).
configure_file(cmake/tb_version.h.in ${CMAKE_BINARY_DIR}/generated/tb_version.h @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(TB_BUILD_TESTS "Build tacklebox_tests" ${PROJECT_IS_TOP_LEVEL})
option(TB_BUILD_FUZZERS "Build libFuzzer harnesses (Clang only; skips the UI stack)" OFF)

if(TB_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TB_BUILD_FUZZERS requires Clang (libFuzzer)")
  endif()
  # Instrument everything the fuzzers can reach; the harness adds the driver.
  add_compile_options(-fsanitize=fuzzer-no-link,address -g -O1)
  add_link_options(-fsanitize=address)
  set(TB_BUILD_TESTS OFF)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dwarfkit (vendored) - the Antelope SDK layer. We consume the static library
# plus its curl transport; its own tests/tools/examples stay out of the build.
# ---------------------------------------------------------------------------
set(DK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DK_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(DK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DK_WITH_CURL ON CACHE BOOL "" FORCE)
# Keep vendored curl's install rules out of our packages (CPack installs
# everything registered; only the app component belongs in the installer).
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

# Mobile TLS for the curl dwarfkit fetches: Android has no system TLS library
# curl can see, so pull mbedTLS first and point curl at it; iOS uses the OS
# Secure Transport stack.
if(ANDROID)
  include(FetchContent)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls
    GIT_TAG v3.6.2
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(mbedtls)
  set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
  set(MBEDTLS_INCLUDE_DIRS ${mbedtls_SOURCE_DIR}/include CACHE PATH "" FORCE)
  set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
  set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
  set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(vendor/dwarfkit)

if(NOT TB_BUILD_FUZZERS)
# ---------------------------------------------------------------------------
# SDL3 - one windowing/input/lifecycle layer for desktop AND mobile.
# TB_MOBILE marks iOS/Android configurations (GLES context, touch, fullscreen).
# ---------------------------------------------------------------------------
option(TB_MOBILE "Build for a touch-first mobile platform (iOS/Android)" OFF)
if(ANDROID OR (APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS"))
  set(TB_MOBILE ON CACHE BOOL "" FORCE)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/sdl)

# ---------------------------------------------------------------------------
# Dear ImGui - core + SDL3/OpenGL3 backends as a static library.
# ---------------------------------------------------------------------------
add_library(imgui STATIC
  vendor/imgui/imgui.cpp
  vendor/imgui/imgui_draw.cpp
  vendor/imgui/imgui_tables.cpp
  vendor/imgui/imgui_widgets.cpp
  vendor/imgui/backends/imgui_impl_sdl3.cpp
  vendor/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui SYSTEM PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
if(TB_MOBILE)
  target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
endif()

# ---------------------------------------------------------------------------
# Embedded assets: fonts are baked into the binary so the app is one file.
# A tiny host tool converts each TTF into a C header at build time.
# ---------------------------------------------------------------------------
add_executable(tb_embed tools/embed.c)

set(TB_EMBED_DIR ${CMAKE_CURRENT_BINARY_DIR}/embedded)
file(MAKE_DIRECTORY ${TB_EMBED_DIR})

function(tb_embed_font NAME FILE)
  add_custom_command(
    OUTPUT ${TB_EMBED_DIR}/${NAME}.h
    COMMAND tb_embed ${CMAKE_CURRENT_SOURCE_DIR}/${FILE} ${TB_EMBED_DIR}/${NAME}.h ${NAME}
    DEPENDS tb_embed ${CMAKE_CURRENT_SOURCE_DIR}/${FILE}
    COMMENT "Embedding ${FILE}")
  set(TB_EMBED_HEADERS ${TB_EMBED_HEADERS} ${TB_EMBED_DIR}/${NAME}.h PARENT_SCOPE)
endfunction()

tb_embed_font(font_rajdhani_regular  assets/fonts/Rajdhani-Regular.ttf)
tb_embed_font(font_rajdhani_medium   assets/fonts/Rajdhani-Medium.ttf)
tb_embed_font(font_rajdhani_semibold assets/fonts/Rajdhani-SemiBold.ttf)
tb_embed_font(font_rajdhani_bold     assets/fonts/Rajdhani-Bold.ttf)
tb_embed_font(font_sharetech_mono    assets/fonts/ShareTechMono-Regular.ttf)

add_custom_target(tb_embedded_assets DEPENDS ${TB_EMBED_HEADERS})
endif()  # NOT TB_BUILD_FUZZERS

# ---------------------------------------------------------------------------
# tacklebox_core - everything below the UI: vault, guard, chain services,
# application controller. UI-free so the test binary can link it.
# ---------------------------------------------------------------------------
add_library(tacklebox_core STATIC
  src/core/clipboard.cpp
  src/core/deeplink.cpp
  src/core/log.cpp
  src/core/paths.cpp
  src/core/rng.cpp
  src/core/secure.cpp
  src/core/autostart.cpp
  src/core/task_runner.cpp
  src/core/util.cpp
  src/vault/cipher.cpp
  src/vault/kdf.cpp
  src/vault/vault.cpp
  src/guard/engine.cpp
  src/guard/risk.cpp
  src/guard/rules.cpp
  src/chain/netreg.cpp
  src/chain/prices.cpp
  src/chain/service.cpp
  src/app/account_util.cpp
  src/app/autopilot_util.cpp
  src/app/bridge.cpp
  src/app/update.cpp
  src/app/link.cpp
  src/app/controller.cpp)
target_include_directories(tacklebox_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
                                                 ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(tacklebox_core PUBLIC dwarfkit dwarfkit_curl dk_trezor_crypto)

if(WIN32)
  target_link_libraries(tacklebox_core PUBLIC bcrypt)
endif()

# MinGW: fold the GCC runtime into the binary so the app runs without the
# toolchain's DLLs next to it (single-file portability).
if(MINGW)
  add_link_options(-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
endif()

# Warnings for our own code only.
function(tb_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:preprocessor)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  endif()
endfunction()
tb_warnings(tacklebox_core)

if(NOT TB_BUILD_FUZZERS)
# ---------------------------------------------------------------------------
# tacklebox - the application.
# ---------------------------------------------------------------------------
set(TB_APP_SOURCES
  src/main.cpp
  src/ui/app_ui.cpp
  src/ui/fx.cpp
  src/ui/layout.cpp
  src/ui/qa.cpp
  src/ui/texcache.cpp
  src/ui/theme.cpp
  src/ui/widgets.cpp
  src/ui/views/assets.cpp
  src/ui/views/autopilot.cpp
  src/ui/views/contracts.cpp
  src/ui/views/createaccount.cpp
  src/ui/views/dashboard.cpp
  src/ui/views/explore.cpp
  src/ui/views/governance.cpp
  src/ui/views/history.cpp
  src/ui/views/msig.cpp
  src/ui/views/onboarding.cpp
  src/ui/views/resources.cpp
  src/ui/views/settings.cpp
  src/ui/views/setup.cpp
  src/ui/views/shell.cpp
  src/ui/views/signmodal.cpp
  src/ui/views/transfer.cpp
  src/ui/views/unlock.cpp
  src/ui/views/vaultview.cpp
  src/ui/views/whitelist.cpp)

if(ANDROID)
  # SDL's Android activity loads the app as libmain.so.
  add_library(tacklebox SHARED ${TB_APP_SOURCES})
  set_target_properties(tacklebox PROPERTIES OUTPUT_NAME main)
else()
  add_executable(tacklebox ${TB_APP_SOURCES})
endif()
add_dependencies(tacklebox tb_embedded_assets)
target_include_directories(tacklebox PRIVATE ${TB_EMBED_DIR})
target_include_directories(tacklebox SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb)
target_link_libraries(tacklebox PRIVATE tacklebox_core imgui dk_qrcodegen)
tb_warnings(tacklebox)

if(TB_MOBILE)
  target_compile_definitions(tacklebox PRIVATE TB_MOBILE)
  target_compile_definitions(tacklebox_core PUBLIC TB_MOBILE)
endif()

if(WIN32)
  target_link_libraries(tacklebox PRIVATE opengl32 dwmapi)
  # Release builds are pure GUI apps; Debug keeps the console for logs.
  set_target_properties(tacklebox PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
elseif(ANDROID)
  # Built as a shared library loaded by SDL's Android activity
  # (platform/android). GLES comes from the NDK.
  target_link_libraries(tacklebox PRIVATE GLESv3 EGL android log)
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  target_link_libraries(tacklebox PRIVATE "-framework OpenGLES" "-framework UIKit")
  set_target_properties(tacklebox PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.tacklebox.wallet"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
elseif(APPLE)
  find_library(COCOA_LIB Cocoa)
  find_library(OPENGL_LIB OpenGL)
  find_library(IOKIT_LIB IOKit)
  target_link_libraries(tacklebox PRIVATE ${COCOA_LIB} ${OPENGL_LIB} ${IOKIT_LIB})
  set_target_properties(tacklebox PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "TackleBox"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.tacklebox.wallet"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(tacklebox PRIVATE OpenGL::GL)
endif()

endif()  # NOT TB_BUILD_FUZZERS (application)

# ---------------------------------------------------------------------------
# Fuzz harnesses: attacker-facing parsers (ESR uris, vault network json,
# sealed link messages). cmake -DTB_BUILD_FUZZERS=ON with clang, then run
# each binary with libFuzzer flags.
# ---------------------------------------------------------------------------
if(TB_BUILD_FUZZERS)
  function(tb_fuzzer name)
    add_executable(${name} fuzz/${name}.cpp)
    target_link_libraries(${name} PRIVATE tacklebox_core)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address)
  endfunction()
  tb_fuzzer(fuzz_esr)
  tb_fuzzer(fuzz_network_json)
  tb_fuzzer(fuzz_sealed)
endif()

# ---------------------------------------------------------------------------
# Tests (doctest comes vendored with dwarfkit).
# ---------------------------------------------------------------------------
if(TB_BUILD_TESTS)
  enable_testing()
  add_executable(tacklebox_tests
    tests/test_account.cpp
    tests/main.cpp
    tests/test_autopilot.cpp
    tests/test_cipher.cpp
    tests/test_deeplink.cpp
    tests/test_guard.cpp
    tests/test_kdf.cpp
    tests/test_link.cpp
    tests/test_prices.cpp
    tests/test_risk.cpp
    tests/test_runner.cpp
    tests/test_update.cpp
    tests/test_vault.cpp)
  target_link_libraries(tacklebox_tests PRIVATE tacklebox_core dk_doctest)
  tb_warnings(tacklebox_tests)
  add_test(NAME tacklebox_tests COMMAND tacklebox_tests)
endif()

# ---------------------------------------------------------------------------
# Install + packaging (CPack). Desktop only; the NSIS generator produces the
# Windows install wizard (EULA page from EULA.txt), ZIP the portable build.
# Branding hooks below are TODO until logo assets land.
# ---------------------------------------------------------------------------
if(NOT TB_MOBILE AND NOT ANDROID AND NOT TB_BUILD_FUZZERS)
  # Everything ships under the "app" component so the vendored projects'
  # own install rules (curl headers, etc.) stay out of the packages.
  install(TARGETS tacklebox RUNTIME DESTINATION . BUNDLE DESTINATION . COMPONENT app)
  install(FILES README.md EULA.txt LICENSE DESTINATION . COMPONENT app)
  install(DIRECTORY docs/ DESTINATION docs COMPONENT app)

  set(CPACK_PACKAGE_NAME "TackleBox")
  set(CPACK_PACKAGE_VENDOR "TackleBox contributors")
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "TackleBox - the toolkit for navigating Antelope blockchains")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/on-a-t-break/tacklebox")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "TackleBox")
  set(CPACK_PACKAGE_EXECUTABLES "tacklebox" "TackleBox")
  set(CPACK_CREATE_DESKTOP_LINKS "tacklebox")
  # The EULA page of the install wizard.
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/EULA.txt")
  set(CPACK_PACKAGE_FILE_NAME "tacklebox-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
  set(CPACK_COMPONENTS_ALL app)

  if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "TackleBox")
    set(CPACK_NSIS_PACKAGE_NAME "TackleBox")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/on-a-t-break/tacklebox")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    # Per-user install: no elevation, matches the per-user vault in %APPDATA%.
    set(CPACK_NSIS_DEFINES "RequestExecutionLevel user")
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA\\\\Programs")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    # The wallet registers tacklebox:/esr: url handlers (HKCU) at runtime;
    # uninstall removes our scheme, and esr: only when it still points here.
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
      DeleteRegKey HKCU 'Software\\\\Classes\\\\tacklebox'
      ReadRegStr $0 HKCU 'Software\\\\Classes\\\\esr\\\\shell\\\\open\\\\command' ''
      StrCmp $0 '\\\"$INSTDIR\\\\tacklebox.exe\\\" \\\"%1\\\"' 0 +2
      DeleteRegKey HKCU 'Software\\\\Classes\\\\esr'
    ")
    # TODO(branding): point these at the real art once the logo pack lands.
    #   set(CPACK_NSIS_MUI_ICON    ${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/tacklebox.ico)
    #   set(CPACK_NSIS_MUI_UNIICON ${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/tacklebox.ico)
    #   set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP .../wizard-side.bmp)  # 164x314 BMP
    #   set(CPACK_NSIS_MUI_HEADERIMAGE_BITMAP      .../wizard-header.bmp) # 150x57 BMP
  elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
  else()
    set(CPACK_GENERATOR "TGZ")
  endif()
  include(CPack)
endif()
