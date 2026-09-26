#include "runtime/ManagedGame.hpp"

#include <devex/asset/Primitives.hpp>
#include <devex/audio/AudioWorld.hpp>
#include <devex/audio/Clip.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/scene/AudioComponents.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("The C# runtime registers components and runs them", "[runtime][managed]")
{
    const std::unique_ptr<ManagedGame> game = startRuntime();
    CHECK(game->componentTypes().size() == 9);

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
