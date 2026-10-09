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

# devex_ship_slangc(<target> OUTPUT_DIRECTORY <directory>)
# Copies slangc and the libraries it loads into <directory>, where the editor finds it to compile
# the shaders of projects: slangc.exe beside its DLLs on Windows, bin/slangc and lib/ elsewhere.
function(devex_ship_slangc target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "OUTPUT_DIRECTORY" "")
    cmake_path(GET DEVEX_SLANGC PARENT_PATH slang_bin)
    if(WIN32)
        set(library_directory "${slang_bin}")
        set(binary_destination "${ARG_OUTPUT_DIRECTORY}")
        set(library_destination "${ARG_OUTPUT_DIRECTORY}")
        # The release libraries only: those ending in d are for debugging Slang itself.
        set(library_names slang slang-compiler slang-glsl-module slang-glslang slang-rt)
        set(libraries)
        foreach(name IN LISTS library_names)
            if(EXISTS "${slang_bin}/${name}.dll")
                list(APPEND libraries "${slang_bin}/${name}.dll")
            endif()
        endforeach()
    else()
        cmake_path(GET slang_bin PARENT_PATH slang_root)
        set(library_directory "${slang_root}/lib")
        set(binary_destination "${ARG_OUTPUT_DIRECTORY}/bin")
        set(library_destination "${ARG_OUTPUT_DIRECTORY}/lib")
        file(GLOB candidates "${library_directory}/libslang-compiler.so*" "${library_directory}/libslang-glsl*.so*"
                             "${library_directory}/libslang-rt.so*")
        # The files themselves, under the names their links give: copies of links would double them.
        set(libraries)
        foreach(candidate IN LISTS candidates)
            if(NOT IS_SYMLINK "${candidate}")
                list(APPEND libraries "${candidate}")
            endif()
        endforeach()
    endif()
    file(GLOB standard_modules LIST_DIRECTORIES true "${library_directory}/slang-standard-module-*")

    set(outputs "${binary_destination}/slangc${CMAKE_EXECUTABLE_SUFFIX}")
    set(commands
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${binary_destination}" "${library_destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DEVEX_SLANGC}" "${binary_destination}/")
    foreach(library IN LISTS libraries)
        cmake_path(GET library FILENAME name)
        list(APPEND outputs "${library_destination}/${name}")
        list(APPEND commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${library}" "${library_destination}/")
    endforeach()
    foreach(directory IN LISTS standard_modules)
        if(IS_DIRECTORY "${directory}")
            cmake_path(GET directory FILENAME name)
            list(APPEND commands COMMAND "${CMAKE_COMMAND}" -E copy_directory "${directory}" "${library_destination}/${name}")
        endif()
    endforeach()
    add_custom_command(
        OUTPUT ${outputs}
        ${commands}
        DEPENDS "${DEVEX_SLANGC}" ${libraries}
        COMMENT "Shipping slangc"
        VERBATIM
    )
    add_custom_target(${target} ALL DEPENDS ${outputs})
    set_target_properties(${target} PROPERTIES FOLDER "Shaders")
endfunction()

# devex_ship_shader_modules(<target> OUTPUT_DIRECTORY <directory> MODULES <files.slang...>)
# Copies the Slang modules that the shaders of projects import into <directory>.
function(devex_ship_shader_modules target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "OUTPUT_DIRECTORY" "MODULES")
    set(outputs)
    foreach(module IN LISTS ARG_MODULES)
        cmake_path(ABSOLUTE_PATH module OUTPUT_VARIABLE module_path)
        cmake_path(GET module FILENAME name)
        set(output "${ARG_OUTPUT_DIRECTORY}/${name}")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${module_path}" "${output}"
            DEPENDS "${module_path}"
            VERBATIM
        )
        list(APPEND outputs "${output}")
    endforeach()
    add_custom_target(${target} ALL DEPENDS ${outputs})
    set_target_properties(${target} PROPERTIES FOLDER "Shaders")
endfunction()
