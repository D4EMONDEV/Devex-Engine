find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(devex_core_tests
    core/AssertTests.cpp
    core/BuildInfoTests.cpp
    core/ErrorTests.cpp
    core/LogTests.cpp
)

target_link_libraries(devex_core_tests PRIVATE Devex::Core Catch2::Catch2WithMain)
target_compile_definitions(devex_core_tests PRIVATE DEVEX_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")
set_target_properties(devex_core_tests PROPERTIES FOLDER "Tests")
devex_set_warnings(devex_core_tests)

catch_discover_tests(devex_core_tests
    TEST_PREFIX "devex.core."
    DISCOVERY_MODE PRE_TEST
)
