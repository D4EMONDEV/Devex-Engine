#include "runtime/ManagedGame.hpp"

#include <devex/platform/Platform.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

using devex::runtime::SystemPhase;
using devex::runtime::detail::ManagedGame;
using devex::scene::Entity;
using devex::scene::Scene;

#ifdef DEVEX_TEST_MANAGED_GAME
TEST_CASE("The C# runtime registers components and runs them", "[runtime][managed]")
{
    const std::filesystem::path managed = devex::platform::executableDirectory() / "managed";
    devex::core::Result<std::unique_ptr<ManagedGame>> game = ManagedGame::create(managed, {});
    if (!game)
    {
        // .NET is not installed on this machine: games are written in C++ there.
        SKIP(game.error().message);
    }
    REQUIRE((*game)->loadAssembly(DEVEX_TEST_MANAGED_GAME));
    CHECK((*game)->componentTypes().size() == 2);

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
    CHECK(*static_cast<const float*>(mover->type->findField("speed")->address(component)) == 2.0f);
    CHECK(*static_cast<const std::string*>(mover->type->findField("label")->address(component)) == "new");
    *static_cast<float*>(mover->type->findField("speed")->address(component)) = 3.0f;
    *static_cast<devex::math::Vec3*>(mover->type->findField("direction")->address(component)) =
        devex::math::Vec3{0.0f, 1.0f, 0.0f};

    // Start runs once, then Update moves the entity and writes back into the engine's memory.
    (*game)->runPhase(scene, SystemPhase::Start, devex::core::Duration::zero());
    CHECK(*static_cast<const std::string*>(mover->type->findField("label")->address(component)) == "started");
    (*game)->runPhase(scene, SystemPhase::Update, devex::core::Duration(0.5));
    CHECK(scene.get<devex::scene::Transform>(entity).position.y == 1.5f);
    CHECK(*static_cast<const std::string*>(mover->type->findField("label")->address(component)) == "moved Runner");
    // A system ran over the whole scene, after the components of the phase.
    CHECK(scene.name(entity) == "renamed by the system");
    // A component the game removed from the scene loses its C# object without a trace.
    mover->remove(scene, entity);
    (*game)->runPhase(scene, SystemPhase::Update, devex::core::Duration(0.5));
    CHECK(scene.get<devex::scene::Transform>(entity).position.y == 1.5f);

    // Unloading keeps the values in the scene as text, and forgets the types.
    void* const again = mover->emplace(scene, entity);
    *static_cast<float*>(mover->type->findField("speed")->address(again)) = 7.0f;
    CHECK((*game)->release(scene) == 1);
    (*game)->unloadAssembly();
    CHECK(registry.find("Mover") == nullptr);
    CHECK(devex::scene::saveScene(scene).find("speed = 7") != std::string::npos);
}
#endif
