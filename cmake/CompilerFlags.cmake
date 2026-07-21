# Compiler-specific C++20 and optimization flags

if(MSVC)
    # MSVC flags
    add_compile_options(
        /W4                 # Warning level 4
        /WX                 # Warnings as errors
        /permissive-        # Strict C++ conformance
        /Zc:__cplusplus     # Correct __cplusplus macro
    )

    # Release optimization
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob2>
    )

    # Debug info
    add_compile_options($<$<CONFIG:Debug>:/Zi>)

else()
    # GCC / Clang flags
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fstrict-aliasing
    )

    # C++20 specific
    add_compile_options(-std=c++20)

    # Release optimization
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
    )

    # Debug info
    add_compile_options($<$<CONFIG:Debug>:-g>)

    # Clang-specific
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()

    # GCC-specific
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    endif()
endif()

# Additional definitions
add_compile_definitions(
    $<$<CONFIG:Debug>:POLYIZON_DEBUG>
    $<$<CONFIG:Release>:POLYIZON_RELEASE>
)
