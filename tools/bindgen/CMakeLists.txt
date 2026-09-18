# Generates the C# views of the engine's components for Devex.Managed.
add_executable(devex_bindgen
    main.cpp
)
target_link_libraries(devex_bindgen PRIVATE Devex::Engine)
set_target_properties(devex_bindgen PROPERTIES
    OUTPUT_NAME "devex-bindgen"
    FOLDER "Tools"
)
devex_set_warnings(devex_bindgen)
