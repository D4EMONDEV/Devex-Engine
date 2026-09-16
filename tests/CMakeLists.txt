find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# devex_add_tests(<module> SOURCES <files...> LIBRARIES <targets...>)
# Builds devex_<module>_tests and registers each Catch2 test case with CTest.
function(devex_add_tests module)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES")
    set(target devex_${module}_tests)

    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES} Catch2::Catch2WithMain)
    set_target_properties(${target} PROPERTIES FOLDER "Tests")
    devex_set_warnings(${target})

    # The dummy SDL video driver lets windows be created without a display.
    catch_discover_tests(${target}
        TEST_PREFIX "devex.${module}."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy"
    )
endfunction()

devex_add_tests(core
    SOURCES
        core/AssertTests.cpp
        core/BuildInfoTests.cpp
        core/ErrorTests.cpp
        core/LogTests.cpp
    LIBRARIES
        Devex::Core
)
target_compile_definitions(devex_core_tests PRIVATE DEVEX_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")

devex_add_tests(platform
    SOURCES
        platform/InputTests.cpp
    LIBRARIES
        Devex::Platform
)

devex_add_tests(runtime
    SOURCES
        runtime/ApplicationTests.cpp
        runtime/FixedTimestepTests.cpp
    LIBRARIES
        Devex::Runtime
)
