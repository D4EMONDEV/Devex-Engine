#include "tools/ToolsState.hpp"

#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace devex;
using namespace devex::tools::detail;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view savedScene = "[scene format=1 kind=\"3d\"]\n";

// An editor over a project, from its opening to the end of what it did.
struct Session
{
    Session(const asset::Project& project, platform::Platform& platform, platform::Window& window, render::Renderer& renderer)
        : database(asset::AssetDatabase::open(project, jobs)), state(std::make_unique<ToolsState>(platform, window, renderer, tools::ToolsMode::Editor))
    {
        REQUIRE(database);
        state->database = database->get();
    }

    core::JobSystem jobs{1};
    core::Result<std::unique_ptr<asset::AssetDatabase>> database;
    std::unique_ptr<ToolsState> state;
    scene::Scene scene;
};

struct Directory
{
    fs::path path = fs::temp_directory_path() / ("devex-autosave-" + core::Uuid::generate().toString());
    ~Directory()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

} // namespace

TEST_CASE("Unsaved work is copied away from its files, and comes back after a crash", "[tools][autosave][gpu]")
{
    Directory directory;
    auto project = asset::createProject(directory.path, "Autosave test");
    REQUIRE(project);
    const fs::path sceneFile = directory.path / "assets" / "level.dvxscene";
    REQUIRE(core::writeTextFile(sceneFile, savedScene));
    const fs::path textFile = directory.path / "code" / "Player.cs";
    REQUIRE(core::writeTextFile(textFile, "// saved\n"));
    const fs::path folder = project->cacheDirectory() / "autosave";
    const fs::path kept = directory.path / "kept";

    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = false});
    REQUIRE(renderer);

    {
        Session session(*project, *platform, *window, *renderer);
        ToolsState& state = *session.state;
        openSceneTab(state, session.scene, sceneFile);
        openTextFile(state, textFile);

        // Nothing unsaved, nothing kept.
        writeAutosaveCopies(state, session.scene);
        CHECK_FALSE(fs::exists(folder));

        // An edit, as the history counts them.
        static_cast<void>(session.scene.createEntity("Added"));
        state.savedState = state.history.stateId() + 1;
        state.textDocuments.front().text = "// edited\n";
        writeAutosaveCopies(state, session.scene);
        CHECK(findAutosaveCopies(*project).size() == 2);

        // The files have not moved.
        CHECK(*core::readTextFile(sceneFile) == savedScene);
        CHECK(*core::readTextFile(textFile) == "// saved\n");

        // The state of a crash, which leaves the copies where they are.
        fs::copy(folder, kept, fs::copy_options::recursive);

        // Saved again, a text loses its copy.
        REQUIRE(state.textDocuments.front().save());
        state.autosave.swept = {};
        updateAutosave(state, session.scene);
        CHECK(findAutosaveCopies(*project).size() == 1);
    }

    fs::remove_all(folder);
    fs::copy(kept, folder, fs::copy_options::recursive);
    REQUIRE(core::writeTextFile(textFile, "// saved\n"));

    {
        Session session(*project, *platform, *window, *renderer);
        ToolsState& state = *session.state;
        state.autosave.recoverable = findAutosaveCopies(*project);
        REQUIRE(state.autosave.recoverable.size() == 2);
        openSceneTab(state, session.scene, sceneFile);
        REQUIRE(state.tabs.size() == 1);

        recoverAutosave(state, session.scene);
        CHECK(state.autosave.recoverable.empty());

        // In place of the scene from its file, modified, with its file untouched.
        REQUIRE(state.tabs.size() == 1);
        CHECK(state.scenePath == sceneFile);
        CHECK(session.scene.entityCount() == 1);
        CHECK(hasUnsavedChanges(state, session.scene));
        CHECK(*core::readTextFile(sceneFile) == savedScene);

        REQUIRE(state.textDocuments.size() == 1);
        CHECK(state.textDocuments.front().text == "// edited\n");
        CHECK(state.textDocuments.front().modified());
        CHECK(*core::readTextFile(textFile) == "// saved\n");

        // What came back is copied again, in case of another crash; what it came from stays aside.
        CHECK(findAutosaveCopies(*project).size() == 2);
        CHECK(fs::exists(project->cacheDirectory() / "backups" / "discarded-autosave"));

        // Saving lets go of the copies.
        REQUIRE(saveScene(state, session.scene));
        CHECK_FALSE(state.tabs.isModified(0, activeDocument(state, session.scene)));
        REQUIRE(state.textDocuments.front().save());
        state.autosave.swept = {};
        updateAutosave(state, session.scene);
        CHECK(findAutosaveCopies(*project).empty());
    }
}

TEST_CASE("Copies that say what a file already says are not offered", "[tools][autosave]")
{
    Directory directory;
    auto project = asset::createProject(directory.path, "Autosave test");
    REQUIRE(project);
    const fs::path file = directory.path / "assets" / "level.dvxscene";
    REQUIRE(core::writeTextFile(file, savedScene));
    const fs::path folder = project->cacheDirectory() / "autosave";
    REQUIRE(core::writeTextFile(folder / "same.dvxscene", savedScene));
    REQUIRE(core::writeTextFile(folder / "other.dvxscene", "[scene format=1 kind=\"2d\"]\n"));
    REQUIRE(core::writeTextFile(folder / "index.dvx", std::format("[autosave format=1]\n\n"
                                                                  "[copy kind=\"scene\" file=\"same.dvxscene\" original=\"{0}\"]\n\n"
                                                                  "[copy kind=\"scene\" file=\"other.dvxscene\" original=\"{0}\"]\n\n"
                                                                  "[copy kind=\"scene\" file=\"gone.dvxscene\" original=\"{0}\"]\n",
                                                                  core::toUtf8(file))));
    const std::vector<AutosaveCopy> copies = findAutosaveCopies(*project);
    REQUIRE(copies.size() == 1);
    CHECK(copies.front().copy.filename() == "other.dvxscene");
    CHECK(copies.front().original == file);

    setAsideAutosave(*project);
    CHECK_FALSE(fs::exists(folder));
    CHECK(findAutosaveCopies(*project).empty());
    CHECK(fs::exists(project->cacheDirectory() / "backups" / "discarded-autosave" / "other.dvxscene"));

    // Leaving normally removes them.
    REQUIRE(core::writeTextFile(folder / "index.dvx", "[autosave format=1]\n"));
    removeAutosave(*project);
    CHECK_FALSE(fs::exists(folder));
}

TEST_CASE("A project opened for the first time starts on its scene, and offers back what a crash left", "[tools][autosave][gpu]")
{
    Directory directory;
    auto project = asset::createProject(directory.path, "Opening test");
    REQUIRE(project);
    const fs::path first = directory.path / "assets" / "a.dvxscene";
    const fs::path startup = directory.path / "assets" / "b.dvxscene";
    REQUIRE(core::writeTextFile(first, savedScene));
    REQUIRE(core::writeTextFile(startup, savedScene));
    project->startupScene = "res://assets/b.dvxscene";
    REQUIRE(asset::saveProject(*project));

    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window = platform->createWindow({.width = 800, .height = 600, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = false});
    REQUIRE(renderer);

    {
        Session session(*project, *platform, *window, *renderer);
        session.state->projectChanged = true;
        updateEditorSession(*session.state, session.scene);
        CHECK(session.state->scenePath == startup);
        CHECK_FALSE(session.state->openRecoverPopup);
    }

    const fs::path folder = project->cacheDirectory() / "autosave";
    REQUIRE(core::writeTextFile(folder / "copy.dvxscene", "[scene format=1 kind=\"2d\"]\n"));
    REQUIRE(core::writeTextFile(folder / "index.dvx", std::format("[autosave format=1]\n\n[copy kind=\"scene\" file=\"copy.dvxscene\" original=\"{}\"]\n", core::toUtf8(startup))));
    Session session(*project, *platform, *window, *renderer);
    session.state->projectChanged = true;
    updateEditorSession(*session.state, session.scene);
    CHECK(session.state->openRecoverPopup);
    CHECK(session.state->autosave.recoverable.size() == 1);
}
