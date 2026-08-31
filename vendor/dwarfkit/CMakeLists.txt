cmake_minimum_required(VERSION 3.24)
project(dwarfkit VERSION 0.1.0 LANGUAGES C CXX)

option(DK_WITH_CURL "Build the libcurl transport (dwarfkit_curl)" ON)
option(DK_BUILD_TESTS "Build dwarfkit_tests" ${PROJECT_IS_TOP_LEVEL})
option(DK_BUILD_TOOLS "Build dkgen" ${PROJECT_IS_TOP_LEVEL})
option(DK_BUILD_EXAMPLES "Build console examples" ${PROJECT_IS_TOP_LEVEL})
option(DK_LIVE_TESTS "Enable tests that hit live chain endpoints" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

include(cmake/CompilerWarnings.cmake)

add_subdirectory(third_party)
add_subdirectory(src)

# tools/, examples/ and adapters/ join the build as their phases land (BLUEPRINT.md section 10)
# dkgen precedes tests so the golden-output tests can link dkgen_lib
if(DK_BUILD_TOOLS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/dkgen/CMakeLists.txt)
  add_subdirectory(tools/dkgen)
endif()

if(DK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(DK_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt)
  add_subdirectory(examples)
endif()

if(PROJECT_IS_TOP_LEVEL)
  include(cmake/Install.cmake)
endif()
