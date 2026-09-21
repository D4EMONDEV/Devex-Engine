#include "runtime/GameCodeBuilder.hpp"

#include <devex/asset/Project.hpp>
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

#ifdef _WIN32
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
file(WRITE "${BUILD_DIRECTORY}/bin/Game.dll" "updated game module")
)"));
        std::filesystem::last_write_time(project.codeDirectory() / "Build.cmake",
                                        std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));
        setBuildMode("success");
    }

    void setBuildMode(std::string_view mode) const
    {
        REQUIRE(devex::core::writeTextFile(project.codeDirectory() / "mode.txt", mode));
    }
#endif
};

#ifdef _WIN32
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
#endif

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

TEST_CASE("Game code status distinguishes an initial build from an engine update", "[runtime][game]")
{
    const TemporaryGameProject fixture;
    CHECK_FALSE(GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug").needsBuild);
    REQUIRE(GameCodeBuilder::createCode(fixture.project));

    const auto status = GameCodeBuilder::buildStatus(fixture.project, fixture.engineConfig, "Debug");
    CHECK(status.needsBuild);
    CHECK_FALSE(status.needsUpdate);
    CHECK_FALSE(status.message.empty());
    const GameCodeBuilder builder(fixture.project, fixture.engineConfig, "Debug");
    CHECK(builder.pending());
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

#ifdef _WIN32
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
        REQUIRE(devex::core::writeTextFile(config / ".." / "bin" / "devex-engine.dll", "rebuilt engine"));
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
#endif
