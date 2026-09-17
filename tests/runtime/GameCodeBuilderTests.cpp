#include "runtime/GameCodeBuilder.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using devex::runtime::detail::GameCodeBuilder;

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
