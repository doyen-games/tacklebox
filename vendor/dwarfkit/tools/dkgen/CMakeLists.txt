# dkgen: the contract code generator (BLUEPRINT.md 7). dkgen_lib exists so the
# golden-output tests can call the generator directly.
add_library(dkgen_lib STATIC generator.cpp)
target_include_directories(dkgen_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dkgen_lib PUBLIC dwarfkit)
dk_warnings(dkgen_lib)

add_executable(dkgen main.cpp)
target_link_libraries(dkgen PRIVATE dkgen_lib)
if(TARGET dwarfkit_curl)
  target_link_libraries(dkgen PRIVATE dwarfkit_curl)
  target_compile_definitions(dkgen PRIVATE DK_WITH_CURL)
endif()
dk_warnings(dkgen)
