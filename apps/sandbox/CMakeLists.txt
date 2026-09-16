add_executable(devex_sandbox
    src/main.cpp
)
add_executable(Devex::Sandbox ALIAS devex_sandbox)

target_link_libraries(devex_sandbox PRIVATE Devex::Core)
set_target_properties(devex_sandbox PROPERTIES
    OUTPUT_NAME "devex-sandbox"
    FOLDER "Applications"
)
devex_set_warnings(devex_sandbox)
