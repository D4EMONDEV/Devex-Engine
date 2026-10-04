#include "runtime/ManagedCodeBuilder.hpp"
#include "runtime/GameCodeBuilder.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <thread>

using devex::runtime::detail::ManagedCodeBuilder;

namespace {

struct ManagedProject
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-csharp-" + devex::core::Uuid::generate().toString());
    devex::asset::Project project;

    ManagedProject()
    {
        const auto created = devex::asset::createProject(root, "Managed test");
        REQUIRE(created);
        project = *created;
    }

    ~ManagedProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

} // namespace

TEST_CASE("New scripts use the chosen code folder without overwriting sources", "[runtime][filesystem]")
{
    const bool csharp = GENERATE(true, false);
    CAPTURE(csharp);
    const ManagedProject fixture;
    const auto create = [&](std::string_view name, std::string_view folder) {
        return csharp ? ManagedCodeBuilder::createScript(fixture.project, name, folder)
                      : devex::runtime::detail::GameCodeBuilder::createComponent(fixture.project, name, folder);
    };
    std::filesystem::create_directories(fixture.project.codeDirectory() / "Gameplay/Actors");
    const auto created = create("Player", "res://code/Gameplay/Actors");
    REQUIRE(created);
    CHECK(*created == fixture.project.codeDirectory() / "Gameplay/Actors" / (csharp ? "Player.cs" : "Player.cpp"));
    const auto text = devex::core::readTextFile(*created);
    REQUIRE(text);
    CHECK(text->contains("Player"));
    REQUIRE(devex::core::writeTextFile(*created, "// edited source\n"));
    CHECK_FALSE(create("Player", "res://code/Gameplay/Actors"));
    CHECK(*devex::core::readTextFile(*created) == "// edited source\n");
    CHECK_FALSE(create("Escaped", "res://assets"));
    CHECK_FALSE(create("Escaped", "res://code/../assets"));
    CHECK_FALSE(create("../Escaped", "res://code"));
    CHECK_FALSE(std::filesystem::exists(fixture.root / (csharp ? "Escaped.cs" : "Escaped.cpp")));
}

TEST_CASE("C# source discovery ignores build outputs and hidden directories", "[runtime][managed][build]")
{
    const ManagedProject fixture;
    for (const auto* directory : {"bin", "obj", ".devex", ".idea", "Nested/obj"})
    {
        REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / directory / "Generated.cs",
                                          "#error This is not a game source\n"));
    }
    CHECK_FALSE(ManagedCodeBuilder::hasCode(fixture.project));
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Gameplay" / "Player.cs", "class Player {}\n"));
    CHECK(ManagedCodeBuilder::hasCode(fixture.project));
}

#ifdef DEVEX_TEST_MANAGED_GAME
TEST_CASE("C# builds in an IDE and in the Devex cache can follow each other", "[runtime][managed][build]")
{
    const auto platform = GENERATE(std::string("AnyCPU"), std::string("x64"));
    CAPTURE(platform);
    const ManagedProject fixture;
    const auto managed = std::filesystem::path(DEVEX_TEST_MANAGED_GAME).parent_path().parent_path() / "managed";
    const auto generated = ManagedCodeBuilder::generatedDirectory(fixture.project);
    REQUIRE(devex::core::writeTextFile(generated / "GameComponents.g.cs", "public static class Generated { public const int Value = 42; }\n"));
    // Compilation needs both a real nested source and the explicitly included generated bindings.
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Gameplay" / "Player.cs",
                                      "public class Player : Devex.Component { public int Value = Generated.Value; }\n"));
    for (const auto* directory : {"bin", "obj", ".devex", ".idea", "Nested/obj"})
    {
        REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / directory / "NotASource.cs",
                                          "#error A build output was compiled as game code\n"));
    }

    ManagedCodeBuilder builder(fixture.project, managed);
    const auto initial = builder.buildAndWait();
    INFO(builder.message());
    REQUIRE(initial);

    // Both IDE builds and the x64 developer environment used by CI leave assembly attributes
    // under code/obj. Specify the platform so the test covers both layouts on every machine.
    const std::array<std::string, 7> arguments{"dotnet", "build", devex::core::toUtf8(fixture.project.codeDirectory() / "Game.csproj"),
                                              "-c", "Debug", "--nologo", "-p:Platform=" + platform};
    auto ide = devex::platform::Process::start(arguments, fixture.root);
    REQUIRE(ide);
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!ide->exitCode() && std::chrono::steady_clock::now() < deadline)
    {
        for (const auto& line : ide->readLines())
        {
            output += line + '\n';
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (const auto& line : ide->readLines())
    {
        output += line + '\n';
    }
    INFO(output);
    REQUIRE(ide->exitCode() == 0);
    auto objects = fixture.project.codeDirectory() / "obj";
    if (platform != "AnyCPU")
    {
        objects /= platform;
    }
    REQUIRE(std::filesystem::exists(objects / "Debug" / "Game.AssemblyInfo.cs"));

    const auto rebuilt = builder.buildAndWait();
    INFO(builder.message());
    REQUIRE(rebuilt);
    CHECK(std::filesystem::exists(builder.builtAssembly()));

    // New IDE cache files must neither count as sources nor request another Devex build.
    std::filesystem::last_write_time(fixture.project.codeDirectory() / "obj" / "NotASource.cs",
                                    std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    const ManagedCodeBuilder reopened(fixture.project, managed);
    CHECK_FALSE(reopened.needsBuild());
}
#endif
