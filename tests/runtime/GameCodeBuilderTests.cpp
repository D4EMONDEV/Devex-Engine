#include "runtime/GameCodeBuilder.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

using devex::runtime::detail::GameCodeBuilder;

namespace {

struct TemporaryGameProject
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-code-" + devex::core::Uuid::generate().toString());
    devex::asset::Project project;
    const std::filesystem::path engineConfig = root / "engine" / "cmake";

    TemporaryGameProject()
    {
        auto created = devex::asset::createProject(root, "Coded");
        REQUIRE(created);
        project = *created;
        REQUIRE(devex::core::writeTextFile(engineConfig / "DevexConfig.cmake", "# test engine package\n"));
    }

    ~TemporaryGameProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    [[nodiscard]] std::filesystem::path cache() const
    {
        return project.cacheDirectory() / "code" / "Debug";
    }

    void createLegacyModule() const
    {
        const auto sourceTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(2);
        std::filesystem::last_write_time(project.codeDirectory() / "Game.cpp", sourceTime);
        std::filesystem::last_write_time(project.codeDirectory() / "CMakeLists.txt", sourceTime);
        const auto library = GameCodeBuilder::libraryPath(project, "Debug");
        REQUIRE(devex::core::writeTextFile(library, "legacy game module"));
        // Reproduces an untouched project whose obsolete DLL is newer than every source.
        std::filesystem::last_write_time(library, sourceTime + std::chrono::hours(1));
    }

    void createBuildFixture() const
    {
        // Exercise the real process/configure/build path without compiling C++ or loading a DLL.
        REQUIRE(devex::core::writeTextFile(project.codeDirectory() / "CMakeLists.txt", R"(
cmake_minimum_required(VERSION 3.25)
project(Game LANGUAGES NONE)
add_custom_target(Game ALL
    COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Build.cmake"
    VERBATIM)
)"));
        REQUIRE(devex::core::writeTextFile(project.codeDirectory() / "Game.cpp", "// keep my game source\n"));
        REQUIRE(devex::core::writeTextFile(project.codeDirectory() / "Build.cmake", R"(
file(READ "${CMAKE_CURRENT_LIST_DIR}/mode.txt" mode)
file(APPEND "${CMAKE_CURRENT_LIST_DIR}/attempts.txt" "${BUILD_DIRECTORY}\n")
if(mode STREQUAL "fail")
    message(FATAL_ERROR "controlled game build failure")
elseif(mode STREQUAL "missing")
    return()
elseif(mode STREQUAL "symbols-once" OR mode STREQUAL "symbols-always")
    if(mode STREQUAL "symbols-always" OR NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/symbols-retried.txt")
        file(WRITE "${CMAKE_CURRENT_LIST_DIR}/symbols-retried.txt" "retry")
        message(FATAL_ERROR "LINK : fatal error LNK1201: cannot write debug symbols")
    endif()
endif()
file(MAKE_DIRECTORY "${BUILD_DIRECTORY}/bin")
if(CMAKE_HOST_WIN32)
    file(WRITE "${BUILD_DIRECTORY}/bin/Game.dll" "updated game module")
else()
    file(WRITE "${BUILD_DIRECTORY}/bin/libGame.so" "updated game module")
endif()
)"));
        std::filesystem::last_write_time(project.codeDirectory() / "Build.cmake",
                                        std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));
        setBuildMode("success");
    }

    void setBuildMode(std::string_view mode) const
    {
        REQUIRE(devex::core::writeTextFile(project.codeDirectory() / "mode.txt", mode));
    }
};

[[nodiscard]] bool finishBuild(GameCodeBuilder& builder)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (builder.update())
        {
            return true;
        }
        if (builder.state() == GameCodeBuilder::State::Failed)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL_CHECK("the fixture game build did not finish within 30 seconds");
    return false;
}

} // namespace

TEST_CASE("New game code gets a CMake project and an example system", "[runtime][game]")
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-code-" + devex::core::Uuid::generate().toString());
    {
        const devex::asset::Project project = *devex::asset::createProject(root, "Coded");
        CHECK_FALSE(GameCodeBuilder::hasCode(project));
        REQUIRE(GameCodeBuilder::createCode(project));
        CHECK(GameCodeBuilder::hasCode(project));
        CHECK_FALSE(GameCodeBuilder::createCode(project).has_value());

        const std::string cmake = *devex::core::readTextFile(project.codeDirectory() / "CMakeLists.txt");
        CHECK(cmake.find("find_package(Devex CONFIG REQUIRED)") != std::string::npos);
        CHECK(cmake.find("devex_add_game_module(") != std::string::npos);
        const std::string game = *devex::core::readTextFile(project.codeDirectory() / "Game.cpp");
        CHECK(game.find("DEVEX_GAME_MODULE(game)") != std::string::npos);

        // Builds go to the project's cache, which is not versioned.
        const std::filesystem::path library = GameCodeBuilder::libraryPath(project);
        CHECK(library.lexically_relative(project.cacheDirectory()).string().starts_with("code"));
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("The first C++ script creates its native project without overwriting sources",
          "[runtime][game][cppproject]")
{
    const TemporaryGameProject fixture;
    std::filesystem::create_directories(fixture.project.codeDirectory() / "Actors");
    const auto created =
        GameCodeBuilder::createComponent(fixture.project, "Player", "res://code/Actors");
    REQUIRE(created);
    CHECK(GameCodeBuilder::hasCode(fixture.project));
    const auto cmake =
        devex::core::readTextFile(fixture.project.codeDirectory() / "CMakeLists.txt");
    REQUIRE(cmake);
    CHECK(cmake->contains("CONFIGURE_DEPENDS"));
    CHECK(cmake->contains("DevexProject.cmake"));
    CHECK(cmake->contains("CMakeFiles"));
    const auto module = devex::core::readTextFile(fixture.project.codeDirectory() / "Game.cpp");
    REQUIRE(module);
    CHECK(module->contains("DEVEX_GAME_MODULE(game)"));
    CHECK_FALSE(module->contains("Spinner"));
    REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig, "Debug"));
    const auto context = fixture.project.cacheDirectory() / "code/DevexProject.cmake";
    const auto text = devex::core::readTextFile(context);
    REQUIRE(text);
    CHECK(text->contains("Devex_DIR"));
    CHECK(text->contains("Debug"));
    const auto sourceText = *devex::core::readTextFile(*created);
    const auto sourceTime = std::filesystem::last_write_time(*created);
    const auto cmakeTime =
        std::filesystem::last_write_time(fixture.project.codeDirectory() / "CMakeLists.txt");
    const auto contextTime = std::filesystem::last_write_time(context);
    REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig, "Debug"));
    CHECK(*devex::core::readTextFile(*created) == sourceText);
    CHECK(std::filesystem::last_write_time(*created) == sourceTime);
    CHECK(std::filesystem::last_write_time(fixture.project.codeDirectory() / "CMakeLists.txt") ==
          cmakeTime);
    CHECK(std::filesystem::last_write_time(context) == contextTime);
    // A Windows checkout keeps the generated CMakeLists.txt in CRLF: unchanged all the same.
    const std::filesystem::path lists = fixture.project.codeDirectory() / "CMakeLists.txt";
    std::string crlf;
    for (const char character : *devex::core::readTextFile(lists))
    {
        crlf += character == '\n' ? std::string("\r\n") : std::string(1, character);
    }
    REQUIRE(devex::core::writeTextFile(lists, crlf));
    REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig, "Debug"));
    CHECK(*devex::core::readTextFile(lists) == crlf);
    REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.root / "new engine/cmake",
                                                 "Release"));
    CHECK(devex::core::readTextFile(context)->contains("new engine/cmake"));
    CHECK(devex::core::readTextFile(context)->contains("Release"));
    CHECK(*devex::core::readTextFile(*created) == sourceText);
    CHECK(*devex::core::readTextFile(fixture.project.codeDirectory() / "Game.cpp") == *module);
}

TEST_CASE("Native project discovery ignores IDE outputs and leaves C#-only projects alone",
          "[runtime][game][cppproject]")
{
    const TemporaryGameProject fixture;
    for (const auto directory :
         {".idea", ".devex", "obj", "bin", "build", "out", "CMakeFiles", "cmake-build-debug"})
        REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / directory /
                                               "CompilerId.cpp",
                                           "// not a game source\n"));
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Player.cs",
                                       "class Player {}\n"));
    CHECK_FALSE(GameCodeBuilder::hasCode(fixture.project));
    REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig));
    CHECK_FALSE(std::filesystem::exists(fixture.project.codeDirectory() / "CMakeLists.txt"));
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Actors/Player.cc",
                                       "// DEVEX_GAME_MODULE(fake)\n"));
    CHECK(GameCodeBuilder::hasCode(fixture.project));
    const GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");
    CHECK(builder.pending());
    CHECK(std::filesystem::exists(fixture.project.codeDirectory() / "CMakeLists.txt"));
    CHECK(devex::core::readTextFile(fixture.project.codeDirectory() / "Game.cpp")
              ->contains("DEVEX_GAME_MODULE(game)"));
}

TEST_CASE("C++ project setup preserves custom CMake and existing module entry points",
          "[runtime][game][cppproject]")
{
    const TemporaryGameProject fixture;
    SECTION("An untouched older generated project is updated without changing its source")
    {
        REQUIRE(GameCodeBuilder::createCode(fixture.project));
        const auto source = fixture.project.codeDirectory() / "Game.cpp";
        const auto original = *devex::core::readTextFile(source);
        const auto modified = std::filesystem::last_write_time(source);
        const auto cmake = fixture.project.codeDirectory() / "CMakeLists.txt";
        REQUIRE(devex::core::writeTextFile(
            cmake,
            R"(# The game module of the project, built by the Devex editor whenever a file of this folder changes.
cmake_minimum_required(VERSION 3.25)
project(Game LANGUAGES CXX)

find_package(Devex CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
devex_add_game_module(SOURCES ${sources})
)"));
        REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig));
        CHECK(devex::core::readTextFile(cmake)->contains("DevexProject.cmake"));
        CHECK(*devex::core::readTextFile(source) == original);
        CHECK(std::filesystem::last_write_time(source) == modified);
    }
    SECTION("A custom CMakeLists remains byte-for-byte unchanged")
    {
        const std::string custom =
            "# my build\ncmake_minimum_required(VERSION 3.25)\nproject(Custom)\n";
        REQUIRE(
            devex::core::writeTextFile(fixture.project.codeDirectory() / "CMakeLists.txt", custom));
        REQUIRE(GameCodeBuilder::createComponent(fixture.project, "Player"));
        REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig));
        CHECK(*devex::core::readTextFile(fixture.project.codeDirectory() / "CMakeLists.txt") ==
              custom);
        CHECK_FALSE(std::filesystem::exists(fixture.project.codeDirectory() / "Game.cpp"));
    }
    SECTION("An edited generated CMakeLists is also custom")
    {
        REQUIRE(GameCodeBuilder::createCode(fixture.project));
        const auto file = fixture.project.codeDirectory() / "CMakeLists.txt";
        const auto custom = *devex::core::readTextFile(file) + "\n# My extra libraries\n";
        REQUIRE(devex::core::writeTextFile(file, custom));
        REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig));
        CHECK(*devex::core::readTextFile(file) == custom);
    }
    SECTION("An entry point with another filename is not duplicated")
    {
        const std::string main =
            "#include <devex/runtime/Game.hpp>\nDEVEX_GAME_MODULE(myGame) {}\n";
        REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Main.cc", main));
        REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig));
        CHECK_FALSE(std::filesystem::exists(fixture.project.codeDirectory() / "Game.cpp"));
        CHECK(*devex::core::readTextFile(fixture.project.codeDirectory() / "Main.cc") == main);
    }
    SECTION("A component named like Game.cpp does not collide with the generated entry point")
    {
        std::string name = "Game";
        SECTION("The same spelling") {}
        SECTION("A different case") { name = "game"; }
        const auto file = GameCodeBuilder::createComponent(fixture.project, name);
        REQUIRE(file);
        CHECK(devex::core::readTextFile(*file)->contains("struct " + name));
        CHECK(devex::core::readTextFile(fixture.project.codeDirectory() / "DevexModule.cpp")
                  ->contains("DEVEX_GAME_MODULE(game)"));
    }
    SECTION("An existing Game.cpp without an entry point is preserved")
    {
        const std::string source =
            "// DEVEX_GAME_MODULE(example)\nconst char* text = \"DEVEX_GAME_MODULE(fake)\";\n";
        REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Game.cpp", source));
        REQUIRE(GameCodeBuilder::prepareProjectFiles(fixture.project, fixture.engineConfig));
        CHECK(*devex::core::readTextFile(fixture.project.codeDirectory() / "Game.cpp") == source);
        CHECK(std::filesystem::exists(fixture.project.codeDirectory() / "DevexModule.cpp"));
        CHECK_FALSE(GameCodeBuilder::createCode(fixture.project));
    }
}

TEST_CASE("Generated CMake tracks added and deleted sources without including IDE caches",
          "[runtime][game][cppproject][build]")
{
    const TemporaryGameProject fixture;
    REQUIRE(GameCodeBuilder::createComponent(fixture.project, "Player"));
    REQUIRE(devex::core::writeTextFile(fixture.engineConfig / "DevexConfig.cmake", R"(
function(devex_add_game_module)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "SOURCES")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sources.txt" "${ARG_SOURCES}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    if(WIN32)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bin/Game.dll" "fixture module")
    else()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bin/libGame.so" "fixture module")
    endif()
    add_custom_target(Game ALL)
endfunction()
)"));
    for (const auto directory :
         {".idea", "obj", "bin", "build", "out", "CMakeFiles", "cmake-build-debug"})
        REQUIRE(devex::core::writeTextFile(
            fixture.project.codeDirectory() / directory / "CompilerId.cpp", "#error cache\n"));
    GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");
    REQUIRE(builder.buildAndWait());
    const auto build =
        GameCodeBuilder::libraryPath(fixture.project, "Debug").parent_path().parent_path();
    auto sources = *devex::core::readTextFile(build / "sources.txt");
    CHECK(sources.contains("Game.cpp"));
    CHECK(sources.contains("Player.cpp"));
    CHECK_FALSE(sources.contains("CompilerId.cpp"));
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Actors/Enemy.cxx",
                                       "// new source\n"));
    REQUIRE(builder.buildAndWait());
    sources = *devex::core::readTextFile(build / "sources.txt");
    CHECK(sources.contains("Actors/Enemy.cxx"));
    REQUIRE(std::filesystem::remove(fixture.project.codeDirectory() / "Player.cpp"));
    REQUIRE(builder.buildAndWait());
    sources = *devex::core::readTextFile(build / "sources.txt");
    CHECK_FALSE(sources.contains("Player.cpp"));
}

TEST_CASE("A first generated C++ script builds against the running engine package",
          "[runtime][game][cppproject][build]")
{
    const TemporaryGameProject fixture;
    REQUIRE(GameCodeBuilder::createComponent(fixture.project, "Player"));
    const auto config =
        std::filesystem::path(DEVEX_TEST_GAME_MODULE).parent_path().parent_path() / "cmake";
    REQUIRE(std::filesystem::is_regular_file(config / "DevexConfig.cmake"));
    GameCodeBuilder builder(fixture.project, config);
    const auto built = builder.buildAndWait();
    INFO(builder.message());
    REQUIRE(built);
    CHECK(std::filesystem::is_regular_file(GameCodeBuilder::libraryPath(fixture.project)));
}

TEST_CASE("Game code status distinguishes an initial build from an engine update", "[runtime][game]")
{
    const TemporaryGameProject fixture;
    CHECK_FALSE(GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug").needsBuild);
    REQUIRE(GameCodeBuilder::createCode(fixture.project));

    const auto status = GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug");
    CHECK(status.needsBuild);
    CHECK_FALSE(status.needsUpdate);
    CHECK_FALSE(status.message.empty());
    CHECK(status.reason == "it was never built");
    const GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");
    CHECK(builder.pending());
}

TEST_CASE("A build tells how far it is from the steps Ninja counts", "[runtime][game]")
{
    CHECK(GameCodeBuilder::stepsOf("[1/4] Building CXX object CMakeFiles/Game.dir/Game.cpp.o") == 0.25f);
    CHECK(GameCodeBuilder::stepsOf("[0/2] Re-checking globbed directories...") == 0.0f);
    CHECK(GameCodeBuilder::stepsOf("[3/3] Linking CXX shared library bin/libGame.so") == 1.0f);
    CHECK_FALSE(GameCodeBuilder::stepsOf("-- Configuring done (0.1s)").has_value());
    CHECK_FALSE(GameCodeBuilder::stepsOf("[5/3] more done than there is").has_value());
    CHECK_FALSE(GameCodeBuilder::stepsOf("[a/3] not a count").has_value());
    CHECK_FALSE(GameCodeBuilder::stepsOf("[2/0]").has_value());
}

TEST_CASE("An obsolete game module needs updating even when its sources have not changed", "[runtime][game]")
{
    const TemporaryGameProject fixture;
    REQUIRE(GameCodeBuilder::createCode(fixture.project));
    fixture.createLegacyModule();
    const auto source = fixture.project.codeDirectory() / "Game.cpp";
    const auto sourceText = *devex::core::readTextFile(source);
    const auto sourceTime = std::filesystem::last_write_time(source);

    SECTION("Legacy builds have no engine metadata")
    {
        CHECK_FALSE(std::filesystem::exists(fixture.cache() / "devex-engine.txt"));
    }
    SECTION("Older builds carry obsolete engine metadata")
    {
        REQUIRE(devex::core::writeTextFile(fixture.cache() / "devex-engine.txt", "old engine API\n"));
    }

    const auto status = GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug");
    CHECK(status.needsBuild);
    CHECK(status.needsUpdate);
    CHECK_FALSE(status.message.empty());
    const GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");
    CHECK(builder.pending());
    CHECK(*devex::core::readTextFile(source) == sourceText);
    CHECK(std::filesystem::last_write_time(source) == sourceTime);
}

TEST_CASE("An engine update publishes its new game module only after a successful build", "[runtime][game][build]")
{
    const TemporaryGameProject fixture;
    fixture.createBuildFixture();
    fixture.createLegacyModule();
    const auto legacy = GameCodeBuilder::libraryPath(fixture.project, "Debug");
    const auto source = fixture.project.codeDirectory() / "Game.cpp";
    const auto sourceTime = std::filesystem::last_write_time(source);
    GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");

    REQUIRE(builder.pending());
    CHECK_FALSE(builder.update());
    REQUIRE(builder.state() == GameCodeBuilder::State::Building);
    CHECK(GameCodeBuilder::libraryPath(fixture.project, "Debug") == legacy);
    CHECK_FALSE(std::filesystem::exists(fixture.cache() / "active-build.txt"));

    REQUIRE(finishBuild(builder));
    CHECK_FALSE(builder.pending());
    CHECK(builder.state() == GameCodeBuilder::State::Succeeded);
    const auto updated = GameCodeBuilder::libraryPath(fixture.project, "Debug");
    CHECK(updated != legacy);
    CHECK(*devex::core::readTextFile(updated) == "updated game module");
    CHECK(*devex::core::readTextFile(legacy) == "legacy game module");
    CHECK(std::filesystem::exists(updated.parent_path().parent_path() / "devex-engine.txt"));
    CHECK_FALSE(GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug").needsBuild);
    const GameCodeBuilder reopened(fixture.project, fixture.engineConfig, "Debug");
    CHECK_FALSE(reopened.pending());
    CHECK(*devex::core::readTextFile(source) == "// keep my game source\n");
    CHECK(std::filesystem::last_write_time(source) == sourceTime);
}

TEST_CASE("Manual C++ compilation waits for a build request after saved edits", "[runtime][game][build][scripts]")
{
    const TemporaryGameProject fixture;
    fixture.createBuildFixture();
    GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");
    REQUIRE(builder.buildAndWait());
    const auto attempts = fixture.project.codeDirectory() / "attempts.txt";
    const std::string previous = *devex::core::readTextFile(attempts);
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Game.cpp", "// changed\n"));
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
    while (std::chrono::steady_clock::now() < until)
    {
        REQUIRE_FALSE(builder.update(false));
        REQUIRE_FALSE(builder.pending());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(builder.hasSourceChanges());
    CHECK(*devex::core::readTextFile(attempts) == previous);
    builder.requestBuild();
    REQUIRE_FALSE(builder.update(false));
    REQUIRE(builder.state() == GameCodeBuilder::State::Building);
    REQUIRE(finishBuild(builder));
    CHECK_FALSE(builder.hasSourceChanges());
    CHECK(*devex::core::readTextFile(attempts) != previous);
}

TEST_CASE("Changed engine packages and build configurations make game builds pending", "[runtime][game][build]")
{
    const TemporaryGameProject fixture;
    fixture.createBuildFixture();
    GameCodeBuilder initial(fixture.project, fixture.engineConfig, "Debug");
    REQUIRE(finishBuild(initial));
    REQUIRE_FALSE(GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug").needsBuild);

    std::filesystem::path config = fixture.engineConfig;
    std::string configuration = "Debug";
    bool needsUpdate = true;
    SECTION("Engine package settings changed at the same location")
    {
        REQUIRE(devex::core::writeTextFile(config / "DevexConfig.cmake", "# changed engine package\n"));
    }
    SECTION("The same package moved to another engine")
    {
        config = fixture.root / "other-engine" / "cmake";
        REQUIRE(devex::core::writeTextFile(config / "DevexConfig.cmake", "# test engine package\n"));
    }
    SECTION("The engine binary was rebuilt")
    {
#ifdef _WIN32
        REQUIRE(devex::core::writeTextFile(config / ".." / "bin" / "devex-engine.dll", "rebuilt engine"));
#else
        REQUIRE(devex::core::writeTextFile(config / ".." / "lib" / "libdevex-engine.so", "rebuilt engine"));
#endif
    }
    SECTION("Another build configuration has no matching module")
    {
        configuration = "Release";
        needsUpdate = false;
    }

    const auto status = GameCodeBuilder::buildStatus(fixture.project, config, configuration);
    CHECK(status.needsBuild);
    CHECK(status.needsUpdate == needsUpdate);
    const GameCodeBuilder reopened(fixture.project, config, configuration);
    CHECK(reopened.pending());
}

TEST_CASE("Failed engine updates preserve the last working game build", "[runtime][game][build]")
{
    const TemporaryGameProject fixture;
    fixture.createBuildFixture();
    fixture.createLegacyModule();
    GameCodeBuilder initial(fixture.project, fixture.engineConfig, "Debug");
    REQUIRE(finishBuild(initial));
    const auto working = GameCodeBuilder::libraryPath(fixture.project, "Debug");
    const auto stampPath = working.parent_path().parent_path() / "devex-engine.txt";
    const auto stamp = *devex::core::readTextFile(stampPath);
    const auto active = *devex::core::readTextFile(fixture.cache() / "active-build.txt");
    REQUIRE(devex::core::writeTextFile(fixture.engineConfig / "DevexConfig.cmake", "# updated engine package\n"));

    SECTION("A compiler or linker error prevents publication")
    {
        fixture.setBuildMode("fail");
    }
    SECTION("A successful process without a module prevents publication")
    {
        fixture.setBuildMode("missing");
    }

    GameCodeBuilder update(fixture.project, fixture.engineConfig, "Debug");
    REQUIRE(update.pending());
    CHECK_FALSE(finishBuild(update));
    CHECK(update.state() == GameCodeBuilder::State::Failed);
    CHECK_FALSE(update.pending());
    CHECK_FALSE(update.message().empty());
    CHECK(GameCodeBuilder::libraryPath(fixture.project, "Debug") == working);
    CHECK(*devex::core::readTextFile(working) == "updated game module");
    CHECK(*devex::core::readTextFile(stampPath) == stamp);
    CHECK(*devex::core::readTextFile(fixture.cache() / "active-build.txt") == active);
    CHECK(GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug").needsUpdate);
    const GameCodeBuilder reopened(fixture.project, fixture.engineConfig, "Debug");
    CHECK(reopened.pending());
}

TEST_CASE("Debug symbol write failures get only one retry in a fresh build folder", "[runtime][game][build]")
{
    const TemporaryGameProject fixture;
    fixture.createBuildFixture();
    fixture.createLegacyModule();
    bool succeeds = true;
    SECTION("The fresh debug symbol file resolves the failure")
    {
        fixture.setBuildMode("symbols-once");
    }
    SECTION("A persistent debug symbol failure stops after the retry")
    {
        fixture.setBuildMode("symbols-always");
        succeeds = false;
    }

    GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");
    CHECK(finishBuild(builder) == succeeds);
    CHECK_FALSE(builder.pending());
    const auto attemptsRead = devex::core::readTextFile(fixture.project.codeDirectory() / "attempts.txt");
    REQUIRE(attemptsRead);
    const auto& attempts = *attemptsRead;
    const auto firstEnd = attempts.find('\n');
    REQUIRE(firstEnd != std::string::npos);
    const auto secondEnd = attempts.find('\n', firstEnd + 1);
    REQUIRE(secondEnd != std::string::npos);
    CHECK(secondEnd + 1 == attempts.size());
    CHECK(attempts.substr(0, firstEnd) != attempts.substr(firstEnd + 1, secondEnd - firstEnd - 1));
    CHECK(GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug").needsUpdate == !succeeds);
}
