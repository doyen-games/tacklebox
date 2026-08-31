# find_package(Dwarfkit) install rules (BLUEPRINT.md Phase 8). The static
# libraries install under lib/, the dwarfkit + vendored dependency headers
# under include/, and a hand-rolled config wires the imported targets.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# secp256k1_precomputed is an object library; archive it for installation
add_library(dk_secp256k1_precomputed STATIC $<TARGET_OBJECTS:secp256k1_precomputed>)
set_target_properties(dk_secp256k1_precomputed PROPERTIES LINKER_LANGUAGE C
  OUTPUT_NAME secp256k1_precomputed)

install(FILES
  $<TARGET_FILE:dwarfkit>
  $<TARGET_FILE:dk_trezor_crypto>
  $<TARGET_FILE:dk_zlib>
  $<TARGET_FILE:secp256k1>
  $<TARGET_FILE:dk_secp256k1_precomputed>
  DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/dwarfkit
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/expected/include/tl
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/nlohmann/include/nlohmann
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
  ${PROJECT_SOURCE_DIR}/cmake/DwarfkitConfig.cmake.in
  ${PROJECT_BINARY_DIR}/DwarfkitConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dwarfkit
  PATH_VARS CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_INCLUDEDIR)

write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/DwarfkitConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${PROJECT_BINARY_DIR}/DwarfkitConfig.cmake
  ${PROJECT_BINARY_DIR}/DwarfkitConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dwarfkit)
