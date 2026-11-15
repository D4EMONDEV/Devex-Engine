# Puts together the kit of the web player, with which an export for the web links the C++ of a game to the engine
# (DevexWebKitConfig.cmake.in): the libraries that the web player links, the headers of the engine and of GLM, the main of
# the player, and the WebGPU of Emscripten that Dawn gives.
#
# As the linker launcher of devex_player, it writes down the link, then makes it:
#   cmake -DRECORD=<file> -P DevexWebKit.cmake -- <link command...>
# Then, once the player is linked, it makes the kit from what it wrote down:
#   cmake -DRECORD=<file> -DKIT=<kit folder> -DSOURCE=<engine sources> -DGLM=<headers of GLM>
#         -DDAWN=<share/dawn of the Dawn for the web> -P DevexWebKit.cmake
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED KIT)
    set(_command "")
    set(_after FALSE)
    math(EXPR _last "${CMAKE_ARGC} - 1")
    foreach(_argument RANGE ${_last})
        if(_after)
            list(APPEND _command "${CMAKE_ARGV${_argument}}")
        elseif("${CMAKE_ARGV${_argument}}" STREQUAL "--")
            set(_after TRUE)
        endif()
    endforeach()
    # Run from the folder that the paths of the command are relative to.
    file(WRITE "${RECORD}" "set(DEVEX_PLAYER_LINK_DIRECTORY [==[${CMAKE_CURRENT_SOURCE_DIR}]==])\nset(DEVEX_PLAYER_LINK [==[${_command}]==])\n")
    execute_process(COMMAND ${_command} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "The link of the web player failed: ${_result}")
    endif()
    return()
endif()

if(NOT EXISTS "${RECORD}")
    message(FATAL_ERROR "The link of the web player is not written down in ${RECORD}: link devex_player again.")
endif()
include("${RECORD}")

# Each library goes to lib/ under its name; those of Emscripten (-lm) stay options. The object of the main is left out:
# the game compiles the main with its code.
file(MAKE_DIRECTORY "${KIT}/lib" "${KIT}/cmake")
set(_libraries "")
foreach(_argument IN LISTS DEVEX_PLAYER_LINK)
    if(_argument MATCHES "^-l")
        list(APPEND _libraries "${_argument}")
    elseif(_argument MATCHES "\\.a$")
        if(NOT IS_ABSOLUTE "${_argument}")
            set(_argument "${DEVEX_PLAYER_LINK_DIRECTORY}/${_argument}")
        endif()
        get_filename_component(_file "${_argument}" NAME)
        file(COPY_FILE "${_argument}" "${KIT}/lib/${_file}" ONLY_IF_DIFFERENT)
        list(APPEND _libraries "${_file}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _libraries)
if(NOT _libraries)
    message(FATAL_ERROR "The link of the web player in ${RECORD} has no library.")
endif()
set(_list "# What the web player links, in the order it does: the files of lib/ and the libraries of Emscripten.\n")
string(APPEND _list "# Written by DevexWebKit.cmake.\nset(DEVEX_WEB_LIBRARIES \"${_libraries}\")\n")
file(CONFIGURE OUTPUT "${KIT}/cmake/DevexWebLibraries.cmake" CONTENT "${_list}" @ONLY)

# The headers that game code includes, without those of an older engine; those of WebGPU, for the port that Emscripten
# builds from share/dawn.
file(REMOVE_RECURSE "${KIT}/include/devex")
file(COPY "${SOURCE}/engine/include/devex" "${GLM}/glm" DESTINATION "${KIT}/include")
get_filename_component(_prefix "${DAWN}/../.." ABSOLUTE)
file(COPY "${_prefix}/include/webgpu" "${_prefix}/include/dawn" DESTINATION "${KIT}/include")
file(COPY "${DAWN}/emdawnwebgpu.port.py" "${DAWN}/src" DESTINATION "${KIT}/share/dawn")
file(COPY "${SOURCE}/apps/player/src/main.cpp" DESTINATION "${KIT}/player")
file(COPY "${SOURCE}/cmake/DevexGameModule.cmake" DESTINATION "${KIT}/cmake")
