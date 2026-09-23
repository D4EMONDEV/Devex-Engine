#include <devex/asset/AssetId.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/ModelInstantiation.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/CommandHistory.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <vector>

using devex::core::Uuid;
using devex::math::Vec3;
using devex::scene::Entity;
using devex::scene::MeshRenderer;
using devex::scene::Scene;
using devex::scene::Transform;
using devex::serialization::TextValue;
using devex::tools::CommandHistory;

namespace {

TextValue vec3(float x, float y, float z)
{
    const Vec3 value{x, y, z};
    return devex::scene::writeFieldValue(devex::reflection::ValueKind::Vec3, &value);
}

std::vector<std::string> childNames(const Scene& scene, Entity parent)
{
    std::vector<std::string> names;
    for (Entity child = parent.isValid() ? scene.firstChild(parent) : scene.firstRoot();
         child.isValid(); child = scene.nextSibling(child))
    {
        names.push_back(scene.name(child));
    }
    return names;
}

} // namespace

TEST_CASE("Field edits undo and redo through reflection", "[tools][commands]")
{
    Scene scene;
    CommandHistory history;
    const Entity entity = scene.createEntity("Cube");
    scene.add<Transform>(entity);

    REQUIRE(history.execute(scene, devex::tools::makeSetFieldCommand(
                                       scene.uuid(entity), "Transform", "position",
                                       vec3(0.0f, 0.0f, 0.0f), vec3(1.0f, 2.0f, 3.0f))));
    CHECK(scene.get<Transform>(entity).position == Vec3{1.0f, 2.0f, 3.0f});
    CHECK(history.nextUndo()->description() == "Edit Transform.position");

    REQUIRE(history.undo(scene));
    CHECK(scene.get<Transform>(entity).position == Vec3{0.0f});
    REQUIRE(history.redo(scene));
    CHECK(scene.get<Transform>(entity).position == Vec3{1.0f, 2.0f, 3.0f});
}

TEST_CASE("History state identifiers tell whether a scene changed since it was saved", "[tools][commands]")
{
    Scene scene;
    CommandHistory history(2);
    const Entity entity = scene.createEntity("A");
    const Uuid uuid = scene.uuid(entity);

    const std::uint64_t saved = history.stateId();
    REQUIRE(history.execute(scene, devex::tools::makeRenameCommand(uuid, "A", "B")));
    const std::uint64_t renamed = history.stateId();
    CHECK(renamed != saved);
    REQUIRE(history.undo(scene));
    CHECK(history.stateId() == saved);
    REQUIRE(history.redo(scene));
    CHECK(history.stateId() == renamed);

    // A different change after an undo is a different state.
    REQUIRE(history.undo(scene));
    REQUIRE(history.execute(scene, devex::tools::makeRenameCommand(uuid, "A", "C")));
    CHECK(history.stateId() != renamed);
    CHECK(history.stateId() != saved);

    // Commands dropped beyond the capacity leave a base state that is not the saved one.
    REQUIRE(history.execute(scene, devex::tools::makeRenameCommand(uuid, "C", "D")));
    REQUIRE(history.execute(scene, devex::tools::makeRenameCommand(uuid, "D", "E")));
    REQUIRE(history.undo(scene));
    REQUIRE(history.undo(scene));
    CHECK(history.nextUndo() == nullptr);
    CHECK(history.stateId() != saved);

    const std::uint64_t beforeClear = history.stateId();
    history.clear();
    CHECK(history.stateId() != beforeClear);
}

TEST_CASE("Recording a new command clears the redo stack", "[tools][commands]")
{
    Scene scene;
    CommandHistory history;
    const Entity entity = scene.createEntity("Before");

    REQUIRE(history.execute(scene, devex::tools::makeRenameCommand(scene.uuid(entity), "Before", "Middle")));
    REQUIRE(history.undo(scene));
    CHECK(history.nextRedo() != nullptr);

    history.recordApplied(devex::tools::makeRenameCommand(scene.uuid(entity), "Before", "After"));
    CHECK(history.nextRedo() == nullptr);
    CHECK_FALSE(history.redo(scene).has_value());
}

TEST_CASE("Deleting an entity undoes with its descendants, UUIDs and place", "[tools][commands]")
{
    Scene scene;
    CommandHistory history;
    const Entity root = scene.createEntity("Root");
    const Entity a = scene.createEntity("A");
    const Entity b = scene.createEntity("B");
    const Entity c = scene.createEntity("C");
    const Entity child = scene.createEntity("Child of B");
    REQUIRE(scene.setParent(a, root));
    REQUIRE(scene.setParent(b, root));
    REQUIRE(scene.setParent(c, root));
    REQUIRE(scene.setParent(child, b));
    scene.add<MeshRenderer>(child, devex::asset::builtin::sphereMesh);
    const Uuid childUuid = scene.uuid(child);

    REQUIRE(history.execute(scene, devex::tools::makeDestroyEntityCommand(scene.uuid(b))));
    CHECK(childNames(scene, root) == std::vector<std::string>{"A", "C"});
    CHECK_FALSE(scene.findEntity(childUuid).isValid());

    REQUIRE(history.undo(scene));
    CHECK(childNames(scene, root) == std::vector<std::string>{"A", "B", "C"});
    const Entity restoredChild = scene.findEntity(childUuid);
    REQUIRE(restoredChild.isValid());
    CHECK(scene.get<MeshRenderer>(restoredChild).mesh == devex::asset::builtin::sphereMesh);

    REQUIRE(history.redo(scene));
    CHECK(scene.entityCount() == 3);
}

TEST_CASE("Created entities keep their UUID across undo and redo", "[tools][commands]")
{
    Scene scene;
    CommandHistory history;
    const Entity parent = scene.createEntity("Parent");
    const Uuid created = Uuid::generate();

    REQUIRE(history.execute(scene, devex::tools::makeCreateEntityCommand(created, "Entity", scene.uuid(parent))));
    const Entity entity = scene.findEntity(created);
    REQUIRE(entity.isValid());
    CHECK(scene.parent(entity) == parent);
    CHECK(scene.has<Transform>(entity));

    REQUIRE(history.undo(scene));
    CHECK_FALSE(scene.findEntity(created).isValid());
    REQUIRE(history.redo(scene));
    CHECK(scene.parent(scene.findEntity(created)) == parent);
}

TEST_CASE("Reparenting undoes to the original position", "[tools][commands]")
{
    Scene scene;
    CommandHistory history;
    const Entity first = scene.createEntity("First");
    const Entity second = scene.createEntity("Second");
    const Entity third = scene.createEntity("Third");

    REQUIRE(history.execute(scene, devex::tools::makeReparentCommand(scene.uuid(second), scene.uuid(third))));
    CHECK(childNames(scene, Entity{}) == std::vector<std::string>{"First", "Third"});
    CHECK(scene.parent(second) == third);

    REQUIRE(history.undo(scene));
    CHECK(childNames(scene, Entity{}) == std::vector<std::string>{"First", "Second", "Third"});

    // A cycle is refused and not recorded.
    const std::size_t steps = history.undoCount();
    CHECK_FALSE(history.execute(scene, devex::tools::makeReparentCommand(scene.uuid(first), scene.uuid(first))).has_value());
    CHECK(history.undoCount() == steps);
}

TEST_CASE("Removed components come back with their values", "[tools][commands]")
{
    Scene scene;
    CommandHistory history;
    const Entity entity = scene.createEntity("Camera");
    scene.add<devex::scene::Camera>(entity, devex::scene::Camera{.verticalFov = 0.8f, .primary = false});
    const Uuid uuid = scene.uuid(entity);

    REQUIRE(history.execute(scene, devex::tools::makeRemoveComponentCommand(uuid, "Camera")));
    CHECK_FALSE(scene.has<devex::scene::Camera>(entity));

    REQUIRE(history.undo(scene));
    REQUIRE(scene.has<devex::scene::Camera>(entity));
    CHECK(scene.get<devex::scene::Camera>(entity).verticalFov == 0.8f);
    CHECK_FALSE(scene.get<devex::scene::Camera>(entity).primary);

    REQUIRE(history.execute(scene, devex::tools::makeAddComponentCommand(uuid, "DirectionalLight")));
    CHECK(scene.has<devex::scene::DirectionalLight>(entity));
    CHECK_FALSE(history.execute(scene, devex::tools::makeAddComponentCommand(uuid, "DirectionalLight")).has_value());
    REQUIRE(history.undo(scene));
    CHECK_FALSE(scene.has<devex::scene::DirectionalLight>(entity));
}

TEST_CASE("Commands on missing entities fail and are dropped", "[tools][commands]")
{
    Scene scene;
    CommandHistory history;
    const Entity entity = scene.createEntity("Short-lived");
    const Uuid uuid = scene.uuid(entity);
    REQUIRE(history.execute(scene, devex::tools::makeRenameCommand(uuid, "Short-lived", "Renamed")));

    // The entity disappears outside the history, for instance when a scene is reloaded.
    scene.destroyEntity(entity);

    const auto undone = history.undo(scene);
    REQUIRE_FALSE(undone.has_value());
    CHECK(undone.error().code == devex::core::ErrorCode::NotFound);
    CHECK(history.nextUndo() == nullptr);
    CHECK(history.nextRedo() == nullptr);
}

TEST_CASE("Placed entity trees undo and redo with the same UUIDs", "[tools][commands]")
{
    devex::asset::ModelData model;
    model.nodes.push_back({.name = "Part", .mesh = devex::asset::builtin::cubeMesh});

    Scene scratch;
    const Entity built = devex::scene::instantiateModel(scratch, model, "Model");
    const Uuid root = scratch.uuid(built);
    const Uuid part = scratch.uuid(scratch.firstChild(built));

    Scene scene;
    CommandHistory history;
    const Entity parent = scene.createEntity("Parent");
    REQUIRE(history.execute(scene, devex::tools::makeCreateEntityTreeCommand(
                                       devex::scene::saveEntityTree(scratch, built), root,
                                       scene.uuid(parent), "Place Model")));
    CHECK(history.nextUndo()->description() == "Place Model");
    REQUIRE(scene.findEntity(root).isValid());
    CHECK(scene.parent(scene.findEntity(root)) == parent);
    CHECK(scene.get<MeshRenderer>(scene.findEntity(part)).mesh == devex::asset::builtin::cubeMesh);

    REQUIRE(history.undo(scene));
    CHECK_FALSE(scene.findEntity(root).isValid());
    CHECK(scene.entityCount() == 1);

    REQUIRE(history.redo(scene));
    CHECK(scene.findEntity(part).isValid());
}

TEST_CASE("Entity trees can be created before a sibling, as duplicates are", "[tools][commands]")
{
    Scene scratch;
    const Entity copy = scratch.createEntity("Copy");
    const Uuid copyUuid = scratch.uuid(copy);

    Scene scene;
    CommandHistory history;
    const Entity first = scene.createEntity("First");
    const Entity second = scene.createEntity("Second");
    REQUIRE(history.execute(scene, devex::tools::makeCreateEntityTreeCommand(devex::scene::saveEntityTree(scratch, copy),
                                                                             copyUuid, Uuid{}, "Duplicate",
                                                                             scene.uuid(second))));
    CHECK(scene.nextSibling(first) == scene.findEntity(copyUuid));
    CHECK(scene.nextSibling(scene.findEntity(copyUuid)) == second);

    REQUIRE(history.undo(scene));
    // Once the sibling is gone, the tree goes last.
    scene.destroyEntity(second);
    REQUIRE(history.redo(scene));
    CHECK(scene.nextSibling(first) == scene.findEntity(copyUuid));
}

TEST_CASE("Prefab instances are protected, reverted and made local as undoable steps", "[tools][commands][prefab]")
{
    const devex::asset::AssetId lamp{*Uuid::parse("a0000000-0000-4000-8000-0000000000aa")};
    const Uuid lampRoot = *Uuid::parse("b0000000-0000-4000-8000-0000000000aa");
    const Uuid lampBulb = *Uuid::parse("b0000000-0000-4000-8000-0000000000bb");
    const std::string lampText = std::format("[scene format=1]\n\n[entity uuid=\"{}\" name=\"Lamp\"]\n\n"
                                             "[component type=\"Transform\"]\n\n"
                                             "[entity uuid=\"{}\" name=\"Bulb\"]\nparent = \"{}\"\n\n"
                                             "[component type=\"Transform\"]\n\n[component type=\"MeshRenderer\"]\n",
                                             lampRoot, lampBulb, lampRoot);
    devex::scene::setPrefabSourceLoader([&](devex::asset::AssetId id) -> devex::core::Result<std::string> {
        if (id != lamp)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "unknown prefab");
        }
        return lampText;
    });

    Scene scene;
    CommandHistory history;
    const devex::core::Result<Entity> created = devex::scene::instantiatePrefab(scene, lamp);
    REQUIRE(created.has_value());
    const Uuid instance = scene.uuid(*created);
    const Uuid bulb = devex::scene::derivePrefabUuid(instance, lampBulb);
    const Entity other = scene.createEntity("Other");
    scene.get<Transform>(*created).position = Vec3{3.0f, 0.0f, 0.0f};

    // The entities of the prefab stay in the instance, with the components the prefab gives them.
    CHECK_FALSE(history.execute(scene, devex::tools::makeDestroyEntityCommand(bulb)).has_value());
    CHECK_FALSE(history.execute(scene, devex::tools::makeReparentCommand(bulb, scene.uuid(other))).has_value());
    CHECK_FALSE(history.execute(scene, devex::tools::makeRemoveComponentCommand(bulb, "MeshRenderer")).has_value());
    REQUIRE(history.execute(scene, devex::tools::makeAddComponentCommand(bulb, "PointLight")));
    REQUIRE(history.execute(scene, devex::tools::makeRemoveComponentCommand(bulb, "PointLight")));
    REQUIRE(history.execute(scene, devex::tools::makeReparentCommand(instance, scene.uuid(other))));
    REQUIRE(history.undo(scene));

    SECTION("Revert all keeps the placement of the root")
    {
        scene.setName(scene.findEntity(bulb), "Renamed");
        const std::string reverted = devex::scene::saveRevertedPrefabInstance(scene, scene.findEntity(instance));
        REQUIRE(history.execute(scene, devex::tools::makeReplaceEntityTreeCommand(instance, reverted, "Revert instance")));
        CHECK(scene.name(scene.findEntity(bulb)) == "Bulb");
        CHECK(scene.get<Transform>(scene.findEntity(instance)).position.x == 3.0f);
        REQUIRE(history.undo(scene));
        CHECK(scene.name(scene.findEntity(bulb)) == "Renamed");
        CHECK(childNames(scene, Entity{}) == std::vector<std::string>{"Lamp", "Other"});
    }
    SECTION("Make local")
    {
        const std::string local = devex::scene::saveUnpackedEntityTree(scene, scene.findEntity(instance));
        REQUIRE(history.execute(scene, devex::tools::makeReplaceEntityTreeCommand(instance, local, "Make instance local")));
        CHECK_FALSE(scene.has<devex::scene::PrefabInstance>(scene.findEntity(instance)));
        REQUIRE(history.execute(scene, devex::tools::makeDestroyEntityCommand(bulb)));
        REQUIRE(history.undo(scene));
        REQUIRE(history.undo(scene));
        CHECK(devex::scene::isInsidePrefabInstance(scene, scene.findEntity(bulb)));
        CHECK(childNames(scene, Entity{}) == std::vector<std::string>{"Lamp", "Other"});
    }

    devex::scene::setPrefabSourceLoader({});
}
