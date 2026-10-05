# What `cmake --install <build> --prefix <folder>` writes: an engine that works on its own, laid out as
# a build is, so that the editor builds game code and exports games from it as it does from a build.
#
#   bin/      the editor, the player, devex-bindgen and the libraries, with resources/, shaders/,
#             managed/ (the C# runtime) and, with Visual C++ in Release, the C++ runtime beside the
#             programs and in redist/ for exported games
#   lib/      the engine library on Linux, its import library on Windows
#   include/  the headers of the engine and of GLM, which they use
#   cmake/    the package that game modules are built against, with paths taken from its own place
#
# The CI packs it for every push; nothing in it refers to the sources or to the build it came from.

# The libraries a program uses are compared with the excluded folders once their paths are normalized.
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

set(_devex_programs devex_engine devex_bindgen)
foreach(program IN ITEMS devex_editor devex_player)
    if(TARGET ${program})
        list(APPEND _devex_programs ${program})
    endif()
endforeach()

if(WIN32)
    # The libraries vcpkg built, found beside the programs of the build; those of the system stay.
    install(TARGETS ${_devex_programs}
        RUNTIME_DEPENDENCY_SET devex_runtime_dependencies
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    install(RUNTIME_DEPENDENCY_SET devex_runtime_dependencies
        DESTINATION bin
        DIRECTORIES "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES "[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\]" "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]"
    )
else()
    # vcpkg links its libraries into the engine on Linux: only the system's are left to find.
    install(TARGETS ${_devex_programs}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
endif()

# The C++ runtime of Visual C++ (see the root CMakeLists.txt), beside the programs so that they start
# on a machine without it, and in redist/ for the games exported from the installed engine.
if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin)
    install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin/redist)
endif()

install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders" DESTINATION bin)
if(TARGET devex_managed)
    install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/managed" DESTINATION bin)
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/engine/include/devex" DESTINATION include)
# The engine found GLM in its own folder, where imported targets stay.
find_package(glm CONFIG REQUIRED)
get_target_property(_devex_glm_include glm::glm-header-only INTERFACE_INCLUDE_DIRECTORIES)
install(DIRECTORY "${_devex_glm_include}/glm" DESTINATION include)

install(FILES
    "${CMAKE_BINARY_DIR}/package/DevexConfig.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/DevexGameModule.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/DevexShowIncludes.cmake"
    DESTINATION cmake
)
install(FILES "${PROJECT_SOURCE_DIR}/cmake/tools/ShowIncludesLauncher.cpp" DESTINATION cmake/tools)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/README.md" DESTINATION .)
