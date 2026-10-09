#include "runtime/ManagedGame.hpp"
#include "runtime/ManagedCodeBuilder.hpp"

#include <devex/animation/AnimationWorld.hpp>
#include <devex/animation/TweenWorld.hpp>
#include <devex/asset/Artifact.hpp>
#include <devex/asset/Localization.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/audio/AudioWorld.hpp>
#include <devex/audio/Clip.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/navigation/NavMeshBuilder.hpp>
#include <devex/navigation/NavigationWorld.hpp>
#include <devex/particles/ParticleWorld.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/physics2d/Physics2DWorld.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/AudioComponents.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/JointComponents.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/scene/ParticleComponents.hpp>
#include <devex/scene/SpriteComponents.hpp>
#include <devex/scene/TilemapComponents.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/scene/Physics2DComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/ui/UiWorld.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
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

TEST_CASE("Late C# cameras follow interpolated parents at different frame rates", "[runtime][managed][camera]")
{
    const int frameRate = GENERATE(30, 60, 90, 144, 240);
    CAPTURE(frameRate);
    const auto game = startRuntime();
    const auto* cameraType = devex::scene::componentRegistry().find("LateCamera");
    REQUIRE(cameraType != nullptr);
    Scene scene;
    const Entity hero = scene.createEntity("Hero");
    scene.add<devex::scene::Transform>(hero, devex::scene::Transform{
        .rotation = devex::math::Quat{0.9238795f, 0.0f, 0.0f, 0.3826834f}, .scale = {1.5f, 0.75f, 1.0f}});
    scene.add<devex::scene::CharacterController2D>(hero,
        devex::scene::CharacterController2D{.gravityScale = 0.0f});
    const Entity camera = scene.createEntity("Camera");
    scene.add<devex::scene::Transform>(camera);
    REQUIRE(scene.setParent(camera, hero));
    void* const component = cameraType->emplace(scene, camera);
    field<EntityRef>(*cameraType, component, "target") = {scene.uuid(hero)};
    auto physics = devex::physics2d::Physics2DWorld::create({});
    REQUIRE(physics);
    run(*game, scene, SystemPhase::Start);
    int remainder = 0;
    int fixedUpdates = 0;
    for (int frame = 1; frame <= frameRate; ++frame)
    {
        scene.updateTransforms();
        remainder += 60;
        while (remainder >= frameRate)
        {
            run(*game, scene, SystemPhase::FixedUpdate, 1.0 / 60.0);
            scene.get<devex::scene::CharacterController2D>(hero).velocity = {6.0f, 0.0f};
            (*physics)->step(scene, devex::core::Duration(1.0 / 60.0));
            remainder -= frameRate;
            ++fixedUpdates;
        }
        run(*game, scene, SystemPhase::Update, 1.0 / frameRate);
        const float alpha = static_cast<float>(remainder) / static_cast<float>(frameRate);
        scene.updateTransforms();
        (*physics)->interpolate(scene, alpha);
        const devex::math::Vec3 rendered = scene.get<devex::scene::WorldTransform>(hero).matrix[3];
        run(*game, scene, SystemPhase::LateUpdate, 1.0 / frameRate);
        scene.updateTransforms();
        (*physics)->interpolate(scene, alpha);
        const auto position = scene.get<devex::scene::WorldTransform>(camera).matrix[3];
        CHECK(position.x == Catch::Approx(rendered.x).margin(1e-5f));
        CHECK(position.y == Catch::Approx(rendered.y + 1.5f).margin(1e-5f));
        CHECK(position.z == Catch::Approx(10.0f));
        CHECK(field<float>(*cameraType, component, "observed_x") == rendered.x);
        CHECK(field<int>(*cameraType, component, "updates") == frame);
        CHECK(field<int>(*cameraType, component, "fixed_updates") == fixedUpdates);
        CHECK(field<int>(*cameraType, component, "late_updates") == frame);
        CHECK(field<int>(*cameraType, component, "systems") == frame);
        CHECK(field<float>(*cameraType, component, "frame_delta") == Catch::Approx(1.0 / frameRate));
        CHECK(field<float>(*cameraType, component, "elapsed") == Catch::Approx(static_cast<double>(frame) / frameRate));
    }
}

namespace {

class CameraApplication final : public devex::runtime::Application
{
public:
    bool sawInterpolation = false;
    bool sawFrameWithoutStep = false;

    devex::core::Result<void> onStartup() override
    {
        m_cameraType = devex::scene::componentRegistry().find("LateCamera");
        REQUIRE(m_cameraType != nullptr);
        m_hero = scene().createEntity("Hero");
        scene().add<devex::scene::Transform>(m_hero);
        scene().add<devex::scene::CharacterController2D>(m_hero,
            devex::scene::CharacterController2D{.gravityScale = 0.0f});
        m_camera = scene().createEntity("Camera");
        scene().add<devex::scene::Transform>(m_camera);
        REQUIRE(scene().setParent(m_camera, m_hero));
        m_component = m_cameraType->emplace(scene(), m_camera);
        field<EntityRef>(*m_cameraType, m_component, "target") = {scene().uuid(m_hero)};
        return {};
    }

    void onFixedUpdate(devex::core::Duration) override
    {
        ++m_fixedUpdates;
        scene().get<devex::scene::CharacterController2D>(m_hero).velocity = {6.0f, 0.0f};
    }

    void onUpdate(devex::core::Duration) override
    {
        // LateUpdate ran exactly once after each preceding Update, even with no fixed step.
        CHECK(field<int>(*m_cameraType, m_component, "late_updates") == m_updates);
        if (m_updates > 0)
        {
            sawFrameWithoutStep |= m_previousFixed == m_fixedUpdates;
            sawInterpolation |= field<float>(*m_cameraType, m_component, "observed_x") < m_previousSimulationX;
        }
        // Last frame's rendered pose must never leak into this frame's simulation.
        const float x = scene().get<devex::scene::Transform>(m_hero).position.x;
        CHECK(scene().get<devex::scene::WorldTransform>(m_hero).matrix[3].x == x);
        m_previousSimulationX = x;
        m_previousFixed = m_fixedUpdates;
        if (++m_updates == 48)
        {
            requestQuit();
        }
    }

    void onShutdown() override
    {
        const auto hero = scene().get<devex::scene::WorldTransform>(m_hero).matrix[3];
        const auto camera = scene().get<devex::scene::WorldTransform>(m_camera).matrix[3];
        CHECK(field<int>(*m_cameraType, m_component, "late_updates") == m_updates);
        CHECK(field<int>(*m_cameraType, m_component, "fixed_updates") == m_fixedUpdates);
        CHECK(field<int>(*m_cameraType, m_component, "systems") == m_updates);
        CHECK(field<float>(*m_cameraType, m_component, "observed_x") == hero.x);
        CHECK(camera.x == Catch::Approx(hero.x));
        CHECK(camera.y == Catch::Approx(hero.y + 1.5f));
        CHECK(camera.z == Catch::Approx(10.0f));
    }

private:
    const devex::scene::ComponentType* m_cameraType = nullptr;
    void* m_component = nullptr;
    Entity m_hero;
    Entity m_camera;
    int m_updates = 0;
    int m_fixedUpdates = 0;
    int m_previousFixed = 0;
    float m_previousSimulationX = 0.0f;
};

} // namespace

TEST_CASE("The application runs late cameras after interpolation and restores simulation poses", "[runtime][managed][camera]")
{
    // Check .NET availability before starting the application's own managed runtime.
    {
        const auto game = startRuntime();
        game->unloadAssembly();
    }
    const auto directory = std::filesystem::temp_directory_path() / ("devex-camera-" + devex::core::Uuid::generate().toString());
    const auto project = devex::asset::createProject(directory, "Camera test");
    REQUIRE(project);
    const auto assembly = devex::runtime::detail::ManagedCodeBuilder::assemblyPath(*project);
    std::filesystem::create_directories(assembly.parent_path());
    std::filesystem::copy_file(DEVEX_TEST_MANAGED_GAME, assembly);
    CameraApplication application;
    const devex::runtime::ApplicationConfig config{
        .title = "Camera test", .width = 320, .height = 240,
        .fixedUpdateRate = 30, .maxFrameRate = 120, .enableRendering = false,
        .loadGameCode = true, .project = project->file, .watchAssets = false,
        .enableAudio = false, .workerThreads = 1, .userDirectory = directory / "user"};
    CHECK(devex::runtime::run(application, config) == EXIT_SUCCESS);
    CHECK(application.sawInterpolation);
    CHECK(application.sawFrameWithoutStep);
    std::filesystem::remove_all(directory);
}

TEST_CASE("The C# runtime registers components and runs them", "[runtime][managed]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    CHECK(game->componentTypes().size() == 25);

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

TEST_CASE("C# components, systems and zones of the game show in the profiler", "[runtime][managed][profiler]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const mover = devex::scene::componentRegistry().find("Mover");
    REQUIRE(mover != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Runner");
    scene.add<devex::scene::Transform>(entity);
    REQUIRE(mover->emplace(scene, entity) != nullptr);

    devex::core::profiler::clear();
    devex::core::profiler::setEnabled(true);
    devex::core::profiler::beginFrame();
    run(*game, scene, SystemPhase::Start);
    run(*game, scene, SystemPhase::Update, 0.5);
    devex::core::profiler::endFrame();
    devex::core::profiler::setEnabled(false);
    const auto frames = devex::core::profiler::history();
    devex::core::profiler::clear();

    REQUIRE(frames.size() == 1);
    const auto depthOf = [&frames](std::string_view name) {
        for (const devex::core::ProfileZone& zone : frames[0]->cpu)
        {
            if (std::string_view(zone.name) == name)
            {
                return static_cast<int>(zone.depth);
            }
        }
        return -1;
    };
    CHECK(depthOf("C# components in") >= 0);
    CHECK(depthOf("Mover.Start") >= 0);
    CHECK(depthOf("Mover.Update") >= 0);
    CHECK(depthOf("Renamer.Rename") >= 0);
    CHECK(depthOf("C# components out") >= 0);
    // A zone the game opens sits inside the zone of its component.
    CHECK(depthOf("Move") == depthOf("Mover.Update") + 1);
    // Off, the profiler keeps nothing more.
    devex::core::profiler::beginFrame();
    run(*game, scene, SystemPhase::Update, 0.5);
    devex::core::profiler::endFrame();
    CHECK(devex::core::profiler::history().empty());
    game->unloadAssembly();
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
    // A ball welded to the world by a joint too weak for it, which hears of the break.
    const Entity hanger = scene.createEntity("Hanger");
    scene.add<devex::scene::Transform>(hanger, devex::scene::Transform{.position = {20.0f, 3.0f, 0.0f}});
    scene.add<devex::scene::RigidBody>(hanger);
    scene.add<devex::scene::SphereCollider>(hanger, devex::scene::SphereCollider{.radius = 0.25f});
    static_cast<void>(bumper->emplace(scene, hanger));
    const Entity weld = scene.createEntity("Weld");
    scene.add<devex::scene::Transform>(weld, devex::scene::Transform{.position = {20.0f, 3.0f, 0.0f}});
    scene.add<devex::scene::FixedJoint>(weld, devex::scene::FixedJoint{.bodyA = {scene.uuid(hanger)}, .breakForce = 1.0f});

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
    CHECK(field<int>(*bumper, bumperOf(hanger), "breaks") == 1);
    CHECK(field<int>(*bumper, bumperOf(block), "breaks") == 0);
    game->unloadAssembly();
}

TEST_CASE("C# components hear 2D collisions and triggers and drive 2D characters", "[runtime][managed][physics2d]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const bumper = devex::scene::componentRegistry().find("Bumper2D");
    const devex::scene::ComponentType* const walker = devex::scene::componentRegistry().find("Walker2D");
    REQUIRE(bumper != nullptr);
    REQUIRE(walker != nullptr);
    devex::core::Result<std::unique_ptr<devex::physics2d::Physics2DWorld>> world = devex::physics2d::Physics2DWorld::create({});
    REQUIRE(world.has_value());

    // A floor that a crate lands on, a trigger that another crate falls through, and a character
    // that walks and jumps.
    Scene scene;
    const Entity floor = scene.createEntity("Floor");
    scene.add<devex::scene::Transform>(floor, devex::scene::Transform{.position = {0.0f, -0.5f, 0.0f}});
    scene.add<devex::scene::BoxCollider2D>(floor, devex::scene::BoxCollider2D{.size = {40.0f, 1.0f}});
    static_cast<void>(bumper->emplace(scene, floor));
    const Entity zone = scene.createEntity("Zone");
    scene.add<devex::scene::Transform>(zone, devex::scene::Transform{.position = {-10.0f, 2.0f, 0.0f}});
    scene.add<devex::scene::BoxCollider2D>(zone, devex::scene::BoxCollider2D{.size = {2.0f, 1.0f}, .trigger = true});
    static_cast<void>(bumper->emplace(scene, zone));
    const Entity crate = scene.createEntity("Crate");
    scene.add<devex::scene::Transform>(crate, devex::scene::Transform{.position = {5.0f, 0.5f, 0.0f}});
    scene.add<devex::scene::RigidBody2D>(crate);
    scene.add<devex::scene::BoxCollider2D>(crate);
    const Entity faller = scene.createEntity("Faller");
    scene.add<devex::scene::Transform>(faller, devex::scene::Transform{.position = {-10.0f, 4.0f, 0.0f}});
    scene.add<devex::scene::RigidBody2D>(faller);
    scene.add<devex::scene::CircleCollider2D>(faller, devex::scene::CircleCollider2D{.radius = 0.25f});
    const Entity hero = scene.createEntity("Hero");
    scene.add<devex::scene::Transform>(hero, devex::scene::Transform{.position = {-3.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::CharacterController2D>(hero);
    void* const walking = walker->emplace(scene, hero);
    field<EntityRef>(*walker, walking, "crate") = scene.reference(crate);

    ManagedGame::Frame frame{.scene = &scene, .physics2d = world->get()};
    scene.updateTransforms();
    (*world)->step(scene, devex::core::Duration(1.0 / 60.0));
    game->runPhase(frame, SystemPhase::Start);
    float highestCrate = 0.0f;
    for (int step = 0; step < 150; ++step)
    {
        frame.delta = devex::core::Duration(1.0 / 60.0);
        game->runPhase(frame, SystemPhase::FixedUpdate);
        scene.updateTransforms();
        (*world)->step(scene, devex::core::Duration(1.0 / 60.0));
        highestCrate = std::max(highestCrate, scene.get<devex::scene::Transform>(crate).position.y);
        game->runPhase(frame, SystemPhase::Update);
        (*world)->clearContacts();
    }
    const auto bumperOf = [&](Entity entity) { return const_cast<void*>(bumper->find(scene, entity)); };
    CHECK(field<int>(*bumper, bumperOf(floor), "hits") >= 2);
    CHECK(field<int>(*bumper, bumperOf(zone), "entered") == 1);
    CHECK(field<int>(*bumper, bumperOf(zone), "left") == 1);
    CHECK(field<int>(*bumper, bumperOf(zone), "hits") == 0);
    // Rays go through triggers, to the ball resting under the zone.
    CHECK(field<std::string>(*bumper, bumperOf(zone), "below") == "Faller");
    // Around the zone: itself, the floor, and the ball.
    CHECK(field<int>(*bumper, bumperOf(zone), "near") == 3);
    // Kicked up by C# at the start.
    CHECK(highestCrate > 1.0f);
    CHECK(field<bool>(*walker, walking, "jumped"));
    CHECK(field<bool>(*walker, walking, "landed"));
    CHECK(scene.get<devex::scene::Transform>(hero).position.x > 0.0f);
    game->unloadAssembly();
}

TEST_CASE("C# components set the parameters of state machines and read their states", "[runtime][managed][animation]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const conductor = devex::scene::componentRegistry().find("Conductor");
    REQUIRE(conductor != nullptr);

    const devex::asset::AssetId controllerId = devex::asset::AssetId::generate();
    devex::asset::AnimatorData data;
    data.entry = "Idle";
    data.parameters = {{.name = "Speed"}, {.name = "Wave", .type = devex::asset::AnimatorParameterType::Trigger}};
    data.states = {{.name = "Idle"}, {.name = "Walk"}, {.name = "Waving"}};
    data.transitions = {
        {.from = "Idle", .to = "Walk", .duration = 0.0f,
         .conditions = {{.parameter = "Speed", .test = devex::asset::AnimatorTest::Greater, .value = 1.0f}}},
        {.to = "Waving", .duration = 0.0f, .conditions = {{.parameter = "Wave", .test = devex::asset::AnimatorTest::Triggered}}},
    };
    const auto controller = std::make_shared<const devex::asset::AnimatorData>(data);
    devex::animation::AnimationWorld world([](devex::asset::AssetId) { return nullptr; },
                                           [&](devex::asset::AssetId id) { return id == controllerId ? controller : nullptr; });

    Scene scene;
    const Entity dancer = scene.createEntity("Dancer");
    scene.add<devex::scene::Transform>(dancer);
    scene.add<devex::scene::Animator>(dancer, devex::scene::Animator{.controller = controllerId});
    void* const component = conductor->emplace(scene, dancer);

    ManagedGame::Frame frame{.scene = &scene, .animation = &world};
    game->runPhase(frame, SystemPhase::Start);
    for (int step = 0; step < 5; ++step)
    {
        frame.delta = devex::core::Duration(1.0 / 60.0);
        game->runPhase(frame, SystemPhase::Update);
        world.update(scene, devex::core::Duration(1.0 / 60.0));
    }
    CHECK(field<std::string>(*conductor, component, "state") == "Waving");
    CHECK(field<float>(*conductor, component, "speed") == 2.0f);
    CHECK(field<bool>(*conductor, component, "waved"));
    CHECK(world.parameter(dancer, "Wave") == 0.0f);
    game->unloadAssembly();
}

TEST_CASE("C# components send navigation agents and query the navigation mesh", "[runtime][managed][navigation]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const navigator = devex::scene::componentRegistry().find("Navigator");
    REQUIRE(navigator != nullptr);

    // A floor, and a wall to walk around.
    Scene scene;
    for (const auto& [position, size] : {std::pair{devex::math::Vec3{0.0f, -0.5f, 0.0f}, devex::math::Vec3{20.0f, 1.0f, 20.0f}},
                                         std::pair{devex::math::Vec3{0.0f, 1.0f, -3.0f}, devex::math::Vec3{1.0f, 2.0f, 14.0f}}})
    {
        const Entity box = scene.createEntity("Box");
        scene.add<devex::scene::Transform>(box, devex::scene::Transform{.position = position});
        scene.add<devex::scene::BoxCollider>(box, devex::scene::BoxCollider{.size = size});
    }
    const devex::asset::AssetId navMeshId = devex::asset::AssetId::generate();
    const Entity surface = scene.createEntity("Navigation");
    scene.add<devex::scene::NavMeshSurface>(surface, devex::scene::NavMeshSurface{.navMesh = navMeshId});
    scene.updateTransforms();
    devex::core::Result<devex::asset::NavMeshData> baked =
        devex::navigation::bakeNavMesh(devex::navigation::collectGeometry(scene, {}), {});
    REQUIRE(baked.has_value());
    const auto navMesh = std::make_shared<const devex::asset::NavMeshData>(std::move(*baked));
    devex::core::Result<std::unique_ptr<devex::navigation::NavigationWorld>> world = devex::navigation::NavigationWorld::create({
        .navMeshes = [&](devex::asset::AssetId id) { return id == navMeshId ? navMesh : nullptr; },
    });
    REQUIRE(world.has_value());

    const Entity agent = scene.createEntity("Agent");
    scene.add<devex::scene::Transform>(agent, devex::scene::Transform{.position = {-5.0f, 0.0f, -5.0f}});
    scene.add<devex::scene::NavMeshAgent>(agent);
    void* const component = navigator->emplace(scene, agent);
    field<devex::math::Vec3>(*navigator, component, "target") = {5.0f, 0.0f, -5.0f};
    scene.updateTransforms();
    (*world)->update(scene, devex::core::Duration(0.0));

    ManagedGame::Frame frame{.scene = &scene, .navigation = world->get()};
    game->runPhase(frame, SystemPhase::Start);
    for (int step = 0; step < 60 * 12; ++step)
    {
        frame.delta = devex::core::Duration(1.0 / 60.0);
        game->runPhase(frame, SystemPhase::Update);
        scene.updateTransforms();
        (*world)->update(scene, devex::core::Duration(1.0 / 60.0));
    }
    CHECK(field<bool>(*navigator, component, "sent"));
    CHECK(field<int>(*navigator, component, "corners") >= 3);
    CHECK(field<bool>(*navigator, component, "sampled"));
    CHECK(field<float>(*navigator, component, "sampled_height") == Catch::Approx(0.0f).margin(0.2f));
    CHECK(field<bool>(*navigator, component, "blocked"));
    CHECK(field<bool>(*navigator, component, "arrived"));
    const devex::math::Vec3 reached = scene.get<devex::scene::Transform>(agent).position;
    CHECK(std::abs(reached.x - 5.0f) < 0.3f);
    CHECK(std::abs(reached.z + 5.0f) < 0.3f);
    game->unloadAssembly();
}

TEST_CASE("C# components open popups and read context menus and double clicks", "[runtime][managed][ui]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const menus = devex::scene::componentRegistry().find("Menus");
    REQUIRE(menus != nullptr);

    Scene scene;
    const Entity canvas = scene.createEntity("Canvas");
    scene.add<devex::scene::Canvas>(canvas, devex::scene::Canvas{.scaleMode = devex::scene::CanvasScaleMode::ConstantPixels});
    const auto element = [&](const char* name, devex::math::Vec2 min, devex::math::Vec2 max) {
        const Entity entity = scene.createEntity(name);
        REQUIRE(scene.setParent(entity, canvas).has_value());
        scene.add<devex::scene::UiRect>(entity, devex::scene::UiRect{.anchorMin = {0.0f, 0.0f},
                                                                      .anchorMax = {0.0f, 0.0f},
                                                                      .offsetMin = min,
                                                                      .offsetMax = max});
        return entity;
    };
    const Entity popup = element("Menu", {0.0f, 0.0f}, {200.0f, 60.0f});
    scene.add<devex::scene::UiPopup>(popup);
    scene.get<devex::scene::UiRect>(popup).visible = false;
    const Entity row = element("Row", {100.0f, 400.0f}, {500.0f, 440.0f});
    scene.add<devex::scene::UiImage>(row);
    scene.add<devex::scene::UiButton>(row);
    scene.add<devex::scene::UiContextMenu>(row, devex::scene::UiContextMenu{.popup = scene.reference(popup)});
    const Entity choice = element("Choice", {100.0f, 500.0f}, {300.0f, 530.0f});
    scene.add<devex::scene::UiDropdown>(choice, devex::scene::UiDropdown{.options = {"Easy", "Hard"}});

    const Entity holder = scene.createEntity("Holder");
    void* const component = menus->emplace(scene, holder);
    field<devex::scene::EntityRef>(*menus, component, "popup") = scene.reference(popup);
    field<devex::scene::EntityRef>(*menus, component, "row") = scene.reference(row);
    field<devex::scene::EntityRef>(*menus, component, "choice") = scene.reference(choice);

    devex::ui::UiWorld world;
    const devex::math::Vec2 window{1280.0f, 720.0f};
    const devex::core::Duration step{1.0 / 60.0};
    ManagedGame::Frame frame{.scene = &scene, .ui = &world};
    game->runPhase(frame, SystemPhase::Start);
    // Each frame, the interface answers the pointer, then the game reads what it did.
    const std::array<devex::ui::UiInput, 7> inputs{{
        {},
        {.pointer = {300.0f, 420.0f}, .secondaryPressed = true},
        {.pointer = {300.0f, 420.0f}, .pointerDown = true, .pointerPressed = true},
        {.pointer = {300.0f, 420.0f}, .pointerReleased = true},
        {.pointer = {300.0f, 420.0f}, .pointerDown = true, .pointerPressed = true},
        {.pointer = {300.0f, 420.0f}, .pointerReleased = true},
        {},
    }};
    for (const devex::ui::UiInput& input : inputs)
    {
        world.update(scene, window, input, step);
        frame.delta = step;
        game->runPhase(frame, SystemPhase::Update);
    }
    CHECK(field<bool>(*menus, component, "opened"));
    CHECK(field<int>(*menus, component, "options") == 2);
    CHECK(field<std::string>(*menus, component, "second_option") == "Hard");
    CHECK(field<bool>(*menus, component, "target_seen"));
    CHECK(field<bool>(*menus, component, "double_clicked"));
    CHECK(field<bool>(*menus, component, "closed"));
    game->unloadAssembly();
}

TEST_CASE("C# components colour the runs of an area of text, counted in their characters", "[runtime][managed][ui]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const colors = devex::scene::componentRegistry().find("TextColors");
    REQUIRE(colors != nullptr);
    static const devex::asset::FontData baked = [] {
        devex::asset::ImportContext context{
            .source = std::filesystem::path{DEVEX_TEST_DATA_DIRECTORY} / "fonts" / "NotoSans-Regular.ttf",
            .mainId = devex::asset::AssetId::generate(),
            .name = "font",
            .options = {{"size", devex::serialization::TextValue(32.0)}},
        };
        const auto result = devex::asset::importFontFile(context);
        REQUIRE(result.has_value());
        const auto data = devex::asset::decodeFont(result->artifacts.front().bytes);
        REQUIRE(data.has_value());
        return *data;
    }();

    Scene scene;
    const Entity canvas = scene.createEntity("Canvas");
    scene.add<devex::scene::Canvas>(canvas, devex::scene::Canvas{.scaleMode = devex::scene::CanvasScaleMode::ConstantPixels});
    const Entity area = scene.createEntity("Notes");
    REQUIRE(scene.setParent(area, canvas).has_value());
    scene.add<devex::scene::UiRect>(area, devex::scene::UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {100.0f, 100.0f},
                                                               .offsetMax = {500.0f, 300.0f}});
    // "é", a face beyond the first plane, then the word.
    scene.add<devex::scene::UiText>(area, devex::scene::UiText{.text = "\xC3\xA9\xF0\x9F\x98\x80 red\nsecond\nthird", .size = 20.0f});
    scene.add<devex::scene::UiTextArea>(area, devex::scene::UiTextArea{.scrollbarSize = 0.0f});
    const Entity holder = scene.createEntity("Holder");
    void* const component = colors->emplace(scene, holder);
    field<devex::scene::EntityRef>(*colors, component, "area") = scene.reference(area);

    const auto fontOf = [](devex::asset::AssetId) {
        devex::render::TextureHandle atlas;
        atlas.index = 0;
        atlas.generation = 1;
        return devex::ui::FontRef{.data = &baked, .atlas = atlas};
    };
    devex::ui::UiWorld world;
    world.setFonts(fontOf);
    ManagedGame::Frame frame{.scene = &scene, .ui = &world};
    // The colours are given before the interface ever saw the area.
    game->runPhase(frame, SystemPhase::Start);
    world.update(scene, devex::math::Vec2{1280.0f, 720.0f}, devex::ui::UiInput{}, devex::core::Duration(1.0 / 60.0));
    game->runPhase(frame, SystemPhase::Update);
    CHECK(field<int>(*colors, component, "first") == 0);
    CHECK(field<int>(*colors, component, "count") == 3);

    devex::render::RenderWorld drawn;
    world.build(scene, devex::ui::DrawContext{.fonts = fontOf}, drawn);
    const auto quadsOf = [&](devex::math::Vec4 color) {
        std::vector<float> tops;
        for (std::size_t vertex = 0; vertex + 3 < drawn.uiVertices.size(); vertex += 4)
        {
            if (drawn.uiVertices[vertex].color == color)
            {
                tops.push_back(drawn.uiVertices[vertex].position.y);
            }
        }
        return tops;
    };
    // The three letters of the word, all on the first line.
    const std::vector<float> red = quadsOf({1.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(red.size() == 3);
    CHECK(std::ranges::max(red) - std::ranges::min(red) < 10.0f);
    const std::vector<float> plain = quadsOf({1.0f, 1.0f, 1.0f, 1.0f});
    REQUIRE_FALSE(plain.empty());
    CHECK(std::ranges::min(red) < std::ranges::min(plain) + 10.0f);
    // A box behind the first letter, and the second line underlined, paler.
    CHECK(quadsOf({1.0f, 1.0f, 0.0f, 0.5f}).size() == 1);
    CHECK(quadsOf({0.0f, 1.0f, 0.0f, 0.35f}).size() == 1);
    game->unloadAssembly();
}

TEST_CASE("C# components read what the pointer carries and where it was dropped", "[runtime][managed][ui]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const drops = devex::scene::componentRegistry().find("Drops");
    REQUIRE(drops != nullptr);

    Scene scene;
    const Entity canvas = scene.createEntity("Canvas");
    scene.add<devex::scene::Canvas>(canvas, devex::scene::Canvas{.scaleMode = devex::scene::CanvasScaleMode::ConstantPixels});
    const auto element = [&](const char* name, devex::math::Vec2 min, devex::math::Vec2 max) {
        const Entity entity = scene.createEntity(name);
        REQUIRE(scene.setParent(entity, canvas).has_value());
        scene.add<devex::scene::UiRect>(entity, devex::scene::UiRect{.anchorMin = {0.0f, 0.0f},
                                                                      .anchorMax = {0.0f, 0.0f},
                                                                      .offsetMin = min,
                                                                      .offsetMax = max});
        scene.add<devex::scene::UiImage>(entity);
        return entity;
    };
    const Entity gem = element("Gem", {100.0f, 100.0f}, {160.0f, 160.0f});
    scene.add<devex::scene::UiDragSource>(gem, devex::scene::UiDragSource{.type = "item", .data = "ruby"});
    const Entity slot = element("Slot", {400.0f, 100.0f}, {480.0f, 180.0f});
    scene.add<devex::scene::UiDropTarget>(slot, devex::scene::UiDropTarget{.accepts = {"item"}, .action = "slot"});

    const Entity holder = scene.createEntity("Holder");
    void* const component = drops->emplace(scene, holder);
    field<devex::scene::EntityRef>(*drops, component, "gem") = scene.reference(gem);
    field<devex::scene::EntityRef>(*drops, component, "slot") = scene.reference(slot);

    devex::ui::UiWorld world;
    const devex::math::Vec2 window{1280.0f, 720.0f};
    const devex::core::Duration step{1.0 / 60.0};
    ManagedGame::Frame frame{.scene = &scene, .ui = &world};
    game->runPhase(frame, SystemPhase::Start);
    const std::array<devex::ui::UiInput, 5> inputs{{
        {},
        {.pointer = {130.0f, 130.0f}, .pointerDown = true, .pointerPressed = true},
        {.pointer = {300.0f, 140.0f}, .pointerDown = true, .pointerMoved = true},
        {.pointer = {440.0f, 140.0f}, .pointerDown = true, .pointerMoved = true},
        {.pointer = {440.0f, 140.0f}, .pointerReleased = true},
    }};
    for (const devex::ui::UiInput& input : inputs)
    {
        world.update(scene, window, input, step);
        frame.delta = step;
        game->runPhase(frame, SystemPhase::Update);
    }
    CHECK(field<bool>(*drops, component, "carrying"));
    CHECK(field<bool>(*drops, component, "dropped"));
    CHECK(field<bool>(*drops, component, "from_gem"));
    CHECK(field<std::string>(*drops, component, "data") == "ruby");
    // Let go in the middle of the slot.
    CHECK(field<float>(*drops, component, "at_x") == Catch::Approx(0.5f));
    CHECK(field<float>(*drops, component, "at_y") == Catch::Approx(0.5f));
    game->unloadAssembly();
}

TEST_CASE("C# code reads the input actions of the project and binds them to other keys", "[runtime][managed][input]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const pilot = devex::scene::componentRegistry().find("Pilot");
    REQUIRE(pilot != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Pilot");
    REQUIRE(pilot->emplace(scene, entity) != nullptr);
    const auto value = [&]<typename T>(const char* name, T) -> T& {
        return field<T>(*pilot, const_cast<void*>(pilot->find(scene, entity)), name);
    };

    using devex::asset::InputDirection;
    devex::runtime::InputActions actions(devex::asset::InputSettings{
        .actions = {
            {.name = "Jump", .context = "Gameplay", .bindings = {{"key:Space"}}},
            {.name = "Throttle", .kind = devex::asset::InputActionKind::Axis, .bindings = {{"key:S", InputDirection::Negative}}},
            {.name = "Move", .kind = devex::asset::InputActionKind::Vector, .bindings = {{"key:D", InputDirection::Right}}},
        }});
    devex::platform::Input input;
    input.beginFrame();
    input.setKeyDown(devex::platform::Key::Space, true);
    input.setKeyDown(devex::platform::Key::S, true);
    input.setKeyDown(devex::platform::Key::D, true);
    actions.update(input, false);

    ManagedGame::Frame frame{.scene = &scene, .input = &input, .actions = &actions};
    game->runPhase(frame, SystemPhase::Update);
    CHECK(value("jumping", bool{}));
    CHECK(value("jump_pressed", bool{}));
    CHECK(value("throttle", float{}) == -1.0f);
    CHECK(value("move_x", float{}) == 1.0f);
    CHECK(value("move_y", float{}) == 0.0f);
    CHECK(value("jump_label", std::string{}) == "Space");
    CHECK(value("unknown_action_throws", bool{}));
    CHECK(value("gameplay_active", bool{}));

    // The game waits for a key, then turns its context off.
    value("listen", bool{}) = true;
    game->runPhase(frame, SystemPhase::Update);
    CHECK(value("listening", bool{}));
    CHECK(actions.isListening());
    value("stop_gameplay", bool{}) = true;
    game->runPhase(frame, SystemPhase::Update);
    CHECK_FALSE(value("gameplay_active", bool{}));
    CHECK_FALSE(actions.isContextActive("Gameplay"));
    game->unloadAssembly();
}

TEST_CASE("C# code saves objects into slots and keeps the settings of the player", "[runtime][managed][saves]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const archivist = devex::scene::componentRegistry().find("Archivist");
    REQUIRE(archivist != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Archivist");
    REQUIRE(archivist->emplace(scene, entity) != nullptr);
    const auto value = [&]<typename T>(const char* name, T) -> T& {
        return field<T>(*archivist, const_cast<void*>(archivist->find(scene, entity)), name);
    };

    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / ("devex-managed-saves-" + devex::core::Uuid::generate().toString());
    devex::runtime::SaveGames saves(folder);
    devex::runtime::PlayerSettings settings;
    ManagedGame::Frame frame{.scene = &scene, .saves = &saves, .settings = &settings};
    game->runPhase(frame, SystemPhase::Update);

    CHECK(value("saved", bool{}));
    CHECK(value("loaded", bool{}));
    CHECK(value("loaded_level", int{}) == 5);
    CHECK(value("loaded_name", std::string{}) == "Léa");
    CHECK(value("loaded_items", int{}) == 2);
    CHECK(value("loaded_checkpoint_y", float{}) == 2.0f);
    CHECK(value("loaded_rank", std::string{}) == "Gold");
    CHECK(value("slot_count", int{}) == 1);
    CHECK(value("first_label", std::string{}) == "Cave");
    CHECK(value("first_version", int{}) == 2);
    CHECK(value("missing_is_null", bool{}));
    CHECK(value("master_volume", float{}) == 0.3f);
    CHECK(value("language", std::string{}) == "fr");
    CHECK(value("difficulty", int{}) == 3);
    CHECK(value("deleted", bool{}));
    CHECK(settings.volume(devex::runtime::PlayerSettings::master) == 0.3f);
    CHECK(settings.stringValue("language", "") == "fr");
    CHECK(settings.integerValue("difficulty", 0) == 3);
    game->unloadAssembly();
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

TEST_CASE("C# code reads the translations of the game and changes its language", "[runtime][managed][translation]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const translator = devex::scene::componentRegistry().find("Translator");
    REQUIRE(translator != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Translator");
    REQUIRE(translator->emplace(scene, entity) != nullptr);
    const auto value = [&](const char* name) -> std::string& {
        return field<std::string>(*translator, const_cast<void*>(translator->find(scene, entity)), name);
    };

    const auto table = devex::asset::parseTranslationCsv("keys,en,fr\nHELLO,Hello,Bonjour\nSCORE,{points} points,{points} points gagnés\n");
    REQUIRE(table.has_value());
    const std::array tables{std::make_shared<const devex::asset::TranslationData>(*table)};
    devex::asset::Localization localization;
    localization.setTables(tables);
    localization.setLanguage("en");
    ManagedGame::Frame frame{.scene = &scene, .localization = &localization};
    game->runPhase(frame, SystemPhase::Update);

    CHECK(value("before") == "Hello");
    // The closest language the tables have.
    CHECK(value("language") == "fr");
    CHECK(localization.language() == "fr");
    CHECK(value("greeting") == "Bonjour");
    CHECK(value("score") == "12 points gagnés");
    CHECK(value("missing") == "Not a key");
    CHECK(value("languages") == "en,fr");
    CHECK(value("native") == "Français");
    game->unloadAssembly();
}

TEST_CASE("C# coroutines wait for frames, time, tweens and tasks, and end with their component", "[runtime][managed][coroutine]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    devex::scene::ComponentRegistry& registry = devex::scene::componentRegistry();
    const devex::scene::ComponentType* const choreographer = registry.find("Choreographer");
    const devex::scene::ComponentType* const reader = registry.find("TickReader");
    REQUIRE(choreographer != nullptr);
    REQUIRE(reader != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Dancer");
    scene.add<devex::scene::Transform>(entity);
    REQUIRE(choreographer->emplace(scene, entity) != nullptr);
    const Entity counter = scene.createEntity("Counter");
    REQUIRE(reader->emplace(scene, counter) != nullptr);
    const auto value = [&]<typename T>(const char* name, T) -> T& {
        return field<T>(*choreographer, const_cast<void*>(choreographer->find(scene, entity)), name);
    };
    const auto ticks = [&] {
        return field<std::int32_t>(*reader, const_cast<void*>(reader->find(scene, counter)), "ticks");
    };

    devex::animation::TweenWorld tweens;
    ManagedGame::Frame frame{.scene = &scene, .tweens = &tweens};
    game->runPhase(frame, SystemPhase::Start);
    CHECK(value("step", std::int32_t{}) == 1);
    // As the engine does: the coroutines during Update, then the tweens.
    const auto update = [&](double seconds) {
        frame.delta = devex::core::Duration(seconds);
        game->runPhase(frame, SystemPhase::Update);
        tweens.update(scene, devex::core::Duration(seconds));
    };
    update(0.1);
    CHECK(value("step", std::int32_t{}) == 2);
    update(0.3);
    CHECK(value("step", std::int32_t{}) == 2);
    update(0.3);
    CHECK(value("step", std::int32_t{}) == 3);
    CHECK(scene.get<devex::scene::Transform>(entity).position.y == Catch::Approx(0.6f));
    update(0.5);
    update(0.5);
    CHECK(value("step", std::int32_t{}) == 3);
    CHECK(scene.get<devex::scene::Transform>(entity).position.y == Catch::Approx(2.0f));
    update(0.1);
    CHECK(value("step", std::int32_t{}) == 4);
    // The awaited coroutine fails, and its caller catches what it threw.
    update(0.1);
    CHECK(value("nested_failed", bool{}));
    CHECK(value("tween_failed", bool{}));
    CHECK(value("step", std::int32_t{}) == 4);
    // Task.Yield goes on in the next Update, on the thread of the game.
    update(0.1);
    CHECK(value("step", std::int32_t{}) == 5);
    CHECK(value("on_game_thread", bool{}));
    // The tween of the sequence, its interval, then the frame the coroutine sees it over.
    for (int index = 0; index < 4; ++index)
    {
        update(0.3);
    }
    CHECK(scene.get<devex::scene::Transform>(entity).scale.x == Catch::Approx(2.0f));
    CHECK(value("step", std::int32_t{}) == 5);
    update(0.1);
    CHECK(value("step", std::int32_t{}) == 6);
    update(0.1);
    CHECK(value("step", std::int32_t{}) == 6);
    value("release", bool{}) = true;
    update(0.1);
    CHECK(value("step", std::int32_t{}) == 7);

    // The coroutine that counts frames ends with its component.
    const std::int32_t counted = ticks();
    CHECK(counted > 10);
    choreographer->remove(scene, entity);
    update(0.1);
    const std::int32_t after = ticks();
    update(0.1);
    update(0.1);
    CHECK(ticks() == after);
    CHECK(after <= counted + 1);
    game->unloadAssembly();
}

TEST_CASE("C# code bursts, reads and stops the particles of an emitter", "[runtime][managed][particles]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("Pyrotechnician");
    REQUIRE(type != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Fireworks");
    scene.add<devex::scene::Transform>(entity);
    devex::scene::ParticleEmitter settings;
    settings.playOnStart = false;
    settings.lifetime = {10.0f, 10.0f};
    scene.add<devex::scene::ParticleEmitter>(entity, settings);
    REQUIRE(type->emplace(scene, entity) != nullptr);
    const auto value = [&]<typename T>(const char* name, T) -> T& {
        return field<T>(*type, const_cast<void*>(type->find(scene, entity)), name);
    };

    devex::particles::ParticleWorld particles;
    ManagedGame::Frame frame{.scene = &scene, .delta = devex::core::Duration(0.1), .particles = &particles};
    for (int index = 0; index < 3; ++index)
    {
        game->runPhase(frame, SystemPhase::Update);
        scene.updateTransforms();
        particles.update(scene, devex::core::Duration(0.1));
    }
    CHECK(value("emitted", std::int32_t{}) == 5);
    CHECK(value("playing", bool{}));
    CHECK(value("stopped", bool{}));
    game->unloadAssembly();
}

TEST_CASE("C# code plays the animations of sprites and flips them", "[runtime][managed][sprite]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("Animator2D");
    REQUIRE(type != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Hero");
    scene.add<devex::scene::Transform>(entity);
    scene.add<devex::scene::SpriteRenderer>(entity);
    scene.add<devex::scene::SpriteAnimator>(entity, devex::scene::SpriteAnimator{.animation = "idle"});
    REQUIRE(type->emplace(scene, entity) != nullptr);
    const auto value = [&]<typename T>(const char* name, T) -> T& {
        return field<T>(*type, const_cast<void*>(type->find(scene, entity)), name);
    };

    run(*game, scene, SystemPhase::Update, 0.1);
    CHECK(scene.get<devex::scene::SpriteAnimator>(entity).animation == "run");
    CHECK(scene.get<devex::scene::SpriteRenderer>(entity).flipX);
    run(*game, scene, SystemPhase::Update, 0.1);
    CHECK(value("shown", std::string{}) == "run 2");
    run(*game, scene, SystemPhase::Update, 0.1);
    CHECK(value("restarted", bool{}));
    game->unloadAssembly();
}

TEST_CASE("C# components hear the events of their animations", "[runtime][managed][animation]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const listener = devex::scene::componentRegistry().find("Listener");
    REQUIRE(listener != nullptr);
    const devex::asset::AssetId framesId = devex::asset::AssetId::generate();
    const auto frames = std::make_shared<const devex::asset::SpriteFramesData>(devex::asset::SpriteFramesData{
        .animations = {{.name = "run",
                        .fps = 10.0f,
                        .frames = {devex::asset::AssetId::generate(), devex::asset::AssetId::generate(), devex::asset::AssetId::generate()},
                        .events = {{.frame = 0, .name = "start"}, {.frame = 2, .name = "step"}}}}});
    devex::animation::AnimationWorld world([](devex::asset::AssetId) { return nullptr; }, {},
                                           [&](devex::asset::AssetId id) { return id == framesId ? frames : nullptr; });

    Scene scene;
    const Entity knight = scene.createEntity("Knight");
    scene.add<devex::scene::Transform>(knight);
    scene.add<devex::scene::SpriteAnimator>(knight, devex::scene::SpriteAnimator{.frames = framesId, .animation = "run"});
    void* const component = listener->emplace(scene, knight);
    // Another entity hears nothing of the knight's.
    const Entity other = scene.createEntity("Other");
    void* const deaf = listener->emplace(scene, other);

    ManagedGame::Frame frame{.scene = &scene, .animation = &world};
    game->runPhase(frame, SystemPhase::Start);
    world.update(scene, devex::core::Duration(0.25));
    frame.delta = devex::core::Duration(0.25);
    game->runPhase(frame, SystemPhase::LateUpdate);
    CHECK(field<std::string>(*listener, component, "heard") == "start;step;");
    CHECK(field<int>(*listener, component, "listed") == 2);
    CHECK(field<std::string>(*listener, deaf, "heard").empty());
    game->unloadAssembly();
}

TEST_CASE("C# code paints and reads the cells of a tilemap", "[runtime][managed][tilemap]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("Tiler");
    REQUIRE(type != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Level");
    scene.add<devex::scene::Transform>(entity, devex::scene::Transform{.position = {10.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::Tilemap>(entity, devex::scene::Tilemap{.cellSize = {2.0f, 2.0f}});
    scene.updateTransforms();
    REQUIRE(type->emplace(scene, entity) != nullptr);
    const auto value = [&]<typename T>(const char* name, T) -> T& {
        return field<T>(*type, const_cast<void*>(type->find(scene, entity)), name);
    };

    run(*game, scene, SystemPhase::Update, 0.1);
    const devex::scene::Tilemap& tilemap = scene.get<devex::scene::Tilemap>(entity);
    CHECK(devex::scene::tileAt(tilemap, {2, 3}) == (5 | devex::scene::tileFlipX));
    CHECK(devex::scene::tileAt(tilemap, {4, 4}) == 0);
    CHECK(value("read", std::int32_t{}) == 50);
    CHECK(value("cell_x", std::int32_t{}) == 1);
    CHECK(value("cell_y", std::int32_t{}) == -1);
    CHECK(value("center", devex::math::Vec3{}) == devex::math::Vec3{13.0f, 3.0f, 0.0f});
    game->unloadAssembly();
}

namespace {

// One tileset, without a project on disk: a terrain matched by its sides, a tile for each pattern.
class OneTileset final : public devex::asset::AssetSource
{
public:
    OneTileset()
    {
        m_info = {.id = id, .type = devex::asset::AssetType::Tileset, .name = "ground", .source = id};
        devex::asset::TilesetData tileset;
        tileset.terrainSets.push_back({.mode = devex::asset::TerrainMode::Sides, .terrains = {{.name = "Ground"}}});
        for (std::uint32_t mask = 0; mask < 16; ++mask)
        {
            devex::asset::TileData tile{.id = mask + 1, .terrainSet = 0, .terrain = 0};
            for (std::size_t side = 0; side < 4; ++side)
            {
                tile.terrainBits[side * 2] = (mask & (1u << side)) != 0 ? 0 : devex::asset::noTerrain;
            }
            tileset.tiles.push_back(tile);
        }
        m_bytes = devex::asset::encodeTileset(tileset);
    }

    [[nodiscard]] const devex::asset::Project& project() const noexcept override
    {
        return m_project;
    }

    [[nodiscard]] const devex::asset::AssetInfo* find(devex::asset::AssetId asset) const override
    {
        return asset == id ? &m_info : nullptr;
    }

    [[nodiscard]] std::vector<devex::asset::AssetInfo> assets(std::optional<devex::asset::AssetType> /*type*/) const override
    {
        return {m_info};
    }

    [[nodiscard]] std::optional<devex::asset::AssetId> findByPath(std::string_view /*resourcePath*/) const override
    {
        return std::nullopt;
    }

    [[nodiscard]] devex::core::Result<std::vector<std::byte>> loadArtifact(devex::asset::AssetId asset) const override
    {
        if (asset != id)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "no such asset");
        }
        return m_bytes;
    }

    [[nodiscard]] devex::core::Result<std::string> sceneText(devex::asset::AssetId /*asset*/) const override
    {
        return devex::core::makeError(devex::core::ErrorCode::NotFound, "no scenes here");
    }

    const devex::asset::AssetId id = devex::asset::AssetId::generate();

private:
    devex::asset::Project m_project;
    devex::asset::AssetInfo m_info;
    std::vector<std::byte> m_bytes;
};

} // namespace

TEST_CASE("C# code paints terrains on a tilemap and reads them", "[runtime][managed][tilemap][terrain]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("TerrainPainter");
    REQUIRE(type != nullptr);
    OneTileset source;
    devex::runtime::AssetManager assets(nullptr, &source);
    Scene scene;
    const Entity entity = scene.createEntity("Level");
    scene.add<devex::scene::Tilemap>(entity, devex::scene::Tilemap{.tileset = source.id});
    REQUIRE(type->emplace(scene, entity) != nullptr);
    const auto value = [&](const char* name) -> std::int32_t& {
        return field<std::int32_t>(*type, const_cast<void*>(type->find(scene, entity)), name);
    };

    ManagedGame::Frame frame{.scene = &scene, .delta = devex::core::Duration(0.1), .assetManager = &assets};
    game->runPhase(frame, SystemPhase::Update);
    CHECK(value("ground_set") == 0);
    CHECK(value("ground") == 0);
    CHECK(value("missing_set") == -1);
    CHECK(value("read_set") == 0);
    CHECK(value("read_terrain") == 0);
    CHECK(value("empty_set") == -1);
    // A row of three, its right end erased: the first opens to the right, the second to the left.
    const devex::scene::Tilemap& tilemap = scene.get<devex::scene::Tilemap>(entity);
    CHECK(devex::scene::tileAt(tilemap, {0, 0}) == 1 + 0b0001);
    CHECK(value("middle") == 1 + 0b0100);
    CHECK(devex::scene::tileAt(tilemap, {2, 0}) == 0);
    // A path upwards, apart from the row.
    CHECK(devex::scene::tileAt(tilemap, {0, 2}) == 1 + 0b0010);
    CHECK(devex::scene::tileAt(tilemap, {0, 3}) == 1 + 0b1000);
    game->unloadAssembly();
}

TEST_CASE("C# code loads scenes in the background and reads how far they are", "[runtime][managed]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const loader = devex::scene::componentRegistry().find("Loader");
    REQUIRE(loader != nullptr);
    Scene scene;
    const Entity entity = scene.createEntity("Loader");
    void* const component = loader->emplace(scene, entity);
    REQUIRE(component != nullptr);
    const devex::asset::AssetId next{devex::core::Uuid::generate()};
    field<devex::asset::AssetId>(*loader, component, "target") = next;
    run(*game, scene, SystemPhase::Start);

    // A scene already loading: the game reads its progress and asks for nothing.
    ManagedGame::Frame loading{.scene = &scene, .loadingScene = next, .loadingProgress = 0.25f};
    game->runPhase(loading, SystemPhase::Update);
    const void* const updated = loader->find(scene, entity);
    CHECK(field<bool>(*loader, const_cast<void*>(updated), "loading"));
    CHECK(field<float>(*loader, const_cast<void*>(updated), "progress") == 0.25f);
    CHECK_FALSE(loading.sceneToLoadInBackground.isValid());
    // Without an asset manager, assets count as ready.
    CHECK(field<bool>(*loader, const_cast<void*>(updated), "ready"));

    // None loading: the game asks for one.
    ManagedGame::Frame idle{.scene = &scene};
    game->runPhase(idle, SystemPhase::Update);
    CHECK_FALSE(field<bool>(*loader, const_cast<void*>(loader->find(scene, entity)), "loading"));
    CHECK(field<float>(*loader, const_cast<void*>(loader->find(scene, entity)), "progress") == 0.0f);
    CHECK(idle.sceneToLoadInBackground == next);
    game->unloadAssembly();
}

TEST_CASE("C# components play sounds and change the volumes of the groups", "[runtime][managed][audio]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    const devex::scene::ComponentType* const jukebox = devex::scene::componentRegistry().find("Jukebox");
    REQUIRE(jukebox != nullptr);
    const devex::reflection::FieldInfo* const group = jukebox->type->findField("group");
    REQUIRE(group != nullptr);
    CHECK(group->audioGroup);
    CHECK(jukebox->type->findField("clip")->assetType == "audio");

    // A tone mixed without a sound output.
    std::vector<std::byte> bytes =
        devex::core::readBinaryFile(std::filesystem::path(DEVEX_TEST_DATA_DIRECTORY) / "audio" / "tone.wav").value();
    const devex::core::Result<devex::audio::ClipInfo> info = devex::audio::probeClip(bytes);
    REQUIRE(info.has_value());
    const std::shared_ptr<const devex::audio::Clip> clip = devex::audio::Clip::create({
        .encoding = info->encoding,
        .loading = devex::asset::AudioLoading::Decoded,
        .channels = info->channels,
        .sampleRate = info->sampleRate,
        .frames = info->frames,
        .waveform = info->waveform,
        .encoded = std::move(bytes),
    }).value();
    std::unique_ptr<devex::audio::AudioEngine> engine = devex::audio::AudioEngine::create({.device = false}).value();
    devex::asset::AudioSettings settings;
    settings.groupNames[1] = "Music";
    engine->configure(settings);
    devex::audio::AudioWorld audio(*engine, [&](devex::asset::AssetId) { return clip; });

    const devex::asset::AssetId clipId = devex::asset::AssetId::generate();
    Scene scene;
    const Entity speaker = scene.createEntity("Speaker");
    scene.add<devex::scene::Transform>(speaker);
    scene.add<devex::scene::AudioSource>(speaker,
                                         devex::scene::AudioSource{.clip = clipId, .loop = true, .playOnStart = false});
    void* const component = jukebox->emplace(scene, speaker);
    field<devex::asset::AssetId>(*jukebox, component, "clip") = clipId;

    ManagedGame::Frame frame{.scene = &scene, .audio = &audio};
    game->runPhase(frame, SystemPhase::Start);
    const auto state = [&]() { return const_cast<void*>(jukebox->find(scene, speaker)); };
    // The source and the one-shot play.
    CHECK(audio.isPlaying(speaker));
    CHECK(audio.soundCount() == 2);
    CHECK(engine->groupVolume(1) == Catch::Approx(0.25f));
    CHECK(engine->masterVolume() == Catch::Approx(0.5f));
    CHECK(field<float>(*jukebox, state(), "music_volume") == Catch::Approx(0.25f));
    CHECK(field<float>(*jukebox, state(), "master_volume") == Catch::Approx(0.5f));
    CHECK(field<std::string>(*jukebox, state(), "error").find("Nothing") != std::string::npos);

    frame.delta = devex::core::Duration(1.0 / 60.0);
    game->runPhase(frame, SystemPhase::Update);
    CHECK(field<bool>(*jukebox, state(), "was_playing"));
    CHECK(field<bool>(*jukebox, state(), "stopped"));
    CHECK_FALSE(audio.isPlaying(speaker));

    // Without audio, the calls do nothing.
    ManagedGame::Frame silent{.scene = &scene};
    game->runPhase(silent, SystemPhase::Update);
    CHECK_FALSE(field<bool>(*jukebox, state(), "was_playing"));
    game->unloadAssembly();
}
#endif
