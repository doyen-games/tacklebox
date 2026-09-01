# Warnings-as-errors for dwarfkit's own targets. Never applied to third_party.
function(dk_warnings target)
  if(MSVC)
    # /Zc:preprocessor: conformant preprocessor, required for DK_FIELDS variadic macros
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor)
  else()
    # -Wno-missing-field-initializers: GCC 14+ fires it on designated
    # initializers of aggregates whose remaining members carry default member
    # initializers, which is the option-struct pattern used across the API.
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror
                                             -Wno-missing-field-initializers)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # Known GCC false positives at -O2/-O3 in inlined libstdc++ shared_ptr
      # internals (GCC bugs 96963, 105562 family), surfacing through nlohmann
      # json in dwarfkit TUs. Clang keeps the full set.
      target_compile_options(${target} PRIVATE -Wno-array-bounds
                                               -Wno-stringop-overflow
                                               -Wno-stringop-overread
                                               -Wno-format-truncation)
    endif()
  endif()
endfunction()
