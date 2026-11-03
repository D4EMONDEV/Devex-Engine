#include "runtime/GlobalScenes.hpp"

#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace devex;
using namespace devex::runtime::detail;

TEST_CASE("Autoloads are ordered, optional and instantiated only once", "[runtime][globals]")
{
    scene::Scene scene;
    const auto level = scene.createEntity("Level");
    const std::array entries{asset::GlobalScene{"State", "res://assets/State.dvxscene"},
                             asset::GlobalScene{"Disabled", "res://assets/Missing.dvxscene", false},
                             asset::GlobalScene{"Music", "res://assets/Music.dvxscene"}};
    int count = 0;
    const InstantiateGlobal create = [&](scene::Scene& target,
                                         const std::string&) -> core::Result<scene::Entity>
    {
        ++count;
        return target.createEntity("Source");
    };
    REQUIRE(startGlobals(scene, entries, create));
    CHECK(count == 2);
    CHECK(hasGlobals(scene));
    const auto state = scene.firstRoot();
    CHECK(scene.name(state) == "State");
    CHECK(scene.name(scene.nextSibling(state)) == "Music");
    CHECK(scene.nextSibling(scene.nextSibling(state)) == level);
    REQUIRE(startGlobals(scene, entries, create));
    CHECK(count == 2);
    scene::Scene empty;
    REQUIRE(startGlobals(empty, entries, create));
    CHECK(empty.name(empty.firstRoot()) == "State");
    CHECK(empty.name(empty.nextSibling(empty.firstRoot())) == "Music");
    count = 2;
    auto restored = scene::loadScene(scene::saveScene(scene));
    REQUIRE(restored);
    REQUIRE(startGlobals(*restored, entries, create));
    CHECK(count == 2);
    CHECK(restored->entityCount() == 3);
}

TEST_CASE("Changing levels preserves global objects, descendants and handles", "[runtime][globals]")
{
    registerGlobalRoot();
    scene::Scene scene;
    const auto global = scene.createEntity("State");
    scene.add<GlobalRoot>(global, GlobalRoot{"State"});
    scene.add<scene::Transform>(global).position.x = 42;
    const auto child = scene.createEntity("Shared UI");
    REQUIRE(scene.setParent(child, global));
    const auto oldLevel = scene.createEntity("Old level");
    scene.add<scene::Transform>(oldLevel);
    scene::Scene next;
    const auto newLevel = next.createEntity("New level");
    const auto uuid = next.uuid(newLevel);
    REQUIRE(replaceLevelKeepingGlobals(scene, next));
    CHECK(scene.isAlive(global));
    CHECK(scene.isAlive(child));
    CHECK(scene.parent(child) == global);
    CHECK(scene.get<scene::Transform>(global).position.x == 42);
    CHECK_FALSE(scene.isAlive(oldLevel));
    CHECK(scene.findEntity(uuid).isValid());
    CHECK(scene.entityCount() == 3);
    REQUIRE(replaceLevelKeepingGlobals(scene, next));
    CHECK(scene.isAlive(global));
    CHECK(scene.entityCount() == 3);
}

TEST_CASE("Global failures leave the level and its valid autoloads intact", "[runtime][globals]")
{
    scene::Scene scene;
    const auto level = scene.createEntity("Level");
    const std::array entries{asset::GlobalScene{"State", "res://assets/State.dvxscene"},
                             asset::GlobalScene{"Missing", "res://assets/Missing.dvxscene"}};
    int count = 0;
    CHECK_FALSE(
        startGlobals(scene, entries,
                     [&](scene::Scene& target, const std::string&) -> core::Result<scene::Entity>
                     {
                         if (++count == 2)
                             return core::makeError(core::ErrorCode::NotFound, "Missing scene");
                         return target.createEntity("Global");
                     }));
    CHECK(scene.entityCount() == 1);
    CHECK(scene.isAlive(level));
    scene.add<GlobalRoot>(level, GlobalRoot{"State"});
    scene::Scene conflict;
    REQUIRE(conflict.createEntity(scene.uuid(level), "Conflicting level"));
    CHECK_FALSE(replaceLevelKeepingGlobals(scene, conflict));
    CHECK(scene.isAlive(level));
    CHECK(scene.entityCount() == 1);
}

TEST_CASE("Projects round trip globals and reject ambiguous names or unsafe paths",
          "[runtime][globals][project]")
{
    asset::Project original{.name = "Globals"};
    original.globals = {{"State", "res://assets/State.dvxscene"},
                        {"Music", "res://assets/Music.dvxscene", false}};
    auto parsed = asset::parseProject(asset::writeProjectText(original), "Globals.dvxproj");
    REQUIRE(parsed);
    CHECK(parsed->globals == original.globals);
    original.globals.push_back(original.globals.front());
    CHECK_FALSE(asset::validateGlobals(original.globals));
    original.globals.pop_back();
    original.globals[0].name = "2Invalid";
    CHECK_FALSE(asset::validateGlobals(original.globals));
    original.globals[0].name = "Valid";
    original.globals[0].path = "res://assets/../Outside.dvxscene";
    CHECK_FALSE(asset::parseProject(asset::writeProjectText(original), "Globals.dvxproj"));
}
