#include "runtime/ManagedGame.hpp"

#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using devex::runtime::SystemPhase;
using devex::runtime::detail::ManagedGame;
using devex::scene::Entity;
using devex::scene::EntityRef;
using devex::scene::Scene;

#ifdef DEVEX_TEST_MANAGED_GAME
namespace {

// Starts .NET, or skips the test on machines without it.
[[nodiscard]] std::unique_ptr<ManagedGame> startRuntime()
{
    const std::filesystem::path managed = devex::platform::executableDirectory() / "managed";
    devex::core::Result<std::unique_ptr<ManagedGame>> game = ManagedGame::create(managed);
    if (!game)
    {
        // .NET is not installed on this machine: games are written in C++ there.
        SKIP(game.error().message);
    }
    REQUIRE((*game)->loadAssembly(DEVEX_TEST_MANAGED_GAME));
    return std::move(*game);
}

template <typename T>
[[nodiscard]] T& field(const devex::scene::ComponentType& type, void* component, const char* name)
{
    const devex::reflection::FieldInfo* const info = type.type->findField(name);
    REQUIRE(info != nullptr);
    return *static_cast<T*>(info->address(component));
}

void run(ManagedGame& game, Scene& scene, SystemPhase phase, double seconds = 0.0)
{
    ManagedGame::Frame frame{.scene = &scene, .delta = devex::core::Duration(seconds)};
    game.runPhase(frame, phase);
}

} // namespace

TEST_CASE("The C# runtime registers components and runs them", "[runtime][managed]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    CHECK(game->componentTypes().size() == 5);

    devex::scene::ComponentRegistry& registry = devex::scene::componentRegistry();
    const devex::scene::ComponentType* const mover = registry.find("Mover");
    REQUIRE(mover != nullptr);
    REQUIRE(mover->layout != nullptr);
    REQUIRE(mover->type->fields.size() == 4);
    CHECK(mover->type->findField("speed") != nullptr);
    CHECK(mover->type->findField("direction")->kind == devex::reflection::ValueKind::Vec3);
    CHECK(mover->type->findField("label")->kind == devex::reflection::ValueKind::String);
    // The [Angle] attribute reaches the inspector.
    CHECK(mover->type->findField("turn")->angle);
    CHECK(registry.find("Counter") != nullptr);

    Scene scene;
    const Entity entity = scene.createEntity("Runner");
    scene.add<devex::scene::Transform>(entity);
    void* const component = mover->emplace(scene, entity);
    REQUIRE(component != nullptr);
    // A new component starts with the values the C# class gives its fields.
    CHECK(field<float>(*mover, component, "speed") == 2.0f);
    CHECK(field<std::string>(*mover, component, "label") == "new");
    field<float>(*mover, component, "speed") = 3.0f;
    field<devex::math::Vec3>(*mover, component, "direction") = devex::math::Vec3{0.0f, 1.0f, 0.0f};

    // Start runs once, then Update moves the entity and writes back into the engine's memory.
    run(*game, scene, SystemPhase::Start);
    CHECK(field<std::string>(*mover, component, "label") == "started");
    run(*game, scene, SystemPhase::Update, 0.5);
    CHECK(scene.get<devex::scene::Transform>(entity).position.y == 1.5f);
    CHECK(field<std::string>(*mover, component, "label") == "moved Runner");
    // A system ran over the whole scene, after the components of the phase.
    CHECK(scene.name(entity) == "renamed by the system");
    // A component the game removed from the scene loses its C# object without a trace.
    mover->remove(scene, entity);
    run(*game, scene, SystemPhase::Update, 0.5);
    CHECK(scene.get<devex::scene::Transform>(entity).position.y == 1.5f);

    // Unloading keeps the values in the scene as text, and forgets the types.
    void* const again = mover->emplace(scene, entity);
    field<float>(*mover, again, "speed") = 7.0f;
    CHECK(game->release(scene) == 1);
    game->unloadAssembly();
    CHECK(registry.find("Mover") == nullptr);
    CHECK(devex::scene::saveScene(scene).find("speed = 7") != std::string::npos);
}

TEST_CASE("C# components hold lists and entities and reach the other components", "[runtime][managed]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    devex::scene::ComponentRegistry& registry = devex::scene::componentRegistry();
    const devex::scene::ComponentType* patrol = registry.find("Patrol");
    const devex::scene::ComponentType* const mover = registry.find("Mover");
    REQUIRE(patrol != nullptr);
    REQUIRE(mover != nullptr);
    CHECK(patrol->type->findField("points")->list != nullptr);
    CHECK(patrol->type->findField("friends")->kind == devex::reflection::ValueKind::Entity);
    CHECK(patrol->type->findField("leader")->kind == devex::reflection::ValueKind::Entity);
    CHECK(patrol->type->findField("leader")->list == nullptr);

    Scene scene;
    const Entity runner = scene.createEntity("Runner");
    scene.add<devex::scene::Transform>(runner);
    scene.add<devex::scene::PointLight>(runner).intensity = 800.0f;
    static_cast<void>(mover->emplace(scene, runner));
    const Entity guard = scene.createEntity("Guard");
    scene.add<devex::scene::Transform>(guard);
    void* component = patrol->emplace(scene, guard);
    REQUIRE(component != nullptr);
    // The lists start as the C# class makes them.
    CHECK(field<std::vector<devex::math::Vec3>>(*patrol, component, "points").size() == 2);
    CHECK(field<std::vector<std::string>>(*patrol, component, "words") == std::vector<std::string>{"a"});
    field<EntityRef>(*patrol, component, "leader") = scene.reference(runner);

    run(*game, scene, SystemPhase::Start);
    run(*game, scene, SystemPhase::Update, 0.1);
    component = patrol->find(scene, guard) != nullptr ? const_cast<void*>(patrol->find(scene, guard)) : nullptr;
    REQUIRE(component != nullptr);
    const auto& points = field<std::vector<devex::math::Vec3>>(*patrol, component, "points");
    REQUIRE(points.size() == 3);
    CHECK(points[2] == devex::math::Vec3(1.0f, 0.0f, 0.0f));
    const auto& words = field<std::vector<std::string>>(*patrol, component, "words");
    REQUIRE(words.size() == 2);
    CHECK(words[1] == "update 1, start 1");
    CHECK(field<std::vector<EntityRef>>(*patrol, component, "friends") == std::vector<EntityRef>{scene.reference(runner)});
    // The generated view of an engine component changed it in place.
    CHECK(scene.get<devex::scene::PointLight>(runner).intensity == 900.0f);
    // Another C# component changed by this one keeps the change.
    CHECK(field<float>(*mover, const_cast<void*>(mover->find(scene, runner)), "speed") == 9.0f);

    const std::string saved = devex::scene::saveScene(scene);
    CHECK(saved.find(std::format("leader = entity(\"{}\")", scene.uuid(runner))) != std::string::npos);
    CHECK(saved.find("points = list(vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 0, 0))") != std::string::npos);

    // A reload of the code while the game plays: the private fields come back, Start does not run again.
    CHECK(game->release(scene) == 2);
    game->unloadAssembly();
    REQUIRE(game->loadAssembly(DEVEX_TEST_MANAGED_GAME));
    CHECK(devex::scene::restorePreservedComponents(scene) == 2);
    // Registered again, the type has a new description.
    patrol = registry.find("Patrol");
    REQUIRE(patrol != nullptr);
    run(*game, scene, SystemPhase::Update, 0.1);
    component = const_cast<void*>(patrol->find(scene, guard));
    REQUIRE(component != nullptr);
    CHECK(field<std::vector<std::string>>(*patrol, component, "words").back() == "update 2, start 1");

    // A new game starts every component over.
    run(*game, scene, SystemPhase::Start);
    run(*game, scene, SystemPhase::Update, 0.1);
    CHECK(field<std::vector<std::string>>(*patrol, const_cast<void*>(patrol->find(scene, guard)), "words").back() ==
          "update 1, start 1");
    game->unloadAssembly();
}

TEST_CASE("Errors of C# code name their line once, and the game goes on", "[runtime][managed]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    std::vector<std::string> errors;
    const devex::core::LogSinkId sink = devex::core::addLogSink([&errors](const devex::core::LogRecord& record) {
        if (record.level >= devex::core::LogLevel::Error)
        {
            errors.emplace_back(record.message);
        }
    });

    Scene scene;
    const Entity entity = scene.createEntity("Broken");
    scene.add<devex::scene::Transform>(entity);
    static_cast<void>(devex::scene::componentRegistry().find("Faulty")->emplace(scene, entity));
    const devex::scene::ComponentType* const mover = devex::scene::componentRegistry().find("Mover");
    static_cast<void>(mover->emplace(scene, entity));
    run(*game, scene, SystemPhase::Start);
    for (int frame = 0; frame < 3; ++frame)
    {
        run(*game, scene, SystemPhase::Update, 0.5);
    }
    devex::core::removeLogSink(sink);

    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("Faulty.Update of 'Broken' failed") != std::string::npos);
    CHECK(errors[0].find("broken on purpose") != std::string::npos);
    // The symbols are loaded: the stack names the file and the line.
    CHECK(errors[0].find("Mover.cs:line") != std::string::npos);
    // The other components of the entity still run.
    CHECK(scene.get<devex::scene::Transform>(entity).position.x == 3.0f);
    game->unloadAssembly();
}

TEST_CASE("C# components hear collisions and triggers and query the physics", "[runtime][managed]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const bumper = devex::scene::componentRegistry().find("Bumper");
    REQUIRE(bumper != nullptr);

    auto meshes = std::make_shared<std::unordered_map<devex::asset::AssetId, devex::asset::MeshData>>();
    devex::core::Result<std::unique_ptr<devex::physics::PhysicsWorld>> world = devex::physics::PhysicsWorld::create({
        .meshes = [meshes](devex::asset::AssetId id) -> const devex::asset::MeshData* {
            std::optional<devex::asset::MeshData> mesh = devex::asset::makeBuiltinMesh(id);
            return mesh ? &meshes->insert_or_assign(id, std::move(*mesh)).first->second : nullptr;
        },
        .threads = 1,
    });
    REQUIRE(world.has_value());

    // A block that a ball falls on, and a trigger that another ball falls through.
    Scene scene;
    const Entity block = scene.createEntity("Block");
    scene.add<devex::scene::Transform>(block, devex::scene::Transform{.position = {0.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::BoxCollider>(block, devex::scene::BoxCollider{.size = {2.0f, 1.0f, 2.0f}});
    static_cast<void>(bumper->emplace(scene, block));
    const Entity zone = scene.createEntity("Zone");
    scene.add<devex::scene::Transform>(zone, devex::scene::Transform{.position = {10.0f, 2.0f, 0.0f}});
    scene.add<devex::scene::BoxCollider>(zone, devex::scene::BoxCollider{.size = {2.0f, 1.0f, 2.0f}, .trigger = true});
    static_cast<void>(bumper->emplace(scene, zone));
    for (const float x : {0.0f, 10.0f})
    {
        const Entity ball = scene.createEntity(x == 0.0f ? "Ball" : "Faller");
        scene.add<devex::scene::Transform>(ball, devex::scene::Transform{.position = {x, 3.0f, 0.0f}});
        scene.add<devex::scene::RigidBody>(ball);
        scene.add<devex::scene::SphereCollider>(ball, devex::scene::SphereCollider{.radius = 0.25f});
    }

    ManagedGame::Frame frame{.scene = &scene, .physics = world->get()};
    game->runPhase(frame, SystemPhase::Start);
    // Two seconds of play: the physics steps, then the Update phase sees the contacts of the frame.
    for (int step = 0; step < 120; ++step)
    {
        scene.updateTransforms();
        (*world)->step(scene, devex::core::Duration(1.0 / 60.0));
        frame.delta = devex::core::Duration(1.0 / 60.0);
        game->runPhase(frame, SystemPhase::Update);
        (*world)->clearContacts();
    }
    const auto bumperOf = [&](Entity entity) { return const_cast<void*>(bumper->find(scene, entity)); };
    CHECK(field<int>(*bumper, bumperOf(block), "hits") == 1);
    CHECK(field<int>(*bumper, bumperOf(block), "entered") == 0);
    CHECK(field<int>(*bumper, bumperOf(zone), "entered") == 1);
    CHECK(field<int>(*bumper, bumperOf(zone), "left") == 1);
    CHECK(field<int>(*bumper, bumperOf(zone), "hits") == 0);
    // The ray from above the block finds the ball resting on it.
    CHECK(field<std::string>(*bumper, bumperOf(block), "below") == "Ball");
    game->unloadAssembly();
}
#endif
