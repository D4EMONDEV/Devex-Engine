#include "tools/ToolsState.hpp"

#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/asset/import/AnimatorFile.hpp>
#include <devex/asset/import/CurveFile.hpp>
#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/asset/import/TilesetFile.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace devex;
using namespace devex::tools::detail;

TEST_CASE("Script preferences persist with user settings and explicit Devex editing overrides them", "[tools][scripts][gpu]")
{
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-script-settings-" + core::Uuid::generate().toString());
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } directory;
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    ToolsState state(*platform, *window, *renderer, tools::ToolsMode::Editor);
    loadUserSettings(state, directory.path / "editor.dvx");
    state.scripts.automaticCompilation = false;
    state.scripts.editors[0] = {ScriptEditor::Custom, "chosen-editor.exe", "\"{file}\""};
    state.scripts.editors[1].editor = ScriptEditor::System;
    state.theme.fontSize = 17.0f;
    saveUserSettings(state);
    const ScriptSettings expected = state.scripts;
    state.scripts = {};
    loadUserSettings(state, state.userSettingsFile);
    CHECK(state.scripts == expected);
    CHECK(state.theme.fontSize == 17.0f);

    const auto file = directory.path / "Player.cs";
    REQUIRE(core::writeTextFile(file, "// unsaved edits stay in Devex Script\n"));
    // Explicit internal editing must never launch the configured external application.
    openTextFile(state, file);
    REQUIRE(state.textDocuments.size() == 1);
    CHECK(state.activeText == file);
    state.textDocuments.front().text = "// changed";
    state.scripts.editors[0].editor = ScriptEditor::Devex;
    openInPreferredEditor(state, file);
    REQUIRE(state.textDocuments.size() == 1);
    CHECK(state.textDocuments.front().text == "// changed");
    CHECK(state.textDocuments.front().modified());

    const auto devex = directory.path / "Theme.dvxtheme";
    REQUIRE(core::writeTextFile(devex, "# theme\n"));
    openInPreferredEditor(state, devex);
    REQUIRE(state.textDocuments.size() == 2);
    CHECK(state.activeText == devex);
}

TEST_CASE("File creation lists content folders and writes named assets in their destination", "[tools][filesystem][gpu]")
{
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-create-files-" + core::Uuid::generate().toString());
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } directory;
    auto project = asset::createProject(directory.path, "Creation test");
    REQUIRE(project);
    std::filesystem::create_directories(project->assetsDirectory() / "Art/Empty");
    for (const auto* folder : {"Gameplay/Actors", "Gameplay/obj", "bin", ".idea"})
    {
        std::filesystem::create_directories(project->codeDirectory() / folder);
    }
    CHECK(creationFolders(*project, asset::ContentRoot::Code) ==
          std::vector<std::string>{"res://code", "res://code/Gameplay", "res://code/Gameplay/Actors"});
    CHECK(creationFolders(*project, asset::ContentRoot::Assets) ==
          std::vector<std::string>{"res://assets", "res://assets/Art", "res://assets/Art/Empty"});
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    ToolsState state(*platform, *window, *renderer, tools::ToolsMode::Editor);
    state.database = database->get();
    const std::string folder = "res://assets/Art/Empty";
    const auto curve = createCurveFile(state, folder, "Jump curve");
    const auto frames = createSpriteFramesFile(state, folder, {}, "Hero frames");
    const auto tiles = createTilesetFile(state, folder, {}, "World tiles");
    const auto animator = createAnimatorFile(state, folder, "Hero animator");
    REQUIRE(curve);
    REQUIRE(frames);
    REQUIRE(tiles);
    REQUIRE(animator);
    for (const auto& file : {*curve, *frames, *tiles, *animator})
    {
        CHECK(file.parent_path() == project->assetsDirectory() / "Art/Empty");
        CHECK(std::filesystem::is_regular_file(file));
    }
    CHECK(curve->filename() == "Jump curve.dvxcurve");
    CHECK(frames->filename() == "Hero frames.dvxframes");
    CHECK(tiles->filename() == "World tiles.dvxtileset");
    CHECK(animator->filename() == "Hero animator.dvxanimator");
    CHECK(asset::parseCurveFile(*core::readTextFile(*curve)));
    CHECK(asset::parseSpriteFramesFile(*core::readTextFile(*frames)));
    CHECK(asset::parseTilesetFile(*core::readTextFile(*tiles)));
    CHECK(asset::parseAnimatorFile(*core::readTextFile(*animator)));
    const auto original = core::readTextFile(*tiles);
    REQUIRE(original);
    CHECK_FALSE(createTilesetFile(state, folder, {}, "World tiles"));
    CHECK(*core::readTextFile(*tiles) == *original);
    CHECK_FALSE(createAnimatorFile(state, "res://code", "Escaped"));
    CHECK_FALSE(createCurveFile(state, folder, "../Escaped"));
    (*database)->waitForImports();
    static_cast<void>((*database)->update());
    CHECK(state.assetToSelect == "res://assets/Art/Empty/Hero animator.dvxanimator");

    const auto selected = core::Uuid::generate();
    state.selection.set(selected);
    requestNewScript(state, true);
    CHECK(state.newScriptTarget == selected);
    requestNewScript(state);
    CHECK(state.newScriptTarget.isNil());
    CHECK(state.newFileFolder == "res://code");
}

TEST_CASE("FileSystem deletion closes affected documents and preserves other tabs", "[tools][filesystem][gpu]")
{
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-delete-session-" + core::Uuid::generate().toString());
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } directory;
    auto project = asset::createProject(directory.path, "Delete test");
    REQUIRE(project);
    const auto removed = project->assetsDirectory() / "Levels/Level.dvxscene";
    const auto kept = project->assetsDirectory() / "LevelsSibling/Keep.dvxscene";
    REQUIRE(core::writeTextFile(removed, "[scene format=1]\n"));
    REQUIRE(core::writeTextFile(kept, "[scene format=1]\n"));
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    ToolsState state(*platform, *window, *renderer, tools::ToolsMode::Editor);
    state.database = database->get();
    scene::Scene scene;
    for (const auto& path : {kept, removed})
    {
        SceneDocument document;
        document.path = path;
        const auto tab = state.tabs.add(std::move(document));
        activateSceneTab(state, scene, tab);
        openTextFile(state, path);
    }
    const auto keptId = state.tabs.id(0);
    const auto removedId = state.tabs.id(1);
    findTextDocument(state, removed)->text += "# unsaved\n";
    state.selectedCode = kept;
    state.playState = tools::PlayState::Playing;
    CHECK_FALSE(deleteFileSystemPath(state, scene, "res://assets/Levels"));
    CHECK(std::filesystem::exists(removed));
    state.playState = tools::PlayState::Editing;
    REQUIRE(deleteFileSystemPath(state, scene, "res://assets/Levels"));
    CHECK_FALSE(std::filesystem::exists(removed));
    CHECK_FALSE(state.tabs.findById(removedId));
    CHECK(state.tabs.findById(keptId));
    CHECK(state.scenePath == kept);
    CHECK_FALSE(findTextDocument(state, removed));
    CHECK(findTextDocument(state, kept));
    CHECK(state.activeText == kept);
    CHECK(state.selectedCode == kept);
    CHECK_FALSE(state.pendingAction);
    REQUIRE(deleteFileSystemPath(state, scene, "res://assets/LevelsSibling"));
    REQUIRE(state.tabs.size() == 1);
    CHECK(state.scenePath.empty());
    CHECK(state.textDocuments.empty());
    CHECK(state.activeText.empty());
}

TEST_CASE("FileSystem moves and renames carry the open documents with them", "[tools][filesystem][gpu]")
{
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-move-session-" + core::Uuid::generate().toString());
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } directory;
    auto project = asset::createProject(directory.path, "Move test");
    REQUIRE(project);
    const auto level = project->assetsDirectory() / "Levels/Level.dvxscene";
    const auto kept = project->assetsDirectory() / "Keep.dvxscene";
    const auto player = project->codeDirectory() / "Player.cs";
    REQUIRE(core::writeTextFile(level, "[scene format=1]\n"));
    REQUIRE(core::writeTextFile(kept, "[scene format=1]\n"));
    REQUIRE(core::writeTextFile(player, "// player\n"));
    std::filesystem::create_directories(project->assetsDirectory() / "Other");
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    ToolsState state(*platform, *window, *renderer, tools::ToolsMode::Editor);
    state.database = database->get();
    scene::Scene scene;
    for (const auto& path : {kept, level})
    {
        SceneDocument document;
        document.path = path;
        activateSceneTab(state, scene, state.tabs.add(std::move(document)));
    }
    openTextFile(state, level);
    openTextFile(state, player);
    findTextDocument(state, level)->text += "# unsaved\n";
    state.selectedCode = player;

    state.playState = tools::PlayState::Playing;
    CHECK_FALSE(moveFileSystemPath(state, scene, "res://assets/Levels", "res://assets/Other", "Levels"));
    state.playState = tools::PlayState::Editing;
    const auto moved = moveFileSystemPath(state, scene, "res://assets/Levels", "res://assets/Other", "Levels");
    REQUIRE(moved);
    CHECK(*moved == "res://assets/Other/Levels");
    const auto movedLevel = project->assetsDirectory() / "Other/Levels/Level.dvxscene";
    CHECK(std::filesystem::exists(movedLevel));
    // The active tab, its text with what was not saved, and the other tab.
    CHECK(sameTextPath(state.scenePath, movedLevel));
    CHECK(sameTextPath(state.tabs.background(0).path, kept));
    TextDocument* const text = findTextDocument(state, movedLevel);
    REQUIRE(text != nullptr);
    CHECK(text->text.ends_with("# unsaved\n"));
    CHECK(state.assetToReveal == "res://assets/Other/Levels");

    REQUIRE(moveFileSystemPath(state, scene, "res://code/Player.cs", "res://code", "Hero.cs"));
    const auto hero = project->codeDirectory() / "Hero.cs";
    CHECK(sameTextPath(state.selectedCode, hero));
    CHECK(sameTextPath(state.activeText, hero));
    CHECK(findTextDocument(state, hero) != nullptr);
    CHECK_FALSE(findTextDocument(state, player));
}

TEST_CASE("Text editor protects pending changes and synchronizes scene and project edits", "[tools][text][gpu]")
{
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-text-session-" + core::Uuid::generate().toString());
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } directory;
    auto project = asset::createProject(directory.path, "Text test");
    REQUIRE(project);
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs);
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    ToolsState state(*platform, *window, *renderer, tools::ToolsMode::Editor);
    state.database = database->get();
    scene::Scene scene;

    const auto file = directory.path / "code" / "Test.cs";
    REQUIRE(core::writeTextFile(file, "// original\n"));
    openTextFile(state, file);
    openTextFile(state, directory.path / "code" / "." / "Test.cs");
    REQUIRE(state.textDocuments.size() == 1);
    state.textDocuments.front().text = "// edited\n";
    CHECK(hasUnsavedChanges(state, scene));
    requestAction(state, scene, {.kind = PendingAction::Kind::CloseText, .path = file});
    REQUIRE(state.pendingAction);
    CHECK(state.textDocuments.size() == 1);
    cancelPendingAction(state);
    CHECK(state.textDocuments.front().modified());
    requestAction(state, scene, {.kind = PendingAction::Kind::CloseProject});
    CHECK_FALSE(state.requests.closeProject);
    REQUIRE(saveForPendingAction(state, scene));
    continuePendingAction(state, scene);
    CHECK(state.requests.closeProject);
    CHECK(*core::readTextFile(file) == "// edited\n");
    state.requests.closeProject = false;

    state.textDocuments.front().text = "// discard\n";
    requestAction(state, scene, {.kind = PendingAction::Kind::ReloadText, .path = file});
    REQUIRE(state.pendingAction);
    discardPendingAction(state, scene);
    CHECK(state.textDocuments.front().text == "// edited\n");
    CHECK_FALSE(hasUnsavedChanges(state, scene));
    state.textDocuments.front().text = "// close without saving";
    requestAction(state, scene, {.kind = PendingAction::Kind::CloseText, .path = file});
    REQUIRE(state.pendingAction);
    discardPendingAction(state, scene);
    CHECK(state.textDocuments.empty());
    CHECK(*core::readTextFile(file) == "// edited\n");

    const auto scenePath = project->assetsDirectory() / "Level.dvxscene";
    REQUIRE(core::writeTextFile(scenePath, "[scene format=1]\n"));
    SceneDocument sceneDocument;
    sceneDocument.path = scenePath;
    const auto tab = state.tabs.add(std::move(sceneDocument));
    activateSceneTab(state, scene, tab);
    openTextFile(state, scenePath);
    TextDocument* text = findTextDocument(state, scenePath);
    REQUIRE(text);
    text->text = "[scene format=1]\n[entity uuid=\"12345678-1234-1234-1234-123456789abc\" name=\"From text\"]\n";
    REQUIRE(saveTextFile(state, scene, *text));
    CHECK(scene.entityCount() == 1);
    CHECK_FALSE(state.tabs.isModified(tab, activeDocument(state, scene)));
    state.savedState = state.history.stateId() + 1;
    text->text = "[scene format=1]\n";
    CHECK_FALSE(saveTextFile(state, scene, *text));
    CHECK(text->modified());
    CHECK(scene.entityCount() == 1);
    state.savedState = state.history.stateId();
    text->text = "invalid scene syntax";
    CHECK_FALSE(saveTextFile(state, scene, *text));
    CHECK(scene.entityCount() == 1);
    REQUIRE(text->reload());
    state.playState = tools::PlayState::Playing;
    text->text += "\n";
    CHECK_FALSE(saveTextFile(state, scene, *text));
    CHECK(text->modified());
    state.playState = tools::PlayState::Editing;
    REQUIRE(text->reload());

    const auto backgroundPath = project->assetsDirectory() / "Background.dvxscene";
    REQUIRE(core::writeTextFile(backgroundPath, "[scene format=1]\n"));
    SceneDocument background;
    background.path = backgroundPath;
    const auto backgroundTab = state.tabs.add(std::move(background));
    openTextFile(state, backgroundPath);
    TextDocument* backgroundText = findTextDocument(state, backgroundPath);
    REQUIRE(backgroundText);
    backgroundText->text = "[scene format=1]\n[entity uuid=\"22345678-1234-1234-1234-123456789abc\" name=\"Background\"]\n";
    REQUIRE(saveTextFile(state, scene, *backgroundText));
    CHECK(state.tabs.background(backgroundTab).scene.entityCount() == 1);
    CHECK(scene.entityCount() == 1);

    openTextFile(state, project->file);
    TextDocument* settings = findTextDocument(state, project->file);
    REQUIRE(settings);
    settings->text = "[project format=1 name=\"Edited project\"]\n";
    REQUIRE(saveTextFile(state, scene, *settings));
    CHECK(state.database->project().name == "Edited project");
    CHECK(*core::readTextFile(project->file) == settings->text);

    settings->text = "unsaved";
    requestAction(state, scene, {.kind = PendingAction::Kind::Quit});
    REQUIRE(state.pendingAction);
    CHECK_FALSE(state.requests.quit);
    cancelPendingAction(state);
    CHECK(settings->modified());
}

TEST_CASE("Closing several scene tabs asks once about the unsaved ones and closes them all", "[tools][tabs][gpu]")
{
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-tabs-session-" + core::Uuid::generate().toString());
        ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
    } directory;
    auto project = asset::createProject(directory.path, "Tabs test");
    REQUIRE(project);
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs);
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    ToolsState state(*platform, *window, *renderer, tools::ToolsMode::Editor);
    state.database = database->get();
    scene::Scene scene;

    // Three scenes, the one in the middle with a change not saved yet.
    std::vector<std::uint64_t> ids;
    for (const char* name : {"First", "Second", "Third"})
    {
        SceneDocument document;
        document.path = project->assetsDirectory() / (std::string(name) + ".dvxscene");
        REQUIRE(core::writeTextFile(document.path, "[scene format=1]\n"));
        ids.push_back(state.tabs.id(state.tabs.add(std::move(document))));
    }
    activateSceneTab(state, scene, 0);
    state.tabs.background(1).savedState = state.tabs.background(1).history.stateId() + 1;

    // The tabs at the right of the first: the changed one holds them back until a choice.
    requestAction(state, scene, {.kind = PendingAction::Kind::CloseTab, .tabs = {ids[1], ids[2]}});
    REQUIRE(state.pendingAction);
    CHECK(state.tabs.size() == 3);
    discardPendingAction(state, scene);
    REQUIRE(state.tabs.size() == 1);
    CHECK(state.tabs.id(0) == ids[0]);

    // All of them, none changed: they go at once, and a new scene takes their place.
    requestAction(state, scene, {.kind = PendingAction::Kind::CloseTab, .tabs = {ids[0]}});
    CHECK_FALSE(state.pendingAction);
    REQUIRE(state.tabs.size() == 1);
    CHECK(state.tabs.id(0) != ids[0]);
}
