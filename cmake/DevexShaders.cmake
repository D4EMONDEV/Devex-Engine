find_program(DEVEX_SLANGC slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED
    DOC "Slang shader compiler from the Vulkan SDK"
)

# devex_add_shaders(<target> OUTPUT_DIRECTORY <directory> SOURCES <files.slang...>
#                   [MODULES <files.slang...>])
# Compiles every entry point of each source file into <directory>/<name>.spv at build time.
# Modules are only imported by sources; the depfiles rebuild the sources that import them.
function(devex_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "OUTPUT_DIRECTORY" "SOURCES;MODULES")

    set(outputs)
    foreach(source IN LISTS ARG_SOURCES)
        cmake_path(ABSOLUTE_PATH source OUTPUT_VARIABLE source_path)
        cmake_path(GET source STEM name)
        set(output "${ARG_OUTPUT_DIRECTORY}/${name}.spv")

        # Column-major matrices match GLM, so mul(matrix, vector) applies the CPU transforms.
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${DEVEX_SLANGC}" "${source_path}"
                -target spirv
                -fvk-use-entrypoint-name
                -matrix-layout-column-major
                "$<$<CONFIG:Debug>:-g>"
                -depfile "${output}.d"
                -o "${output}"
            DEPENDS "${source_path}"
            DEPFILE "${output}.d"
            COMMENT "Compiling shader ${name}.slang"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
        list(APPEND outputs "${output}")
    endforeach()

    add_custom_target(${target} ALL DEPENDS ${outputs} SOURCES ${ARG_SOURCES} ${ARG_MODULES})
    set_target_properties(${target} PROPERTIES FOLDER "Shaders")
endfunction()
