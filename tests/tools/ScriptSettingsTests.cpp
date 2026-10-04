#include "tools/ScriptSettings.hpp"

#include <devex/core/Path.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace devex::tools::detail;

TEST_CASE("Script editor preferences survive serialization independently for each language", "[tools][scripts]")
{
    ScriptSettings settings;
    settings.editors[0] = {ScriptEditor::Custom, "C:/Program Files/Editor/rider64.exe", "\"{project}\" \"{file}\""};
    settings.editors[1].editor = ScriptEditor::System;
    settings.editors[2].editor = ScriptEditor::Devex;
    const devex::serialization::TextDocument document{{writeScriptSettings(settings)}};
    const auto parsed = devex::serialization::parseText(devex::serialization::writeText(document));
    REQUIRE(parsed);
    CHECK(readScriptSettings(parsed->sections.front()) == settings);

    const auto invalid = devex::serialization::parseText(R"([scripts csharp_editor="unknown"])");
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
