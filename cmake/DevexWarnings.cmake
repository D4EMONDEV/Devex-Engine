function(devex_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor
            # The classes the engine library shares hold members of the standard library, which
            # every side builds with the same compiler and runtime.
            /wd4251
            /wd4275
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            # Designated initializers leave out the fields whose default member initializers fit,
            # which is how the engine fills its structures; GCC would report each of them.
            -Wno-missing-field-initializers
        )
        # GCC's guess that a value may be read uninitialized, once it optimizes, reports values
        # of std::optional read only after they were checked. MSVC warns of the real cases.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE -Wno-maybe-uninitialized)
        endif()
    endif()

    if(DEVEX_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
