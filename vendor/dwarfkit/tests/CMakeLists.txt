file(GLOB_RECURSE DK_TEST_SOURCES CONFIGURE_DEPENDS unit/*.test.cpp)

add_executable(dwarfkit_tests main.cpp util/mock_session.cpp ${DK_TEST_SOURCES})
target_link_libraries(dwarfkit_tests PRIVATE dwarfkit dk_doctest)
if(TARGET dkgen_lib)
  target_link_libraries(dwarfkit_tests PRIVATE dkgen_lib)
  target_include_directories(dwarfkit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(dwarfkit_tests PRIVATE DK_HAVE_DKGEN
    DK_GEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/gen")
endif()
# DOCTEST_CONFIG_USE_STD_HEADERS: doctest's forward declarations of std types
# conflict with the VS2022 17.14 STL
target_compile_definitions(dwarfkit_tests PRIVATE
  DOCTEST_CONFIG_USE_STD_HEADERS
  DK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(DK_LIVE_TESTS)
  target_compile_definitions(dwarfkit_tests PRIVATE DK_LIVE_TESTS=1)
  if(TARGET dwarfkit_curl)
    target_link_libraries(dwarfkit_tests PRIVATE dwarfkit_curl)
  endif()
endif()
dk_warnings(dwarfkit_tests)

add_test(NAME dwarfkit_tests COMMAND dwarfkit_tests)
