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
devex_set_application_icon(devex_editor)

if(WIN32)
    # Started from the desktop, the editor opens no terminal; its settings can ask for one.
    set_target_properties(devex_editor PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(devex_editor PRIVATE /ENTRY:mainCRTStartup)

    # A terminal does not wait for a windowed program, nor reads its exit code: this one is for scripts.
    add_executable(devex_editor_console
        src/main.cpp
    )
    target_link_libraries(devex_editor_console PRIVATE Devex::Engine)
    set_target_properties(devex_editor_console PROPERTIES
        OUTPUT_NAME "devex-editor-console"
        FOLDER "Applications"
    )
    devex_set_warnings(devex_editor_console)
    devex_set_application_icon(devex_editor_console)
    add_dependencies(devex_editor_console devex_editor)
endif()
