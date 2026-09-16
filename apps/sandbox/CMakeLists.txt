add_executable(devex_sandbox
    src/main.cpp
)
add_executable(Devex::Sandbox ALIAS devex_sandbox)

target_link_libraries(devex_sandbox PRIVATE Devex::Runtime)
# The sandbox opens its project from the source tree, so that asset changes are seen live.
target_compile_definitions(devex_sandbox
    PRIVATE DEVEX_SANDBOX_PROJECT="${CMAKE_CURRENT_SOURCE_DIR}/project/Sandbox.dvxproj"
)
set_target_properties(devex_sandbox PROPERTIES
    OUTPUT_NAME "devex-sandbox"
    FOLDER "Applications"
)
devex_set_warnings(devex_sandbox)
