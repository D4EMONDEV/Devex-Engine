#include "tools/ScriptSettings.hpp"

#include <devex/core/Path.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace devex::tools::detail;

namespace {
struct ScriptDirectory
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-ide-" + devex::core::Uuid::generate().toString());
    ~ScriptDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
} // namespace

TEST_CASE("Script editor preferences survive serialization independently for each language", "[tools][scripts]")
{
    ScriptSettings settings;
    settings.automaticCompilation = false;
    settings.editors[0] = {ScriptEditor::Custom, "C:/Program Files/Editor/rider64.exe", "\"{project}\" \"{file}\""};
    settings.editors[1].editor = ScriptEditor::System;
    settings.editors[2].editor = ScriptEditor::Devex;
    const devex::serialization::TextDocument document{{writeScriptSettings(settings)}};
    const auto parsed = devex::serialization::parseText(devex::serialization::writeText(document));
    REQUIRE(parsed);
    CHECK(readScriptSettings(parsed->sections.front()) == settings);

    const auto invalid = devex::serialization::parseText(R"([scripts csharp_editor="unknown" automatic_compilation=42])");
    REQUIRE(invalid);
    CHECK(readScriptSettings(invalid->sections.front()) == ScriptSettings{});
}

TEST_CASE("Script opening respects C# C++ and Devex preferences", "[tools][scripts]")
{
    ScriptSettings settings;
    settings.editors[0].editor = ScriptEditor::Custom;
    settings.editors[1].editor = ScriptEditor::System;
    settings.editors[2].editor = ScriptEditor::Custom;
    for (const auto* file : {"Player.cs", "Player.CS", "Game.csproj", "Game.slnx"})
    {
        CHECK(scriptEditorCategory(file) == 0);
        CHECK(scriptEditorFor(settings, file).editor == ScriptEditor::Custom);
    }
    for (const auto* file : {"Player.cpp", "Player.hpp", "Player.H", "Player.cxx", "CMakeLists.txt", "Build.cmake"})
    {
        CHECK(scriptEditorCategory(file) == 1);
        CHECK(scriptEditorFor(settings, file).editor == ScriptEditor::System);
    }
    for (const auto* file : {"Scene.dvxscene", "Surface.dvxmat", "image.png.dvxmeta", "editor.dvx", "Theme.DVXTHEME"})
    {
        CHECK(scriptEditorCategory(file) == 2);
        CHECK(scriptEditorFor(settings, file).editor == ScriptEditor::Custom);
    }
    CHECK_FALSE(scriptEditorCategory("Readme.md"));
    CHECK(scriptEditorFor(settings, "Readme.md").editor == ScriptEditor::Devex);
}

TEST_CASE("External editor arguments preserve paths and are never shell commands", "[tools][scripts]")
{
    const auto file = devex::core::pathFromUtf8("D:/Jeu été/{project}/Player Movement.cs");
    const auto project = devex::core::pathFromUtf8("D:/Jeu été");
    ScriptEditorChoice editor{ScriptEditor::Custom, "C:/Program Files/Editor/editor.exe",
                              R"(--reuse-window "{project}" --goto "{file}:12" "C:\some folder\" "" $(literal) &literal)"};
    const auto command = scriptEditorCommand(editor, file, project);
    REQUIRE(command);
    REQUIRE(command->size() == 9);
    CHECK((*command)[0] == editor.executable);
    CHECK((*command)[2] == devex::core::toUtf8(project));
    CHECK((*command)[4] == devex::core::toUtf8(file) + ":12");
    CHECK((*command)[5] == "C:\\some folder\\");
    CHECK((*command)[6].empty());
    CHECK((*command)[7] == "$(literal)");
    CHECK((*command)[8] == "&literal");

    editor.arguments = "--reuse-window";
    const auto appended = scriptEditorCommand(editor, file, project);
    REQUIRE(appended);
    REQUIRE(appended->size() == 3);
    CHECK(appended->back() == devex::core::toUtf8(file));
    editor.arguments = "\"{file}";
    CHECK_FALSE(scriptEditorCommand(editor, file, project));
    editor.executable.clear();
    CHECK_FALSE(scriptEditorCommand(editor, file, project));
}

TEST_CASE("IDE launch loads the project before opening a script", "[tools][scripts]")
{
    using namespace devex;
    ScriptDirectory directory;
    const auto root = directory.path / core::pathFromUtf8("Jeu été");
    const auto code = root / "code";
    std::filesystem::create_directories(code);
    REQUIRE(core::writeTextFile(code / "Game.csproj", "<Project />"));
    REQUIRE(core::writeTextFile(code / "CMakeLists.txt", "project(Game)"));
    const auto file = code / "Characters/Player Movement.cs";
    ScriptEditorChoice editor{ScriptEditor::Custom, "C:/Program Files/Rider/bin/rider64.exe"};
    auto command = scriptEditorCommand(editor, file, root);
    REQUIRE(command);
    CHECK(*command == std::vector<std::string>{editor.executable, core::toUtf8(code / "Game.csproj"), "--line", "1", core::toUtf8(file)});

    editor.executable = "C:/Programs/CLion/bin/clion64.exe";
    command = scriptEditorCommand(editor, code / "Player.cpp", root);
    REQUIRE(command);
    CHECK(*command == std::vector<std::string>{editor.executable, core::toUtf8(code), "--line", "1", core::toUtf8(code / "Player.cpp")});
    command = scriptEditorCommand(editor, code / "CMakeLists.txt", root);
    REQUIRE(command);
    CHECK(*command == std::vector<std::string>{editor.executable, core::toUtf8(code)});

    editor.executable = "C:/Code/Code.exe";
    command = scriptEditorCommand(editor, file, root);
    REQUIRE(command);
    CHECK(*command == std::vector<std::string>{editor.executable, core::toUtf8(code), "--goto", core::toUtf8(file) + ":1"});

    editor.executable = "C:/VS/devenv.exe";
    command = scriptEditorCommand(editor, file, root);
    REQUIRE(command);
    CHECK(*command == std::vector<std::string>{editor.executable, core::toUtf8(code / "Game.csproj"), "/Command", "File.OpenFile \"" + core::toUtf8(file) + "\""});

    editor.executable = "C:/Rider/rider64.exe";
    editor.arguments = R"(--custom "{project_file}" "{project_dir}" "{project}" "{file}")";
    command = scriptEditorCommand(editor, file, root);
    REQUIRE(command);
    CHECK(*command == std::vector<std::string>{editor.executable, "--custom", core::toUtf8(code / "Game.csproj"), core::toUtf8(code), core::toUtf8(root), core::toUtf8(file)});

    editor.arguments = "\"{file}\"";
    command = scriptEditorCommand(editor, root / "assets/theme.dvxtheme", root);
    REQUIRE(command);
    CHECK(*command == std::vector<std::string>{editor.executable, "--line", "1", core::toUtf8(root / "assets/theme.dvxtheme")});
    CHECK(scriptProjectFile(file, root / "missing").empty());
}

TEST_CASE("IDE discovery ignores missing executables and deduplicates installations", "[tools][scripts]")
{
    using namespace devex;
    ScriptDirectory directory;
    const auto rider = directory.path / "Rider/bin/rider64.exe";
    const auto clion = directory.path / "CLion/bin/clion64.exe";
    const auto riderAlias = directory.path / "Rider/bin/rider.exe";
    const auto unknown = directory.path / "Unknown/editor.exe";
    for (const auto& path : {rider, riderAlias, clion, unknown})
    {
        std::filesystem::create_directories(path.parent_path());
        REQUIRE(core::writeTextFile(path, ""));
    }
    const std::array candidates{rider, rider.parent_path() / "./rider64.exe", riderAlias, clion, unknown, directory.path / "Code.exe"};
    const auto found = findScriptEditors(candidates);
    REQUIRE(found.size() == 2);
    CHECK(found[0].name == "JetBrains CLion");
    CHECK(found[1].name == "JetBrains Rider");
    CAPTURE(found[1].executable, rider);
    CHECK(sameEditorPath(found[1].executable, rider));
    CHECK(externalEditorKind("RIDER64.EXE") == ExternalEditor::Rider);
}

TEST_CASE("Editor paths recognize filesystem aliases and keep distinct files separate", "[tools][scripts]")
{
    using namespace devex;
    ScriptDirectory directory;
    const auto executable = directory.path / "Installation/bin/rider64.exe";
    const auto alias = directory.path / "Alias/bin/rider64.exe";
    const auto other = directory.path / "Other/bin/rider64.exe";
    for (const auto& file : {executable, alias, other})
    {
        std::filesystem::create_directories(file.parent_path());
    }
    REQUIRE(core::writeTextFile(executable, ""));
    REQUIRE(core::writeTextFile(other, ""));
    // Hard links need no symlink privilege on Windows and exercise identity, not path spelling.
    std::error_code error;
    std::filesystem::create_hard_link(executable, alias, error);
    REQUIRE_FALSE(error);
    CHECK(sameEditorPath(executable, alias));
    CHECK(sameEditorPath(alias, executable));
    CHECK_FALSE(sameEditorPath(executable, other));
    CHECK_FALSE(sameEditorPath(executable, directory.path / "missing.exe"));
    CHECK_FALSE(sameEditorPath(directory.path / "missing.exe", directory.path / "absent.exe"));
    const std::array candidates{executable, alias, other};
    CHECK(findScriptEditors(candidates).size() == 2);
}
