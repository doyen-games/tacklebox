# Console examples (BLUEPRINT.md section 4). These need the curl transport.
if(NOT TARGET dwarfkit_curl)
  message(STATUS "dwarfkit examples skipped: dwarfkit_curl is not built")
  return()
endif()

add_executable(transfer_privatekey transfer_privatekey.cpp)
target_link_libraries(transfer_privatekey PRIVATE dwarfkit dwarfkit_curl)
dk_warnings(transfer_privatekey)

add_executable(anchor_login anchor_login.cpp)
target_link_libraries(anchor_login PRIVATE dwarfkit dwarfkit_curl dk_qrcodegen)
dk_warnings(anchor_login)
