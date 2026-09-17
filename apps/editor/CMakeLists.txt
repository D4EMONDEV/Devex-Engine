add_executable(devex_editor
    src/main.cpp
)
add_executable(Devex::Editor ALIAS devex_editor)

target_link_libraries(devex_editor PRIVATE Devex::Engine)
set_target_properties(devex_editor PROPERTIES
    OUTPUT_NAME "devex-editor"
    FOLDER "Applications"
)
devex_set_warnings(devex_editor)
