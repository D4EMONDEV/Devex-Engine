# The C# side of the engine: the API games use and the runtime the engine hosts, built into
# bin/managed with the .NET SDK. It compiles the views of the engine's components, which
# devex-bindgen generates from the reflection of this build.
set(DEVEX_MANAGED_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Devex.Managed/Devex.Managed.csproj")
file(GLOB_RECURSE DEVEX_MANAGED_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Devex.Managed/*.cs")
list(FILTER DEVEX_MANAGED_SOURCES EXCLUDE REGEX "/(bin|obj)/")
set(DEVEX_MANAGED_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/managed")
set(DEVEX_MANAGED_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/EngineComponents.g.cs")

add_custom_command(
    OUTPUT "${DEVEX_MANAGED_GENERATED}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND devex_bindgen "${DEVEX_MANAGED_GENERATED}"
    DEPENDS devex_bindgen
    COMMENT "Generating the C# views of the engine's components"
    VERBATIM
)
add_custom_command(
    OUTPUT "${DEVEX_MANAGED_DIRECTORY}/Devex.Managed.dll"
    COMMAND "${DEVEX_DOTNET}" build "${DEVEX_MANAGED_PROJECT}" -c Release -o "${DEVEX_MANAGED_DIRECTORY}"
            "-p:DevexGeneratedDirectory=${CMAKE_CURRENT_BINARY_DIR}/generated"
            "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/obj/"
            --nologo -v quiet
    DEPENDS ${DEVEX_MANAGED_SOURCES} "${DEVEX_MANAGED_PROJECT}" "${DEVEX_MANAGED_GENERATED}"
    COMMENT "Building the C# runtime of the engine"
    VERBATIM
)
add_custom_target(devex_managed DEPENDS "${DEVEX_MANAGED_DIRECTORY}/Devex.Managed.dll")
set_target_properties(devex_managed PROPERTIES FOLDER "Engine")
