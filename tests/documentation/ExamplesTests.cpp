#include <devex/math/Math.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/runtime/GameModule.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <string>
#include <vector>

#ifdef DEVEX_DOC_MANAGED_GAME
#include "runtime/ManagedGame.hpp"
#include <devex/physics2d/Physics2DWorld.hpp>
#include <devex/scene/Physics2DComponents.hpp>

#include <cmath>
#include <memory>
#endif

using devex::runtime::GameModule;
using devex::runtime::SystemPhase;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

template <typename T>
T& field(const devex::scene::ComponentType& type, void* component, const char* name)
{
    const auto* const info = type.type->findField(name);
    REQUIRE(info != nullptr);
    return *static_cast<T*>(info->address(component));
}

#ifdef DEVEX_DOC_MANAGED_GAME
using devex::runtime::detail::ManagedGame;

std::unique_ptr<ManagedGame> startManagedExamples()
{
    auto game = ManagedGame::create(devex::platform::executableDirectory() / "managed");
    if (!game)
    {
        FAIL(game.error().message);
    }
    REQUIRE((*game)->loadAssembly(DEVEX_DOC_MANAGED_GAME));
    return std::move(*game);
}
#endif

} // namespace

TEST_CASE("The documented C++ game modules load and register their components",
          "[documentation][game]")
{
    struct Example
    {
        const char* path;
        const char* component;
    };
    for (const auto& example : std::array{
             Example{DEVEX_DOC_PLAYER_MODULE, "PlayerController"},
             Example{DEVEX_DOC_GRAVITY_MODULE, "GravityVolume"},
             Example{DEVEX_DOC_PARTICLES_MODULE, "FireParticle"},
             Example{DEVEX_DOC_DIALOGUE_MODULE, "Dialogue"}})
    {
        INFO(example.component);
        auto module = GameModule::load(example.path, {});
        REQUIRE(module.has_value());
        CHECK((*module)->registry().components().size() == 1);
        CHECK((*module)->registry().systems().size() == 1);
        CHECK(devex::scene::componentRegistry().find(example.component) != nullptr);
        module->reset();
        CHECK(devex::scene::componentRegistry().find(example.component) == nullptr);
    }
}

TEST_CASE("The documented particles remove every expired entity after iteration",
          "[documentation][game]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.hidden = true});
    REQUIRE(window.has_value());
    devex::runtime::AssetManager assets(nullptr, nullptr);
    auto module = GameModule::load(DEVEX_DOC_PARTICLES_MODULE, {});
    REQUIRE(module.has_value());
    const auto* const type = devex::scene::componentRegistry().find("FireParticle");
    REQUIRE(type != nullptr);

    Scene scene;
    Entity survivor;
    for (int index = 0; index < 4; ++index)
    {
        const Entity entity = scene.createEntity("Particle");
        scene.add<devex::scene::Transform>(entity);
        void* const particle = type->emplace(scene, entity);
        REQUIRE(particle != nullptr);
        field<float>(*type, particle, "lifetime") = index == 1 ? 2.0f : 0.25f;
        if (index == 1)
        {
            survivor = entity;
        }
    }
    devex::runtime::SystemContext context{
        .scene = scene, .input = platform->input(), .window = *window, .assets = assets,
        .delta = devex::core::Duration(0.5)};
    (*module)->registry().run(SystemPhase::Update, context);
    CHECK(scene.entityCount() == 1);
    REQUIRE(scene.isAlive(survivor));
    CHECK(scene.get<devex::scene::Transform>(survivor).position.y == Catch::Approx(0.5f));
    CHECK(field<float>(*type, const_cast<void*>(type->find(scene, survivor)), "lifetime") == 1.5f);
    // Pools instantiated by a module must be released while its library is still loaded.
    CHECK((*module)->release(scene) == 1);
}

#ifdef DEVEX_DOC_MANAGED_GAME
TEST_CASE("The documented 2D controller walks and only jumps from the ground",
          "[documentation][managed]")
{
    const auto game = startManagedExamples();
    const auto* const type = devex::scene::componentRegistry().find("PlayerController2D");
    REQUIRE(type != nullptr);
    Scene scene;
    const Entity player = scene.createEntity("Player");
    scene.add<devex::scene::Transform>(player);
    auto& controller = scene.add<devex::scene::CharacterController2D>(player);
    controller.grounded = true;
    REQUIRE(type->emplace(scene, player) != nullptr);

    devex::runtime::InputActions actions(devex::asset::InputSettings{
        .actions = {
            {.name = "Move", .kind = devex::asset::InputActionKind::Vector,
             .bindings = {{"key:D", devex::asset::InputDirection::Right}}},
            {.name = "Jump", .bindings = {{"key:Space"}}}}});
    devex::platform::Input input;
    input.beginFrame();
    input.setKeyDown(devex::platform::Key::D, true);
    input.setKeyDown(devex::platform::Key::Space, true);
    actions.update(input, false);
    ManagedGame::Frame frame{.scene = &scene, .input = &input, .actions = &actions};
    game->runPhase(frame, SystemPhase::Update);
    CHECK(controller.velocity.x == 5.0f);
    CHECK(controller.velocity.y == 7.0f);

    controller.grounded = false;
    controller.velocity.y = -1.0f;
    game->runPhase(frame, SystemPhase::Update);
    CHECK(controller.velocity.y == -1.0f);
}

TEST_CASE("The documented quest completes after its collectible is destroyed",
          "[documentation][managed]")
{
    const auto game = startManagedExamples();
    auto& registry = devex::scene::componentRegistry();
    const auto* const inventory = registry.find("PlayerInventory");
    const auto* const quest = registry.find("Quest");
    const auto* const collectible = registry.find("Collectible");
    REQUIRE(inventory != nullptr);
    REQUIRE(quest != nullptr);
    REQUIRE(collectible != nullptr);
    auto world = devex::physics2d::Physics2DWorld::create({});
    REQUIRE(world.has_value());
    Scene scene;
    const Entity player = scene.createEntity("Player");
    scene.add<devex::scene::Transform>(player);
    scene.add<devex::scene::RigidBody2D>(player).gravityScale = 0.0f;
    scene.add<devex::scene::BoxCollider2D>(player);
    REQUIRE(inventory->emplace(scene, player) != nullptr);
    void* const questData = quest->emplace(scene, player);
    REQUIRE(questData != nullptr);
    const Entity item = scene.createEntity("Key");
    scene.add<devex::scene::Transform>(item);
    scene.add<devex::scene::BoxCollider2D>(item).trigger = true;
    REQUIRE(collectible->emplace(scene, item) != nullptr);
    scene.updateTransforms();
    (*world)->step(scene, devex::core::Duration(1.0 / 60.0));
    ManagedGame::Frame frame{.scene = &scene, .physics2d = world->get()};
    game->runPhase(frame, SystemPhase::Update);
    REQUIRE_FALSE(scene.isAlive(item));
    (*world)->clearContacts();
    game->runPhase(frame, SystemPhase::Update);
    CHECK(field<bool>(*quest, questData, "is_completed"));
    void* const inventoryData = const_cast<void*>(inventory->find(scene, player));
    REQUIRE(inventoryData != nullptr);
    CHECK(field<std::vector<std::string>>(*inventory, inventoryData, "collected_items") ==
          std::vector<std::string>{"key"});
}

TEST_CASE("The documented camera follows at different frame rates and faces its target",
          "[documentation][managed]")
{
    const int steps = GENERATE(30, 60, 144);
    const auto game = startManagedExamples();
    const auto* const type = devex::scene::componentRegistry().find("FollowCamera");
    REQUIRE(type != nullptr);
    Scene scene;
    const Entity target = scene.createEntity("Target");
    scene.add<devex::scene::Transform>(target).position = {3.0f, 0.0f, 0.0f};
    const Entity camera = scene.createEntity("Camera");
    scene.add<devex::scene::Transform>(camera).position = {0.0f, 2.0f, 10.0f};
    void* const component = type->emplace(scene, camera);
    REQUIRE(component != nullptr);
    field<devex::scene::EntityRef>(*type, component, "target") = scene.reference(target);
    ManagedGame::Frame frame{.scene = &scene, .delta = devex::core::Duration(1.0 / steps)};
    for (int index = 0; index < steps; ++index)
    {
        scene.updateTransforms();
        game->runPhase(frame, SystemPhase::LateUpdate);
    }
    const auto& transform = scene.get<devex::scene::Transform>(camera);
    CHECK(transform.position.x == Catch::Approx(3.0f * (1.0f - std::exp(-5.0f))).margin(0.0001f));
    CHECK(transform.position.y == Catch::Approx(2.0f));
    CHECK(transform.position.z == Catch::Approx(5.0f + 5.0f * std::exp(-5.0f)).margin(0.0001f));
    const auto forward = transform.rotation * devex::math::Vec3{0.0f, 0.0f, -1.0f};
    const auto direction = devex::math::normalize(
        scene.get<devex::scene::Transform>(target).position - transform.position);
    CHECK(devex::math::dot(forward, direction) > 0.9999f);
}
#endif
