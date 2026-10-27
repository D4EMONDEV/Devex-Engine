# Included by the package that game modules find, in a build of the engine or in an installed one.
#
# devex_add_game_module(SOURCES <files...>)
# Builds the game module, Game.dll or libGame.so, into bin/ of the build folder, where the editor and
# the player load it from.
function(devex_add_game_module)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "SOURCES")
    add_library(Game SHARED ${ARG_SOURCES})
    target_link_libraries(Game PRIVATE Devex::Engine)
    set_target_properties(Game PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(MSVC)
        # The classes of the engine hold members of the standard library, built the same way.
        target_compile_options(Game PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /wd4251 /wd4275)
    endif()
    # The module is loaded once the engine is, in the editor, the player and an exported game, beside
    # whose executable it then lies with the engine library: it looks for its libraries there only,
    # rather than in the engine folder of the machine it was built on.
    if(UNIX AND NOT APPLE)
        set_target_properties(Game PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endfunction()
