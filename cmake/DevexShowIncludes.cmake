# Ninja reads header dependencies from the /showIncludes notes that MSVC prints, recognized by a
# prefix CMake detects once. Without the English language pack the prefix is localized with
# non-ASCII characters, whose bytes follow the console code page that other tools of the build
# switch to UTF-8, and which CMake cannot write into its Ninja rules when they are not valid UTF-8:
# Ninja then records no dependency at all, and changing a header rebuilds nothing. In that case the
# compiler runs through a launcher that rewrites the notes with the English prefix (see
# tools/ShowIncludesLauncher.cpp). The launcher is compiled while configuring, before any target.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR NOT CMAKE_GENERATOR MATCHES "^Ninja")
    return()
endif()

set(_devex_probe_directory "${CMAKE_BINARY_DIR}/CMakeFiles/DevexShowIncludes")
file(WRITE "${_devex_probe_directory}/devex_probe_header.h" "\n")
file(WRITE "${_devex_probe_directory}/devex_probe.cpp" "#include \"devex_probe_header.h\"\n")

# Sets the output variable to the prefix of the notes the command prints, or to nothing.
function(_devex_detect_prefix output)
    execute_process(
        COMMAND ${ARGN} /nologo /showIncludes /Zs devex_probe.cpp
        WORKING_DIRECTORY "${_devex_probe_directory}"
        OUTPUT_VARIABLE probe_output
        RESULT_VARIABLE probe_result
        ENCODING NONE
    )
    set(prefix "")
    if(probe_result EQUAL 0 AND probe_output MATCHES "\n([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)[A-Za-z]:")
        set(prefix "${CMAKE_MATCH_1}")
    endif()
    set(${output} "${prefix}" PARENT_SCOPE)
endfunction()

_devex_detect_prefix(_devex_prefix "${CMAKE_CXX_COMPILER}")
if(_devex_prefix MATCHES "^[ -~]+$")
    # An English compiler: CMake's own detection works.
    return()
endif()

set(_devex_launcher_source "${CMAKE_CURRENT_LIST_DIR}/tools/ShowIncludesLauncher.cpp")
set(_devex_launcher "${_devex_probe_directory}/devex_show_includes_launcher.exe")
if(NOT EXISTS "${_devex_launcher}" OR "${_devex_launcher_source}" IS_NEWER_THAN "${_devex_launcher}")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /O2 /W4 "${_devex_launcher_source}"
                "/Fe:${_devex_launcher}"
        WORKING_DIRECTORY "${_devex_probe_directory}"
        OUTPUT_VARIABLE _devex_launcher_output
        RESULT_VARIABLE _devex_launcher_result
    )
    if(NOT _devex_launcher_result EQUAL 0)
        message(WARNING "Cannot build ${_devex_launcher_source}: header changes may not rebuild "
                        "the files that include them.\n${_devex_launcher_output}")
        return()
    endif()
endif()

_devex_detect_prefix(_devex_prefix "${_devex_launcher}" "${CMAKE_CXX_COMPILER}")
if(_devex_prefix MATCHES "^Note: including file: +$")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_devex_launcher}")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file:")
else()
    message(WARNING "The localized /showIncludes notes of the compiler are not recognized: header "
                    "changes may not rebuild the files that include them.")
endif()
