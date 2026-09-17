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
