#include "tools/ToolsState.hpp"

#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace devex;
using namespace devex::tools::detail;

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
