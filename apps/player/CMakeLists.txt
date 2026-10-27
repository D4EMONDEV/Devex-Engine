add_executable(devex_player
    src/main.cpp
)
add_executable(Devex::Player ALIAS devex_player)

target_link_libraries(devex_player PRIVATE Devex::Engine)
set_target_properties(devex_player PROPERTIES
    OUTPUT_NAME "devex-player"
    FOLDER "Applications"
)
devex_set_warnings(devex_player)
devex_set_application_icon(devex_player)
# An exported game is this program renamed, with the engine library beside it: the game looks for
# the library there first, then in lib/ next to bin/, as in a build or an installed engine.
if(UNIX AND NOT APPLE)
    set_target_properties(devex_player PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib"
    )
endif()
