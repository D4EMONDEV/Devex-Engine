# Compile the examples directly from the document, with diagnostics pointing back to its lines.
set(document "${PROJECT_SOURCE_DIR}/KNOWLEDGE.md")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${document}")
file(READ "${document}" knowledge)
string(REPLACE "\r\n" "\n" knowledge "${knowledge}")
set(examples_directory "${CMAKE_CURRENT_BINARY_DIR}/examples")
file(MAKE_DIRECTORY "${examples_directory}")

function(devex_extract_example name language)
    set(marker "<!-- devex-example: ${name} -->")
    string(REPLACE "." "\\." escaped_name "${name}")
    string(REGEX MATCHALL "<!-- devex-example: ${escaped_name} -->" occurrences "${knowledge}")
    list(LENGTH occurrences count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one documented example '${name}', found ${count}.")
    endif()
    set(opening "${marker}\n```${language}\n")
    string(FIND "${knowledge}" "${opening}" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "Expected a ${language} code fence immediately after ${marker}.")
    endif()
    string(LENGTH "${opening}" length)
    math(EXPR begin "${begin} + ${length}")
    string(SUBSTRING "${knowledge}" ${begin} -1 remainder)
    string(FIND "${remainder}" "\n```" end)
    if(end EQUAL -1)
        message(FATAL_ERROR "Unclosed code fence for documented example '${name}'.")
    endif()
    string(SUBSTRING "${remainder}" ${end} -1 closing)
    if(NOT closing MATCHES "^\n```(\n|$)")
        message(FATAL_ERROR "Expected a closing code fence for documented example '${name}'.")
    endif()
    string(SUBSTRING "${remainder}" 0 ${end} source)
    if(NOT language STREQUAL "cmake")
        string(SUBSTRING "${knowledge}" 0 ${begin} prefix)
        string(REGEX MATCHALL "\n" newlines "${prefix}")
        list(LENGTH newlines line)
        math(EXPR line "${line} + 1")
        set(source "#line ${line} \"${document}\"\n${source}")
    endif()
    # Only update changed files, substituting the source once to preserve its literal contents.
    file(CONFIGURE OUTPUT "${examples_directory}/${name}" CONTENT "@source@\n" @ONLY)
endfunction()

add_custom_target(devex_documentation_examples ALL)
set_target_properties(devex_documentation_examples PROPERTIES FOLDER "Tests/Documentation")
set(DEVEX_WARNINGS_AS_ERRORS ON)

function(devex_add_documented_module module source)
    devex_extract_example("${source}" cpp)
    add_library(devex_doc_${module} SHARED "${examples_directory}/${source}")
    target_link_libraries(devex_doc_${module} PRIVATE Devex::Engine)
    devex_set_warnings(devex_doc_${module})
    set_target_properties(devex_doc_${module} PROPERTIES FOLDER "Tests/Documentation")
    add_dependencies(devex_documentation_examples devex_doc_${module})
endfunction()

devex_add_documented_module(player PlayerModule.cpp)
devex_add_documented_module(gravity GravityModule.cpp)
devex_add_documented_module(particles FireParticlesModule.cpp)
devex_add_documented_module(dialogue DialogueModule.cpp)

devex_add_tests(documentation
    SOURCES ExamplesTests.cpp
    LIBRARIES Devex::Engine
    ENVIRONMENT SDL_VIDEO_DRIVER=dummy
)
add_dependencies(devex_documentation_tests devex_documentation_examples)
target_include_directories(devex_documentation_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(devex_documentation_tests PRIVATE
    DEVEX_DOC_PLAYER_MODULE="$<TARGET_FILE:devex_doc_player>"
    DEVEX_DOC_GRAVITY_MODULE="$<TARGET_FILE:devex_doc_gravity>"
    DEVEX_DOC_PARTICLES_MODULE="$<TARGET_FILE:devex_doc_particles>"
    DEVEX_DOC_DIALOGUE_MODULE="$<TARGET_FILE:devex_doc_dialogue>"
)

# Exercise the documented CMake project against the engine's game package, on both platforms.
devex_extract_example(CMakeLists.txt cmake)
add_test(NAME devex.documentation.configure
    COMMAND "${CMAKE_COMMAND}" -S "${examples_directory}" -B "${CMAKE_CURRENT_BINARY_DIR}/game"
        -G "${CMAKE_GENERATOR}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DDevex_DIR=${CMAKE_BINARY_DIR}/cmake"
)
set_tests_properties(devex.documentation.configure PROPERTIES
    LABELS documentation FIXTURES_SETUP documentation_project TIMEOUT 120)
add_test(NAME devex.documentation.build
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}/game" --config "$<CONFIG>"
)
set_tests_properties(devex.documentation.build PROPERTIES
    LABELS documentation FIXTURES_REQUIRED documentation_project TIMEOUT 120)

set(managed_examples)
foreach(example IN ITEMS PlayerController2D CarController Quest FollowCamera Health)
    devex_extract_example("${example}.cs" csharp)
    list(APPEND managed_examples "${examples_directory}/${example}.cs")
endforeach()

# Keep the downloadable scripts and the code pasted into the guide identical.
set(guide "${PROJECT_SOURCE_DIR}/docs/guide-demarrage-2d-3d.md")
file(GLOB guide_examples CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/examples/guide-demarrage/*.cs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${guide}" ${guide_examples})
file(READ "${guide}" guide_text)
string(REPLACE "\r\n" "\n" guide_text "${guide_text}")
foreach(example IN LISTS guide_examples)
    file(READ "${example}" source)
    string(REPLACE "\r\n" "\n" source "${source}")
    string(STRIP "${source}" source)
    string(FIND "${guide_text}" "\n```csharp\n${source}\n```\n" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "The code in '${guide}' no longer matches '${example}'.")
    endif()
endforeach()

if(DEVEX_DOTNET)
    set(managed_project "${CMAKE_CURRENT_SOURCE_DIR}/Examples.csproj")
    set(managed_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/managed-documentation")
    add_custom_command(
        OUTPUT "${managed_output}/DocumentationExamples.dll"
        COMMAND "${DEVEX_DOTNET}" build "${managed_project}" -c Release -o "${managed_output}"
            --nologo -v quiet "-p:DevexManagedDirectory=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/managed"
            "-p:DevexDocumentationDirectory=${examples_directory}"
            "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/obj/"
        DEPENDS ${managed_examples} ${guide_examples} "${managed_project}" devex_managed
        COMMENT "Compiling the C# examples of KNOWLEDGE.md and the getting-started guide"
        VERBATIM
    )
    add_custom_target(devex_documentation_managed
        DEPENDS "${managed_output}/DocumentationExamples.dll")
    set_target_properties(devex_documentation_managed PROPERTIES FOLDER "Tests/Documentation")
    add_dependencies(devex_documentation_examples devex_documentation_managed)
    target_compile_definitions(devex_documentation_tests PRIVATE
        DEVEX_DOC_MANAGED_GAME="${managed_output}/DocumentationExamples.dll")
endif()
