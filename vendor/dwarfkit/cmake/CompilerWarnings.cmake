# Warnings-as-errors for dwarfkit's own targets. Never applied to third_party.
function(dk_warnings target)
  if(MSVC)
    # /Zc:preprocessor: conformant preprocessor, required for DK_FIELDS variadic macros
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor)
  else()
    # -Wno-missing-field-initializers: GCC 14+ fires it on designated
    # initializers of aggregates whose remaining members carry default member
    # initializers, which is the option-struct pattern used across the API.
    # -Wno-overlength-strings: the generated contract ABI headers embed json
    # blobs beyond the 64KB minimum the standard guarantees; every supported
    # compiler handles them, the pedantic warning is noise.
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror
                                             -Wno-missing-field-initializers
                                             -Wno-overlength-strings)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # Known GCC false positives at -O2/-O3 in inlined libstdc++ shared_ptr
      # internals (GCC bugs 96963, 105562 family), surfacing through nlohmann
      # json in dwarfkit TUs, plus version-specific pedantic errors:
      # -Wchanges-meaning (GCC 13 rejects APIResponse::json shadowing the
      # json alias; later GCCs accept it) and -Wformat-truncation (GCC 13
      # flags a provably-sized snprintf in time.cpp). Clang keeps the rest.
      target_compile_options(${target} PRIVATE -Wno-array-bounds
                                               -Wno-stringop-overflow
                                               -Wno-stringop-overread
                                               -Wno-format-truncation
                                               -Wno-changes-meaning)
    endif()
  endif()
endfunction()
