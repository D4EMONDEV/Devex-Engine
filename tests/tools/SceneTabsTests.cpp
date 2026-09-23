#include "tools/SceneTabs.hpp"

#include <devex/scene/Scene.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <unordered_set>

using devex::tools::detail::ActiveDocument;
using devex::tools::detail::SceneDocument;
using devex::tools::detail::SceneTabs;

namespace {

// The editor's live document: the application's scene and the tools' fields.
struct Live
{
    std::filesystem::path path;
    devex::scene::Scene scene;
    devex::tools::CommandHistory history;
    std::uint64_t savedState = 0;
    devex::tools::detail::Selection selection;
    devex::tools::detail::EditorCamera camera;
    std::unordered_set<devex::core::Uuid> hidden;

    [[nodiscard]] ActiveDocument document() noexcept
    {
        return {path, scene, history, savedState, selection, camera, hidden};
    }
};

[[nodiscard]] SceneDocument makeDocument(const std::string& name, std::size_t entities)
{
    SceneDocument document;
    document.path = std::filesystem::path("scenes") / (name + ".dvxscene");
    for (std::size_t index = 0; index < entities; ++index)
    {
        static_cast<void>(document.scene.createEntity(name + std::to_string(index)));
    }
    document.savedState = document.history.stateId();
    return document;
}

} // namespace

TEST_CASE("Scene tabs swap their documents with the live one", "[tools][tabs]")
{
    Live live;
    SceneTabs tabs;
    CHECK(tabs.empty());
    CHECK_FALSE(tabs.active().has_value());

    const std::size_t level = tabs.add(makeDocument("level", 3));
    const std::size_t menu = tabs.add(makeDocument("menu", 1));
    tabs.activate(level, live.document());
    REQUIRE(tabs.active() == level);
    CHECK(live.scene.entityCount() == 3);
    CHECK(live.scene.name(live.scene.firstRoot()) == "level0");
    CHECK(live.path.filename() == "level.dvxscene");

    // An edit and a selection stay with their scene.
    const devex::core::Uuid first = live.scene.uuid(live.scene.firstRoot());
    REQUIRE(live.history.execute(live.scene, devex::tools::makeRenameCommand(first, "level0", "Hero")));
    live.selection.set(first);
    live.hidden.insert(first);
    live.camera.set({1.0f, 2.0f, 3.0f}, 10.0f, -20.0f, 5.0f, 4.0f);
    CHECK(tabs.isModified(level, live.document()));
    CHECK_FALSE(tabs.isModified(menu, live.document()));

    tabs.activate(menu, live.document());
    CHECK(live.scene.entityCount() == 1);
    CHECK(live.scene.name(live.scene.firstRoot()) == "menu0");
    CHECK(live.selection.empty());
    CHECK(live.hidden.empty());
    CHECK(live.history.nextUndo() == nullptr);
    CHECK(tabs.isModified(level, live.document()));
    CHECK(tabs.background(level).scene.entityCount() == 3);

    tabs.activate(level, live.document());
    CHECK(live.scene.entityCount() == 3);
    CHECK(live.scene.name(live.scene.firstRoot()) == "Hero");
    CHECK(live.selection.active() == first);
    CHECK(live.hidden.contains(first));
    CHECK(live.camera.pivot() == devex::math::Vec3{1.0f, 2.0f, 3.0f});
    REQUIRE(live.history.nextUndo() != nullptr);
    REQUIRE(live.history.undo(live.scene));
    CHECK(live.scene.name(live.scene.firstRoot()) == "level0");
    CHECK_FALSE(tabs.isModified(level, live.document()));

    CHECK(tabs.findByPath(std::filesystem::path("scenes") / "menu.dvxscene", live.document()) == menu);
    CHECK_FALSE(tabs.findByPath("scenes/other.dvxscene", live.document()).has_value());
    std::size_t background = 0;
    tabs.forEachBackgroundScene([&background](devex::scene::Scene& scene) { background += scene.entityCount(); });
    CHECK(background == 1);
}

TEST_CASE("Closing the active scene tab shows its neighbor", "[tools][tabs]")
{
    Live live;
    SceneTabs tabs;
    static_cast<void>(tabs.add(makeDocument("a", 1)));
    static_cast<void>(tabs.add(makeDocument("b", 2)));
    static_cast<void>(tabs.add(makeDocument("c", 3)));
    const std::uint64_t idOfC = tabs.id(2);

    tabs.activate(1, live.document());
    tabs.close(1, live.document());
    REQUIRE(tabs.size() == 2);
    // The tab after the closed one comes to the screen.
    CHECK(tabs.active() == 1);
    CHECK(tabs.id(1) == idOfC);
    CHECK(live.scene.entityCount() == 3);

    // Closing a background tab keeps the active one.
    tabs.close(0, live.document());
    CHECK(tabs.active() == 0);
    CHECK(live.scene.entityCount() == 3);

    tabs.close(0, live.document());
    CHECK(tabs.empty());
    CHECK_FALSE(tabs.active().has_value());
    CHECK(live.scene.entityCount() == 0);
    CHECK(live.path.empty());
}

TEST_CASE("Moving scene tabs keeps the active one", "[tools][tabs]")
{
    Live live;
    SceneTabs tabs;
    static_cast<void>(tabs.add(makeDocument("a", 1)));
    static_cast<void>(tabs.add(makeDocument("b", 2)));
    tabs.activate(0, live.document());
    const std::uint64_t active = tabs.id(0);
    tabs.move(0, 1);
    CHECK(tabs.active() == 1);
    CHECK(tabs.id(1) == active);
    CHECK(live.scene.entityCount() == 1);
}
